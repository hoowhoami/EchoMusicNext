#include "EmbeddedCoverCache.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QRegularExpression>
#include <QSaveFile>
#include <QUrl>
#include <atomic>
#include <algorithm>
#include <limits>
#include <utility>

namespace EchoMusic
{
namespace
{
QString digest(const QString &value)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(value.toUtf8(), QCryptographicHash::Sha256).toHex());
}
bool validKey(const QString &value)
{
    static const QRegularExpression pattern(QStringLiteral("^[a-f0-9]{64}$"));
    return pattern.match(value).hasMatch();
}
bool validFile(const QString &value)
{
    static const QRegularExpression pattern(QStringLiteral("^[a-f0-9]{64}\\.(png|jpg|gif|webp)$"));
    return pattern.match(value).hasMatch();
}
QString extension(const QByteArray &header)
{
    if (header.size() < 12)
        return {};
    if (header.startsWith("\x89PNG\r\n\x1a\n"))
        return "png";
    if (header.startsWith(QByteArray::fromHex("ffd8ff")))
        return "jpg";
    if (header.startsWith("GIF87a") || header.startsWith("GIF89a"))
        return "gif";
    if (header.startsWith("RIFF") && header.mid(8, 4) == "WEBP")
        return "webp";
    return {};
}
} // namespace
struct EmbeddedCoverCache::State
{
    QString root;
    Limits limits;
    QList<Record> records;
    std::atomic<quint64> epoch{0};
    std::atomic_bool closing{false};
    bool loaded = false;
    bool current(quint64 version) const
    {
        return !closing && epoch == version;
    }
    bool ensureRoot()
    {
        return QDir().mkpath(root) && !QFileInfo(root).isSymLink();
    }
    void save()
    {
        QJsonArray rows;
        for (const auto &r : records)
            rows.append(QJsonObject{{"scope", r.scope},
                                    {"file", r.file},
                                    {"keys", QJsonArray::fromStringList(r.keys)},
                                    {"created", r.created},
                                    {"used", r.used},
                                    {"bytes", r.bytes},
                                    {"modified", r.modified}});
        QSaveFile output(QDir(root).filePath("index.json"));
        if (!output.open(QIODevice::WriteOnly))
            return;
        const auto bytes = QJsonDocument(QJsonObject{{"version", 1}, {"entries", rows}})
                               .toJson(QJsonDocument::Compact);
        if (output.write(bytes) == bytes.size())
            output.commit();
    }
    void load()
    {
        if (loaded || !ensureRoot())
            return;
        loaded = true;
        QFile input(QDir(root).filePath("index.json"));
        if (input.open(QIODevice::ReadOnly) && input.size() <= 1024 * 1024) {
            const auto document = QJsonDocument::fromJson(input.readAll()).object();
            if (document["version"].toInt() == 1)
                for (const auto &value : document["entries"].toArray()) {
                    if (records.size() >= 512)
                        break;
                    const auto row = value.toObject();
                    Record r{row["scope"].toString(), row["file"].toString()};
                    for (const auto &key : row["keys"].toArray())
                        if (validKey(key.toString()) && r.keys.size() < 2)
                            r.keys.append(key.toString());
                    r.created = row["created"].toInteger();
                    r.used = row["used"].toInteger();
                    r.bytes = row["bytes"].toInteger();
                    r.modified = row["modified"].toInteger();
                    if (validKey(r.scope) && validFile(r.file) && !r.keys.isEmpty() &&
                        r.bytes > 0 && r.bytes <= limits.fileBytes)
                        records.append(r);
                }
        }
    }
    void sweep()
    {
        load();
        if (!loaded || closing || !ensureRoot())
            return;
        const auto now = QDateTime::currentMSecsSinceEpoch();
        const auto previous = records.size();
        records.removeIf([&](const Record &r) {
            const QFileInfo file(QDir(root).filePath(r.file));
            return r.created <= 0 || r.created > now || now - r.created >= limits.ageMs ||
                   !file.isFile() || file.isSymLink() || file.size() != r.bytes ||
                   file.lastModified().toMSecsSinceEpoch() != r.modified;
        });
        std::stable_sort(records.begin(), records.end(),
                         [](const Record &a, const Record &b) { return a.used > b.used; });
        QHash<QString, qint64> files;
        qint64 bytes = 0;
        QList<Record> retained;
        for (const auto &r : records) {
            const auto extra = files.contains(r.file) ? 0 : r.bytes;
            if (retained.size() >= limits.entries || bytes + extra > limits.bytes)
                continue;
            retained.append(r);
            files.insert(r.file, r.bytes);
            bytes += extra;
        }
        records = std::move(retained);
        // Remove only files owned by this cache. This also reclaims interrupted
        // publications/orphans; source reader files live in another directory.
        const auto entries = QDir(root).entryInfoList(QDir::Files | QDir::NoSymLinks);
        for (const auto &file : entries) {
            if (closing)
                return;
            if (validFile(file.fileName()) && !files.contains(file.fileName()))
                QFile::remove(file.filePath());
        }
        if (records.size() != previous || !QFileInfo::exists(QDir(root).filePath("index.json")))
            save();
    }
    void storeFile(const QString &scope, const QStringList &keys, const QString &source,
                   quint64 version)
    {
        if (!current(version))
            return;
        sweep();
        const QFileInfo info(source);
        if (!current(version) || !info.isFile() || info.isSymLink() || info.size() <= 0 ||
            info.size() > std::min(limits.fileBytes, limits.bytes))
            return;
        QFile input(source);
        if (!input.open(QIODevice::ReadOnly))
            return;
        const auto ext = extension(input.peek(12));
        if (ext.isEmpty())
            return;
        QCryptographicHash hash(QCryptographicHash::Sha256);
        while (!input.atEnd() && current(version)) {
            const auto bytes = input.read(64 * 1024);
            if (bytes.isEmpty())
                return;
            hash.addData(bytes);
        }
        if (!current(version) || input.pos() != info.size() || input.error() != QFile::NoError)
            return;
        const auto name = QString::fromLatin1(hash.result().toHex()) + "." + ext;
        const auto destination = QDir(root).filePath(name);
        const QFileInfo existing(destination);
        if (existing.isSymLink())
            return;
        if (!existing.isFile() || existing.size() != info.size()) {
            if (!input.seek(0))
                return;
            QSaveFile output(destination);
            if (!output.open(QIODevice::WriteOnly))
                return;
            QCryptographicHash copied(QCryptographicHash::Sha256);
            while (!input.atEnd() && current(version)) {
                const auto bytes = input.read(64 * 1024);
                if (bytes.isEmpty() || output.write(bytes) != bytes.size())
                    return;
                copied.addData(bytes);
            }
            const QFileInfo after(source);
            if (!current(version) || input.error() != QFile::NoError ||
                input.pos() != info.size() || after.size() != info.size() ||
                after.lastModified() != info.lastModified() || copied.result() != hash.result() ||
                !output.commit())
                return;
        }
        if (!current(version))
            return;
        const auto now = QDateTime::currentMSecsSinceEpoch();
        records.removeIf([&](const Record &r) {
            return r.scope == scope &&
                   std::any_of(keys.begin(), keys.end(),
                               [&](const QString &key) { return r.keys.contains(key); });
        });
        records.append({scope, name, keys, now, now, info.size(),
                        QFileInfo(destination).lastModified().toMSecsSinceEpoch()});
        sweep();
        save();
    }
};
EmbeddedCoverCache::EmbeddedCoverCache(QString root, Limits limits, QObject *parent)
    : QObject(parent), m_directory(std::move(root)), m_limits(limits), m_worker(new QObject),
      m_state(std::make_shared<State>())
{
    m_state->root = m_directory;
    m_state->limits = limits;
    m_worker->moveToThread(&m_thread);
    m_thread.setObjectName("echo-embedded-cover-cache");
    connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(&m_thread, &QThread::finished, this, &EmbeddedCoverCache::closed);
    m_expiry.setSingleShot(true);
    m_check.setSingleShot(true);
    m_touch.setSingleShot(true);
    m_check.setInterval(250);
    m_touch.setInterval(1000);
    connect(&m_expiry, &QTimer::timeout, this, &EmbeddedCoverCache::maintain);
    connect(&m_check, &QTimer::timeout, this, &EmbeddedCoverCache::maintain);
    connect(&m_touch, &QTimer::timeout, this, &EmbeddedCoverCache::touch);
    connect(&m_files, &QFileSystemWatcher::fileChanged, this, [this] {
        if (!m_closing)
            m_check.start();
    });
    connect(&m_files, &QFileSystemWatcher::directoryChanged, this, [this] {
        if (!m_closing)
            m_check.start();
    });
    m_thread.start();
    setScope(0);
}
EmbeddedCoverCache::~EmbeddedCoverCache()
{
    shutdown();
    m_thread.wait();
}
QString EmbeddedCoverCache::identityKey(const QString &identity) const
{
    return digest(m_scope + "\n" + identity);
}
void EmbeddedCoverCache::setScope(quint64 account)
{
    if (m_closing)
        return;
    m_scope = digest(QString::number(account));
    ++m_epoch;
    m_state->epoch = m_epoch;
    const auto keys = m_index.keys();
    m_index.clear();
    m_pending.clear();
    m_touched.clear();
    m_checkAgain = false;
    m_expiry.stop();
    m_touch.stop();
    emit changed(keys);
    maintain();
}
void EmbeddedCoverCache::apply(quint64 version, const QList<Record> &records)
{
    if (m_closing || version != m_epoch)
        return;
    QHash<QString, Record> next;
    for (const auto &r : records)
        if (r.scope == m_scope)
            for (const auto &key : r.keys)
                next.insert(key, r);
    QSet<QString> changedKeys;
    for (auto it = m_index.cbegin(); it != m_index.cend(); ++it)
        if (!next.contains(it.key()) ||
            (next[it.key()].file != it->file || next[it.key()].created != it->created))
            changedKeys.insert(it.key());
    for (auto it = next.cbegin(); it != next.cend(); ++it)
        if (!m_index.contains(it.key()) ||
            (m_index[it.key()].file != it->file || m_index[it.key()].created != it->created))
            changedKeys.insert(it.key());
    m_index = std::move(next);
    if (!m_files.directories().contains(m_directory))
        m_files.addPath(m_directory);
    QSet<QString> wanted;
    for (const auto &record : m_index)
        wanted.insert(QDir(m_directory).filePath(record.file));
    const auto watching = m_files.files();
    QStringList remove;
    for (const auto &file : watching) {
        if (!wanted.remove(file))
            remove.append(file);
    }
    if (!remove.isEmpty())
        m_files.removePaths(remove);
    if (!wanted.isEmpty())
        m_files.addPaths(QStringList(wanted.cbegin(), wanted.cend()));
    armExpiry();
    if (!changedKeys.isEmpty())
        emit changed(QStringList(changedKeys.cbegin(), changedKeys.cend()));
    emit ready();
}
QString EmbeddedCoverCache::lookup(const QStringList &identities)
{
    const auto now = QDateTime::currentMSecsSinceEpoch();
    for (const auto &identity : identities) {
        const auto key = identityKey(identity);
        auto it = m_index.find(key);
        if (it == m_index.end())
            continue;
        if (now < it->created || now - it->created >= m_limits.ageMs) {
            m_check.start();
            continue;
        }
        m_touched.insert(key, now);
        if (!m_touch.isActive())
            m_touch.start();
        auto url = QUrl::fromLocalFile(QDir(m_directory).filePath(it->file));
        // Recreated files must not reuse an Image error cached for an evicted
        // resource. Re-importing unchanged content keeps the same image key.
        url.setQuery(QStringLiteral("v=%1").arg(it->modified));
        return url.toString();
    }
    return {};
}
void EmbeddedCoverCache::store(const QStringList &identities, const QString &source)
{
    if (m_closing || identities.isEmpty() || source.isEmpty() || m_pending.size() >= 8)
        return;
    QStringList keys;
    for (const auto &identity : identities)
        if (!identity.startsWith("id:"))
            keys.append(identityKey(identity));
    if (keys.isEmpty() || m_pending.contains(keys.first()))
        return;
    keys = keys.mid(0, 2);
    touch();
    m_touch.stop();
    const auto key = keys.first();
    m_pending.insert(key);
    const auto state = m_state;
    const auto scope = m_scope;
    const auto version = m_epoch;
    QMetaObject::invokeMethod(m_worker, [this, state, keys, key, scope, source, version] {
        state->storeFile(scope, keys, source, version);
        const auto rows = state->records;
        QMetaObject::invokeMethod(this, [this, key, version, rows] {
            if (version != m_epoch)
                return;
            m_pending.remove(key);
            apply(version, rows);
        });
    });
}
void EmbeddedCoverCache::maintain()
{
    if (m_closing)
        return;
    if (m_checkVersion == m_epoch) {
        m_checkAgain = true;
        return;
    }
    const auto state = m_state;
    const auto version = m_epoch;
    m_checkVersion = version;
    QMetaObject::invokeMethod(m_worker, [this, state, version] {
        if (state->current(version))
            state->sweep();
        const auto rows = state->records;
        QMetaObject::invokeMethod(this, [this, version, rows] {
            if (m_checkVersion == version)
                m_checkVersion = 0;
            apply(version, rows);
            if (version == m_epoch && std::exchange(m_checkAgain, false))
                maintain();
        });
    });
}
void EmbeddedCoverCache::touch()
{
    if (m_closing || m_touched.isEmpty())
        return;
    const auto keys = std::exchange(m_touched, {});
    const auto state = m_state;
    const auto version = m_epoch;
    QMetaObject::invokeMethod(m_worker, [state, version, keys] {
        if (!state->current(version))
            return;
        for (auto &r : state->records)
            for (const auto &key : r.keys)
                if (keys.contains(key))
                    r.used = std::max(r.used, keys.value(key));
        state->save();
    });
}
void EmbeddedCoverCache::armExpiry()
{
    qint64 deadline = std::numeric_limits<int>::max();
    const auto now = QDateTime::currentMSecsSinceEpoch();
    for (const auto &r : m_index)
        deadline = std::min(deadline, std::max(qint64(1), r.created + m_limits.ageMs - now));
    if (!m_index.isEmpty())
        m_expiry.start(int(deadline));
    else
        m_expiry.stop();
}
void EmbeddedCoverCache::shutdown()
{
    if (std::exchange(m_closing, true))
        return;
    m_state->closing = true;
    ++m_state->epoch;
    m_expiry.stop();
    m_check.stop();
    m_touch.stop();
    const auto watched = m_files.directories() + m_files.files();
    if (!watched.isEmpty())
        m_files.removePaths(watched);
    m_index.clear();
    m_pending.clear();
    m_touched.clear();
    QMetaObject::invokeMethod(m_worker, [] { QThread::currentThread()->quit(); });
}
} // namespace EchoMusic
