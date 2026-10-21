#include "DspProviderRegistry_p.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSqlQuery>
#include <QUuid>
#include <QTimer>
#include <QRegularExpression>
#include <utility>

namespace EchoMusic
{
DspProviderStore::DspProviderStore(QString directory) : m_directory(std::move(directory)) {}
DspProviderStore::~DspProviderStore()
{
    close();
}
void DspProviderStore::close()
{
    if (!m_database.isValid())
        return;
    const auto name = m_database.connectionName();
    m_database.close();
    m_database = {};
    QSqlDatabase::removeDatabase(name);
}
void DspProviderStore::open()
{
    Q_ASSERT(thread() == QThread::currentThread());
    if (!QDir().mkpath(m_directory)) {
        emit failed(tr("无法创建音效引擎目录"));
        return;
    }
    m_database = QSqlDatabase::addDatabase("QSQLITE", QUuid::createUuid().toString());
    m_database.setDatabaseName(m_directory + "/catalog.sqlite");
    if (!m_database.open()) {
        emit failed(tr("无法打开音效引擎目录数据库"));
        return;
    }
    QSqlQuery q(m_database);
    if (!q.exec("PRAGMA user_version") || !q.next() || q.value(0).toInt() > 1) {
        m_database.close();
        emit failed(tr("音效引擎目录数据库版本不受支持"));
        return;
    }
    if (!q.exec("CREATE TABLE IF NOT EXISTS dsp_engines (id TEXT PRIMARY KEY, path TEXT NOT NULL, "
                "record TEXT NOT NULL)") ||
        !q.exec("CREATE TABLE IF NOT EXISTS retired_libraries (path TEXT PRIMARY KEY)") ||
        !q.exec("PRAGMA user_version=1")) {
        m_database.close();
        emit failed(tr("无法初始化音效引擎目录数据库"));
        return;
    }
    // Collect imports interrupted before their SQL commit, plus retired files.
    QDir dir(m_directory);
    for (const auto &name : dir.entryList({".import-*"}, QDir::Files | QDir::Hidden))
        QFile::remove(dir.absoluteFilePath(name));
    for (const auto &name : dir.entryList({"*.dylib", "*.dll", "*.so"}, QDir::Files)) {
        const auto path = dir.absoluteFilePath(name);
        QSqlQuery used(m_database);
        used.prepare("SELECT 1 FROM dsp_engines WHERE path=?");
        used.addBindValue(path);
        if (used.exec() && !used.next())
            discard(path);
    }
    // Unix unlink would succeed even while a graph still maps the library.
    // Conservatively retain retired versions until the next process startup.
#ifdef Q_OS_WIN
    auto *timer = new QTimer(this);
    timer->setInterval(5000);
    connect(timer, &QTimer::timeout, this, &DspProviderStore::collect);
    timer->start();
#endif
    collect();
    publish();
}
void DspProviderStore::prepare(const QString &path)
{
    if (!m_database.isOpen()) {
        emit failed(tr("音效引擎目录未就绪"));
        return;
    }
    QFile source(path);
#ifdef Q_OS_WIN
    const QString extension = ".dll";
#elif defined(Q_OS_MACOS)
    const QString extension = ".dylib";
#else
    const QString extension = ".so";
#endif
    if (!path.endsWith(extension, Qt::CaseInsensitive) || !source.open(QIODevice::ReadOnly) ||
        source.size() <= 0 || source.size() > 256 * 1024 * 1024) {
        emit failed(tr("请选择当前平台的音效引擎动态库（最大256 MiB）"));
        return;
    }
    const auto staging =
        m_directory + "/.import-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
    QSaveFile out(staging);
    if (!out.open(QIODevice::WriteOnly)) {
        emit failed(tr("无法暂存音效引擎"));
        return;
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    qint64 total = 0;
    while (!source.atEnd()) {
        const auto bytes = source.read(1024 * 1024);
        total += bytes.size();
        if (bytes.isEmpty() || total > 256 * 1024 * 1024 || out.write(bytes) != bytes.size() ||
            QThread::currentThread()->isInterruptionRequested()) {
            out.cancelWriting();
            emit failed(tr("音效引擎复制失败或已取消"));
            return;
        }
        hash.addData(bytes);
    }
    if (!out.commit()) {
        emit failed(tr("无法发布音效引擎文件"));
        return;
    }
    const auto digest = QString::fromLatin1(hash.result().toHex());
    const auto target = m_directory + "/" + digest + extension;
    // Identical reimports are safe even when Windows currently maps this file.
    if (QFileInfo::exists(target))
        QFile::remove(staging);
    else if (!QFile::rename(staging, target)) {
        QFile::remove(staging);
        emit failed(tr("无法发布音效引擎文件"));
        return;
    }
    emit prepared(
        {{"path", target}, {"contentHash", digest}, {"fileName", QFileInfo(path).fileName()}});
}
void DspProviderStore::commit(const QVariantMap &record)
{
    const auto id = record.value("id").toString(), path = record.value("path").toString();
    if (!m_database.transaction()) {
        emit failed(tr("无法保存音效引擎"));
        return;
    }
    QSqlQuery previous(m_database);
    previous.prepare("SELECT path FROM dsp_engines WHERE id=?");
    previous.addBindValue(id);
    if (!previous.exec()) {
        m_database.rollback();
        emit failed(tr("无法读取音效引擎版本"));
        return;
    }
    const auto old = previous.next() ? previous.value(0).toString() : QString();
    QSqlQuery q(m_database);
    q.prepare("INSERT OR REPLACE INTO dsp_engines(id,path,record) VALUES(?,?,?)");
    q.addBindValue(id);
    q.addBindValue(path);
    q.addBindValue(
        QString::fromUtf8(QJsonDocument::fromVariant(record).toJson(QJsonDocument::Compact)));
    bool ok = q.exec();
    if (ok && !old.isEmpty() && old != path) {
        q.prepare("INSERT OR IGNORE INTO retired_libraries(path) VALUES(?)");
        q.addBindValue(old);
        ok = q.exec();
    }
    if (!ok || !m_database.commit()) {
        m_database.rollback();
        emit failed(tr("无法保存音效引擎"));
        return;
    }
    // Do not collect a live library synchronously; graph/overlap/tail instances
    // own their mappings. Windows removal is retried until the mapping releases.
    publish();
}
void DspProviderStore::remove(const QString &id)
{
    if (!m_database.transaction()) {
        emit failed(tr("无法移除音效引擎"));
        return;
    }
    QSqlQuery q(m_database);
    q.prepare("INSERT OR IGNORE INTO retired_libraries SELECT path FROM dsp_engines WHERE id=?");
    q.addBindValue(id);
    bool ok = q.exec();
    q.prepare("DELETE FROM dsp_engines WHERE id=?");
    q.addBindValue(id);
    ok = q.exec() && ok;
    if (!ok || !m_database.commit()) {
        m_database.rollback();
        emit failed(tr("无法移除音效引擎"));
        return;
    }
    publish();
}
void DspProviderStore::discard(const QString &path)
{
    if (QFileInfo(path).absolutePath() != QDir(m_directory).absolutePath())
        return;
    QSqlQuery q(m_database);
    q.prepare("INSERT OR IGNORE INTO retired_libraries(path) VALUES(?)");
    q.addBindValue(path);
    q.exec();
}
void DspProviderStore::collect()
{
    QSqlQuery q(m_database);
    if (!q.exec("SELECT path FROM retired_libraries WHERE path NOT IN (SELECT path FROM "
                "dsp_engines) LIMIT 32"))
        return;
    QStringList paths;
    while (q.next())
        paths.append(q.value(0).toString());
    for (const auto &path : paths) {
        if (QFileInfo(path).absolutePath() != QDir(m_directory).absolutePath())
            continue;
        if (!QFileInfo::exists(path) || QFile::remove(path)) {
            QSqlQuery done(m_database);
            done.prepare("DELETE FROM retired_libraries WHERE path=?");
            done.addBindValue(path);
            done.exec();
        }
    }
}
void DspProviderStore::publish()
{
    QSqlQuery q(m_database);
    if (!q.exec("SELECT record FROM dsp_engines ORDER BY id")) {
        emit failed(tr("无法读取音效引擎目录"));
        return;
    }
    QVariantList rows;
    while (q.next()) {
        auto record = QJsonDocument::fromJson(q.value(0).toByteArray()).object().toVariantMap();
        const auto path = record.value("path").toString(),
                   digest = record.value("contentHash").toString();
        static const QRegularExpression digestPattern(QStringLiteral("^[a-f0-9]{64}$"));
        if (!digestPattern.match(digest).hasMatch() ||
            QFileInfo(path).absolutePath() != QDir(m_directory).absolutePath() ||
            QFileInfo(path).completeBaseName() != digest || record.value("id").toString().isEmpty())
            continue;
        QFile library(path);
        QCryptographicHash hash(QCryptographicHash::Sha256);
        // Restore validates immutable versions on this worker, before the GUI
        // can select an absent/corrupted library. Never hash on the audio thread.
        if (!library.open(QIODevice::ReadOnly) || !hash.addData(&library) ||
            QString::fromLatin1(hash.result().toHex()) != digest)
            continue;
        rows.append(record);
    }
    emit loaded(rows);
}
DspProviderRegistry::DspProviderRegistry(NativeBridge &native, const QString &directory,
                                         QObject *parent)
    : QAbstractListModel(parent), m_native(native), m_store(new DspProviderStore(directory))
{
    m_store->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, m_store, &QObject::deleteLater);
    connect(m_store, &DspProviderStore::failed, this, &DspProviderRegistry::fail);
    connect(m_store, &DspProviderStore::loaded, this, [this](const QVariantList &rows) {
        beginResetModel();
        m_rows = rows;
        endResetModel();
        m_busy = false;
        m_error.clear();
        emit changed();
        if (!m_restored) {
            m_restored = true;
            emit restored();
        }
        if (!m_removing.isEmpty()) {
            m_removing.clear();
            emit notice(tr("音效引擎已删除，文件将在引擎释放后完成清理"), true);
        }
        if (!m_importing.isEmpty()) {
            const auto id = std::exchange(m_importing, {});
            emit imported(id);
        }
    });
    connect(m_store, &DspProviderStore::prepared, this, [this](const QVariantMap &candidate) {
        m_candidate = candidate;
        m_probe = m_native.submit(NativeBridge::InspectDsp, candidate.value("path").toString(), 0);
        if (!m_probe)
            fail(tr("音效引擎检查未被接受"));
    });
    connect(&native, &NativeBridge::completed, this,
            [this](quint64 id, quint64, bool success, const QByteArray &bytes) {
                if (!m_probe || id != m_probe)
                    return;
                m_probe = 0;
                if (!success) {
                    fail(tr("音效引擎检查失败：%1").arg(QString::fromUtf8(bytes)));
                    return;
                }
                const auto info = QJsonDocument::fromJson(bytes).object();
                const auto providerId = info.value("provider_id").toString().trimmed();
                const auto rawManifest = info.value("manifest_json").toString().toUtf8();
                QJsonParseError parseError;
                const auto document = QJsonDocument::fromJson(rawManifest, &parseError);
                auto manifest = document.object();
                const bool declared = !rawManifest.trimmed().isEmpty() && !manifest.isEmpty();
                // ABI2 permits absent optional manifest metadata. Such providers
                // still process audio; they simply have no generated extension UI.
                if (rawManifest.trimmed().isEmpty() ||
                    (parseError.error == QJsonParseError::NoError && document.isObject() &&
                     manifest.isEmpty()))
                    manifest = {{"resources", QJsonArray()},
                                {"presets", QJsonArray()},
                                {"controls", QJsonArray()}};
                if (providerId.isEmpty() || providerId.size() > 256 ||
                    providerId.contains(QChar::Null) ||
                    (!rawManifest.trimmed().isEmpty() &&
                     (parseError.error != QJsonParseError::NoError || !document.isObject())) ||
                    (declared && manifest.value("schemaVersion").toInt() != 1)) {
                    fail(tr("音效引擎身份或manifest无效"));
                    return;
                }
                auto record = m_candidate;
                record.insert("id", providerId);
                record.insert("version", info.value("provider_version").toString());
                record.insert("name", manifest.value("displayName").toString(providerId));
                record.insert("description", manifest.value("description").toString());
                record.insert("manifest", manifest.toVariantMap());
                record.insert("manifestDeclared", declared);
                record.insert("latencyFrames", info.value("latency_frames").toInt());
                record.insert("maxChannels", info.value("max_channels").toInt());
                record.insert("state",
                              QJsonDocument::fromJson(info.value("state_json").toString().toUtf8())
                                  .object()
                                  .toVariantMap());
                m_candidate.clear();
                m_importing = providerId;
                QMetaObject::invokeMethod(m_store,
                                          [store = m_store, record] { store->commit(record); });
            });
    m_thread.start();
    QMetaObject::invokeMethod(m_store, &DspProviderStore::open);
}
DspProviderRegistry::~DspProviderRegistry()
{
    m_thread.requestInterruption();
    QMetaObject::invokeMethod(m_store, &DspProviderStore::close, Qt::BlockingQueuedConnection);
    m_thread.quit();
    m_thread.wait();
}
int DspProviderRegistry::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_rows.size();
}
QVariant DspProviderRegistry::data(const QModelIndex &index, int role) const
{
    return index.isValid() && index.row() >= 0 && index.row() < m_rows.size() &&
                   role == Qt::UserRole
               ? m_rows[index.row()]
               : QVariant();
}
QHash<int, QByteArray> DspProviderRegistry::roleNames() const
{
    return {{Qt::UserRole, "details"}};
}
QVariantMap DspProviderRegistry::record(const QString &id) const
{
    for (const auto &r : m_rows)
        if (r.toMap().value("id").toString() == id)
            return r.toMap();
    return {};
}
void DspProviderRegistry::importFile(const QUrl &url)
{
    if (m_busy)
        return;
    if (!url.isLocalFile()) {
        fail(tr("请选择本地音效引擎文件"));
        return;
    }
    m_busy = true;
    m_error.clear();
    emit changed();
    QMetaObject::invokeMethod(
        m_store, [store = m_store, path = url.toLocalFile()] { store->prepare(path); });
}
void DspProviderRegistry::remove(const QString &id)
{
    if (m_busy || record(id).isEmpty())
        return;
    m_busy = true;
    m_removing = id;
    emit changed();
    QMetaObject::invokeMethod(m_store, [store = m_store, id] { store->remove(id); });
}
void DspProviderRegistry::fail(const QString &message)
{
    const auto path = m_candidate.value("path").toString();
    if (!path.isEmpty())
        QMetaObject::invokeMethod(m_store, [store = m_store, path] { store->discard(path); });
    m_candidate.clear();
    m_importing.clear();
    m_busy = false;
    m_error = message;
    m_removing.clear();
    emit changed();
    if (m_restored)
        emit notice(message, false);
}
} // namespace EchoMusic
