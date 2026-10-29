#include "StorageSnapshot.h"
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QUuid>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QSet>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <algorithm>
namespace EchoMusic
{
namespace
{
QString markerName(const QString &name)
{
    return ".echo-store-" +
           QCryptographicHash::hash(name.toUtf8(), QCryptographicHash::Sha256).toHex();
}
QString databasesIn(const QString &root, QSet<QString> &databases, QSet<QString> &ignored)
{
    if (root.isEmpty() || !QDir(root).exists())
        return {};
    QDirIterator files(root, QDir::Files | QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot,
                       QDirIterator::Subdirectories);
    int count = 0;
    qint64 bytes = 0;
    while (files.hasNext()) {
        files.next();
        const auto info = files.fileInfo();
        if (info.isSymLink() || info.isJunction())
            return "Storage snapshot cannot contain links";
        if (++count > 4096 || (bytes += info.isFile() ? info.size() : 0) > 512ll * 1024 * 1024)
            return "Storage exceeds snapshot limits (512 MiB/4096 entries)";
        if (!info.isFile())
            continue;
        QFile file(info.filePath());
        if (!file.open(QIODevice::ReadOnly))
            return "Cannot inspect storage file";
        const auto relative = QDir(root).relativeFilePath(info.filePath());
        const auto marker = info.fileName();
        const bool descriptor = marker.startsWith(".echo-store-") && marker.size() == 76 &&
                                std::all_of(marker.cbegin() + 12, marker.cend(), [](QChar c) {
                                    return (c >= u'0' && c <= u'9') || (c >= u'a' && c <= u'f');
                                });
        if (descriptor) {
            if (file.size() > 4096)
                return "Invalid storage descriptor size";
            const auto doc = QJsonDocument::fromJson(file.readAll());
            const auto record = doc.object();
            const auto name = record.value("file").toString();
            if (!doc.isObject() || record.value("version") != 1 ||
                record.value("kind") != "sqlite" || name.isEmpty() || name.contains('/') ||
                name.contains('\\') || name.contains(':') || name.contains(QChar(0)) ||
                name == "." || name == ".." || markerName(name) != info.fileName())
                return "Invalid owned storage descriptor";
            const auto database = info.dir().filePath(name);
            const QFileInfo databaseInfo(database);
            if (!databaseInfo.exists() && !databaseInfo.isSymLink()) {
                // An interrupted deletion can leave only its descriptor behind.
                ignored.insert(relative);
                continue;
            }
            if (!databaseInfo.isFile() || databaseInfo.isSymLink())
                return "Owned database is unsafe";
            databases.insert(QDir(root).relativeFilePath(database));
        } else if (file.read(16) == QByteArray("SQLite format 3\0", 16))
            databases.insert(relative);
    }
    return {};
}
bool databaseFile(const QString &path, const QSet<QString> &databases)
{
    if (databases.contains(path))
        return true;
    for (const auto suffix :
         {QLatin1StringView("-wal"), QLatin1StringView("-shm"), QLatin1StringView("-journal")})
        if (path.endsWith(suffix) && databases.contains(path.chopped(suffix.size())))
            return true;
    return false;
}
QString snapshotDatabase(const QString &source, const QString &destination)
{
    const auto connection = QUuid::createUuid().toString();
    QString error;
    {
        auto db = QSqlDatabase::addDatabase("QSQLITE", connection);
        db.setDatabaseName(source);
        db.setConnectOptions("QSQLITE_OPEN_READONLY;QSQLITE_BUSY_TIMEOUT=5000");
        if (!db.open())
            error = db.lastError().text();
        else {
            QSqlQuery query(db);
            query.prepare("VACUUM INTO ?");
            query.addBindValue(destination);
            if (!query.exec())
                error = query.lastError().text();
        }
        db.close();
        if (error.isEmpty()) {
            db.setDatabaseName(destination);
            if (!db.open())
                error = db.lastError().text();
            else {
                QSqlQuery query(db);
                if (!query.exec("PRAGMA quick_check") || !query.next() ||
                    query.value(0).toString() != "ok")
                    error = "Plugin database snapshot integrity check failed";
            }
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(connection);
    return error;
}
QString copyTree(const QString &source, const QString &destination,
                 const QSet<QString> &databases = {}, const QSet<QString> &ignored = {})
{
    if (!QDir().mkpath(destination))
        return "Cannot create plugin data snapshot";
    if (source.isEmpty() || !QDir(source).exists())
        return {};
    QDirIterator files(source, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden,
                       QDirIterator::Subdirectories);
    qint64 bytes = 0;
    int count = 0;
    while (files.hasNext()) {
        files.next();
        auto info = files.fileInfo();
        if (info.isSymLink() || info.isJunction())
            return "Plugin data snapshot cannot contain links";
        const auto relative = QDir(source).relativeFilePath(info.filePath());

        const auto target = destination + '/' + QDir(source).relativeFilePath(info.filePath());
        if (info.isDir()) {
            if (!QDir().mkpath(target))
                return "Cannot create snapshot directory";
            continue;
        }
        bytes += info.size();
        if (++count > 4096 || bytes > 512ll * 1024 * 1024)
            return "Plugin data exceeds snapshot limits (512 MiB/4096 files)";
        if (ignored.contains(relative) || databaseFile(relative, databases))
            continue;
        if (!QFile::copy(info.filePath(), target))
            return "Cannot copy plugin private data";
    }
    return {};
}
} // namespace
bool StorageSnapshot::forgetSqlite(const QString &file)
{
    const QFileInfo info(file);
    const auto marker = info.dir().filePath(markerName(info.fileName()));
    return !QFile::exists(marker) || QFile::remove(marker);
}
QString StorageSnapshot::claimSqlite(const QString &file)
{
    const QFileInfo info(file);
    if (info.isSymLink() || !info.isFile())
        return "Cannot register unsafe database storage";
    QSaveFile descriptor(info.dir().filePath(markerName(info.fileName())));
    const auto bytes =
        QJsonDocument(QJsonObject{{"version", 1}, {"kind", "sqlite"}, {"file", info.fileName()}})
            .toJson(QJsonDocument::Compact);
    QFile existing(descriptor.fileName());
    if (existing.exists()) {
        if (!existing.open(QIODevice::ReadOnly) || existing.size() > 4096 ||
            existing.readAll() != bytes)
            return "Invalid owned database descriptor";
        return {};
    }
    descriptor.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
    return descriptor.open(QIODevice::WriteOnly) && descriptor.write(bytes) == bytes.size() &&
                   descriptor.commit()
               ? QString()
               : "Cannot register database storage";
}
QString StorageSnapshot::capture(const QString &data, const QString &backup)
{
    if (backup.isEmpty())
        return "Snapshot destination is required";
    const QFileInfo input(data);
    if (!data.isEmpty() &&
        (input.isSymLink() || input.isJunction() || (input.exists() && !input.isDir())))
        return "Snapshot source is not a regular storage directory";
    if (QDir(backup).exists())
        return "Pending plugin rollback must be recovered first";
    const auto staging = backup + ".staging-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
    QSet<QString> databases, ignored;
    auto error = databasesIn(data, databases, ignored);
    if (error.isEmpty())
        error = copyTree(data, staging, databases, ignored);
    for (const auto &relative : databases) {
        if (!error.isEmpty())
            break;
        error = snapshotDatabase(data + '/' + relative, staging + '/' + relative);
    }
    if (error.isEmpty() && !QDir().rename(staging, backup))
        error = "Cannot publish plugin data snapshot";
    if (!error.isEmpty())
        QDir(staging).removeRecursively();
    return error;
}
QString StorageSnapshot::restore(const QString &data, const QString &backup)
{
    if (data.isEmpty() || backup.isEmpty())
        return "Restore paths are required";
    if (!QDir(backup).exists())
        return "Missing private-data rollback snapshot";
    const auto next = data + ".restore-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
    auto error = copyTree(backup, next);
    if (!error.isEmpty()) {
        QDir(next).removeRecursively();
        return error;
    }
    const auto retired = data + ".failed-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
    const bool hadData = QDir(data).exists();
    if (hadData && !QDir().rename(data, retired)) {
        QDir(next).removeRecursively();
        return "Cannot detach failed plugin data";
    }
    if (!QDir().rename(next, data)) {
        if (hadData)
            QDir().rename(retired, data);
        QDir(next).removeRecursively();
        return "Cannot restore plugin data";
    }
    if (hadData)
        QDir(retired).removeRecursively();
    return {};
}
void StorageSnapshot::discard(const QString &backup)
{
    if (!backup.isEmpty())
        QDir(backup).removeRecursively();
}
} // namespace EchoMusic
