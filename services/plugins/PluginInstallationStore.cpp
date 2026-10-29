#include <QSet>
#include <QFileInfo>
#include "PluginInstallationStore.h"
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QUuid>

namespace EchoMusic
{
bool PluginInstallationStore::validToken(const QString &token)
{
    static const QRegularExpression pattern(
        "^[a-f0-9]{8}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{12}$");
    return pattern.match(token).hasMatch();
}
QVariantMap PluginInstallationStore::read(QString *error) const
{
    QFile file(m_paths.state("catalog.json"));
    if (!file.exists())
        return {{"version", 1}, {"installations", QVariantList{}}, {"retained", QVariantList{}}};
    if (!file.open(QIODevice::ReadOnly) || file.size() > 4 * 1024 * 1024) {
        if (error)
            *error = "Cannot read plugin installation catalog";
        return {};
    }
    QJsonParseError parse;
    const auto doc = QJsonDocument::fromJson(file.readAll(), &parse);
    const auto value = doc.object().toVariantMap();
    if (parse.error != QJsonParseError::NoError || value.value("version") != 1 ||
        value.value("installations").metaType().id() != QMetaType::QVariantList ||
        value.value("retained").metaType().id() != QMetaType::QVariantList) {
        if (error)
            *error = "Invalid plugin installation catalog";
        return {};
    }
    QSet<QString> ids, tokens;
    for (const auto &item : value.value("installations").toList()) {
        const auto r = item.toMap();
        const auto id = r.value("id").toString(), token = r.value("installation").toString();
        if (!PluginPaths::validId(id) || !validToken(token) || ids.contains(id) ||
            tokens.contains(token)) {
            if (error)
                *error = "Invalid or duplicate installed plugin identity";
            return {};
        }
        ids.insert(id);
        tokens.insert(token);
    }
    return value;
}
QString PluginInstallationStore::write(const QVariantMap &catalog) const
{
    if (!QDir().mkpath(m_paths.state()))
        return "Cannot create plugin state directory";
    QSaveFile file(m_paths.state("catalog.json"));
    const auto bytes = QJsonDocument::fromVariant(catalog).toJson(QJsonDocument::Compact);
    if (bytes.size() > 4 * 1024 * 1024)
        return "Plugin installation catalog exceeds its limit";
    if (!file.open(QIODevice::WriteOnly))
        return "Cannot write plugin installation catalog";
    file.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
    return file.write(bytes) == bytes.size() && file.commit()
               ? QString()
               : "Cannot commit plugin installation catalog";
}
QVariantList PluginInstallationStore::records(QString *error) const
{
    auto rows = read(error).value("installations").toList();
    for (auto &value : rows) {
        auto r = value.toMap();
        r["root"] = m_paths.code(r.value("id").toString());
        value = r;
    }
    return rows;
}
QString PluginInstallationStore::save(const QVariantList &records) const
{
    QString error;
    auto catalog = read(&error);
    if (!error.isEmpty())
        return error;
    QVariantList rows;
    for (const auto &value : records) {
        auto r = value.toMap();
        r.remove("root");
        rows.append(r);
    }
    catalog["installations"] = rows;
    // The new owner and removal of the root retained-data alias commit together.
    // Archives for other authors keep their explicit, separate paths.
    QSet<QString> installed;
    for (const auto &row : rows)
        installed.insert(row.toMap().value("id").toString());
    QVariantList retained;
    for (const auto &value : catalog.value("retained").toList()) {
        const auto record = value.toMap();
        if (!installed.contains(record.value("id").toString()) ||
            !record.value("path").toString().isEmpty())
            retained.append(value);
    }
    catalog["retained"] = retained;
    return write(catalog);
}
QVariantList PluginInstallationStore::retained(QString *error) const
{
    return read(error).value("retained").toList();
}
QString PluginInstallationStore::retain(const QVariantMap &record, bool keep) const
{
    QString error;
    auto catalog = read(&error);
    if (!error.isEmpty())
        return error;
    auto rows = catalog.value("retained").toList();
    rows.removeIf([&](const QVariant &value) {
        return value.toMap().value("id") == record.value("id") &&
               value.toMap().value("path").toString().isEmpty();
    });
    if (keep)
        rows.append(QVariantMap{{"id", record.value("id")},
                                {"name", record.value("name")},
                                {"origin", record.value("origin")}});
    catalog["retained"] = rows;
    return write(catalog);
}
QString PluginInstallationStore::recoverDataMove() const
{
    QFile file(m_paths.state("data-move.json"));
    if (!file.exists())
        return {};
    if (!file.open(QIODevice::ReadOnly) || file.size() > 256 * 1024)
        return "Cannot read retained-data transaction";
    const auto journal = QJsonDocument::fromJson(file.readAll()).object();
    file.close();
    const auto id = journal.value("id").toString(), token = journal.value("token").toString();
    if (!PluginPaths::validId(id) || !validToken(token))
        return "Invalid retained-data transaction";
    QString error;
    const auto catalog = read(&error);
    if (!error.isEmpty())
        return error;
    if (catalog.value("dataMove") != token) {
        const auto moves = journal.value("moves").toArray();
        const auto safe = [&](const QString &p) {
            return p == id || (p.startsWith(".retained/" + id + '/') &&
                               p == ".retained/" + id + '/' + QFileInfo(p).fileName() &&
                               validToken(QFileInfo(p).fileName()));
        };
        for (int i = moves.size() - 1; i >= 0; --i) {
            const auto move = moves[i].toObject();
            const auto from = move.value("from").toString(), to = move.value("to").toString();
            if (!safe(from) || !safe(to))
                return "Invalid retained-data boundary";
            const auto source = m_paths.root() + "/.data/" + from,
                       target = m_paths.root() + "/.data/" + to;
            if (QFileInfo(source).isSymLink() || QFileInfo(target).isSymLink())
                return "Retained data cannot be redirected";
            if (!QDir(source).exists() && QDir(target).exists()) {
                if (!QDir().mkpath(QFileInfo(source).absolutePath()) ||
                    !QDir().rename(target, source))
                    return "Cannot recover retained plugin data";
            }
        }
    }
    return QFile::remove(file.fileName()) ? QString() : "Cannot finish retained-data recovery";
}
QString PluginInstallationStore::prepareData(const QString &id, const QVariantMap &origin) const
{
    auto error = recoverDataMove();
    if (!error.isEmpty())
        return error;
    auto catalog = read(&error);
    if (!error.isEmpty() || !PluginPaths::validId(id))
        return error.isEmpty() ? "Invalid plugin ID" : error;
    auto rows = catalog.value("retained").toList();
    QJsonArray moves;
    bool available = !QDir(m_paths.data(id)).exists();
    for (auto &value : rows) {
        auto r = value.toMap();
        if (r.value("id") != id || !r.value("path").toString().isEmpty())
            continue;
        if (PluginPaths::sourceIdentity(r.value("origin").toMap()) !=
            PluginPaths::sourceIdentity(origin)) {
            const auto target =
                ".retained/" + id + '/' + QUuid::createUuid().toString(QUuid::WithoutBraces);
            if (!available)
                moves.append(QJsonObject{{"from", id}, {"to", target}});
            r["path"] = target;
            value = r;
            available = true;
        }
    }
    if (available)
        for (auto &value : rows) {
            auto r = value.toMap();
            const auto from = r.value("path").toString();
            if (r.value("id") != id || from.isEmpty() ||
                PluginPaths::sourceIdentity(r.value("origin").toMap()) !=
                    PluginPaths::sourceIdentity(origin))
                continue;
            if (from != ".retained/" + id + '/' + QFileInfo(from).fileName() ||
                !validToken(QFileInfo(from).fileName()))
                return "Invalid retained data path";
            if (QDir(m_paths.root() + "/.data/" + from).exists()) {
                moves.append(QJsonObject{{"from", from}, {"to", id}});
                r.remove("path");
                value = r;
                break;
            }
        }
    if (moves.isEmpty())
        return {};
    const auto token = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QDir().mkpath(m_paths.state());
    QSaveFile file(m_paths.state("data-move.json"));
    const auto bytes = QJsonDocument(QJsonObject{{"id", id}, {"token", token}, {"moves", moves}})
                           .toJson(QJsonDocument::Compact);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        return "Cannot journal retained plugin data";
    for (const auto &value : moves) {
        const auto move = value.toObject();
        const auto from = m_paths.root() + "/.data/" + move.value("from").toString(),
                   to = m_paths.root() + "/.data/" + move.value("to").toString();
        if (QFileInfo(from).isSymLink() || !QDir().mkpath(QFileInfo(to).absolutePath()) ||
            !QDir().rename(from, to)) {
            recoverDataMove();
            return "Cannot isolate retained plugin data";
        }
    }
    catalog["retained"] = rows;
    catalog["dataMove"] = token;
    error = write(catalog);
    if (!error.isEmpty()) {
        const auto rollback = recoverDataMove();
        return error + (rollback.isEmpty() ? QString() : "; rollback: " + rollback);
    }
    return recoverDataMove();
}
QString PluginInstallationStore::removeRetained(const QString &key) const
{
    QString error;
    auto catalog = read(&error);
    if (!error.isEmpty())
        return error;
    auto rows = catalog.value("retained").toList();
    rows.removeIf([&](const QVariant &value) {
        const auto r = value.toMap();
        return r.value("id").toString() + ':' + r.value("path").toString() == key;
    });
    catalog["retained"] = rows;
    return write(catalog);
}
QString PluginInstallationStore::detach(const QVariantMap &record, bool keepData) const
{
    QString error;
    auto catalog = read(&error);
    if (!error.isEmpty())
        return error;
    auto installed = catalog.value("installations").toList();
    installed.removeIf([&](const QVariant &v) {
        return v.toMap().value("installation") == record.value("installation");
    });
    auto retained = catalog.value("retained").toList();
    retained.removeIf([&](const QVariant &v) {
        return v.toMap().value("id") == record.value("id") &&
               v.toMap().value("path").toString().isEmpty();
    });
    if (keepData)
        retained.append(QVariantMap{{"id", record.value("id")},
                                    {"name", record.value("name")},
                                    {"origin", record.value("origin")}});
    catalog["installations"] = installed;
    catalog["retained"] = retained;
    return write(catalog);
}
} // namespace EchoMusic
