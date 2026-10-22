#include "PluginDataTransaction.h"
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QUuid>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
namespace EchoMusic
{
namespace
{
bool managedDatabaseFile(const QString &relative)
{
    if (relative == "values.sqlite" || relative.startsWith("values.sqlite-"))
        return true;
    if (!relative.startsWith("databases/") || relative.mid(10).contains('/'))
        return false;
    return relative.endsWith(".sqlite") || relative.contains(".sqlite-");
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
QString copyTree(const QString &source, const QString &destination, bool skipDatabase = false)
{
    if (!QDir().mkpath(destination))
        return "Cannot create plugin data snapshot";
    if (!QDir(source).exists())
        return {};
    QDirIterator files(source, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden,
                       QDirIterator::Subdirectories);
    qint64 bytes = 0;
    int count = 0;
    while (files.hasNext()) {
        files.next();
        auto info = files.fileInfo();
        if (info.isSymLink())
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
        if (skipDatabase && managedDatabaseFile(relative))
            continue;
        if (!QFile::copy(info.filePath(), target))
            return "Cannot copy plugin private data";
    }
    return {};
}
} // namespace
QString PluginDataTransaction::capture(const QString &data, const QString &backup)
{
    if (QDir(backup).exists())
        return "Pending plugin rollback must be recovered first";
    const auto staging = backup + ".staging-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
    auto error = copyTree(data, staging, true);
    QStringList databases;
    if (QFile::exists(data + "/values.sqlite"))
        databases.append("values.sqlite");
    for (const auto &file : QDir(data + "/databases").entryList({"*.sqlite"}, QDir::Files))
        databases.append("databases/" + file);
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
QString PluginDataTransaction::restore(const QString &data, const QString &backup)
{
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
void PluginDataTransaction::discard(const QString &backup)
{
    QDir(backup).removeRecursively();
}
} // namespace EchoMusic
