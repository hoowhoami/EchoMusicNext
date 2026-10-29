#include <QFileInfo>
#include "PluginInstallTransaction.h"
#include "PluginPaths.h"
#include "PluginInstallationStore.h"
#include "StorageSnapshot.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QUuid>
#include <QScopeGuard>

namespace EchoMusic
{
namespace
{
QString writeJournal(const QString &directory, const QJsonObject &journal)
{
    QSaveFile file(directory + "/journal.json");
    const auto bytes = QJsonDocument(journal).toJson(QJsonDocument::Compact);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit()
               ? QString()
               : "Cannot commit plugin transaction journal";
}
QJsonObject readJournal(const PluginPaths &paths, const QString &directory, QString &error)
{
    QFile file(directory + "/journal.json");
    if (!file.open(QIODevice::ReadOnly) || file.size() > 256 * 1024) {
        error = "Cannot read plugin transaction";
        return {};
    }
    QJsonParseError parse;
    const auto object = QJsonDocument::fromJson(file.readAll(), &parse).object();
    const auto id = object.value("id").toString();
    const auto token = QFileInfo(directory).fileName();
    if (parse.error != QJsonParseError::NoError || object.value("version").toInt() != 1 ||
        !PluginPaths::validId(id) || !PluginInstallationStore::validToken(token) ||
        QDir(directory).absolutePath() != paths.transactions(id) + '/' + token ||
        QFileInfo(directory).isSymLink() || QFileInfo(paths.transactions(id)).isSymLink()) {
        error = "Invalid plugin transaction boundary";
        return {};
    }
    return object;
}
QString replaceCode(const QString &source, const QString &target, const QString &transaction)
{
    const auto ready = transaction + "/restoring";
    const auto removed = transaction + "/replaced";
    if (!QDir(ready).removeRecursively() || !QDir(removed).removeRecursively())
        return "Cannot prepare code rollback";
    if (!source.isEmpty()) {
        const auto error = PluginPaths::copyTree(source, ready);
        if (!error.isEmpty())
            return error;
    }
    if (QFileInfo::exists(target) && !QDir().rename(target, removed))
        return "Cannot detach plugin code";
    if (!source.isEmpty() && !QDir().rename(ready, target)) {
        QDir().rename(removed, target);
        return "Cannot restore plugin code";
    }
    QDir(removed).removeRecursively();
    return {};
}
} // namespace
QPair<QString, QString> PluginInstallTransaction::begin(const QString &root,
                                                        const PluginPackage &next,
                                                        const PluginPackage &previous)
{
    PluginPaths paths(root);
    if (!PluginPaths::validId(next.id))
        return {{}, "Invalid plugin ID"};
    const auto directory =
        paths.transactions(next.id) + '/' + QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (!QDir().mkpath(directory))
        return {{}, "Cannot create plugin transaction"};
    bool armed = false;
    auto cleanup = qScopeGuard([&] {
        if (!armed)
            QDir(directory).removeRecursively();
    });
    const auto code = paths.code(next.id);
    const bool physicalCode = QFileInfo::exists(code);
    const bool hadCode = physicalCode || !previous.root.isEmpty();
    const bool hadData = QFileInfo::exists(paths.data(next.id));
    auto error = PluginPaths::copyTree(next.root, directory + "/staged");
    if (error.isEmpty() && hadCode)
        error = PluginPaths::copyTree(previous.root.isEmpty() ? code : previous.root,
                                      directory + "/previous");
    if (error.isEmpty())
        error = StorageSnapshot::capture(paths.data(next.id), directory + "/data-snapshot");
    if (!error.isEmpty())
        return {{}, error};
    error = writeJournal(
        directory, {{"version", 1}, {"id", next.id}, {"hadCode", hadCode}, {"hadData", hadData}});
    if (!error.isEmpty())
        return {{}, error};
    // Once armed, every interrupted rename is recovered from immutable backups.
    armed = true;
    if (physicalCode && !QDir().rename(code, directory + "/detached"))
        error = "Cannot detach previous plugin code";
    if (error.isEmpty() && !QDir().rename(directory + "/staged", code))
        error = "Cannot publish plugin code";
    if (!error.isEmpty()) {
        const auto restored = rollback(root, directory);
        if (restored.isEmpty()) {
            discard(directory);
            return {{}, error};
        }
        return {directory, error + "; rollback: " + restored};
    }
    return {directory, {}};
}
QString PluginInstallTransaction::rollback(const QString &root, const QString &transaction)
{
    PluginPaths paths(root);
    QString error;
    const auto journal = readJournal(paths, transaction, error);
    if (!error.isEmpty())
        return error;
    const auto id = journal.value("id").toString();
    if (journal.value("remove").toBool()) {
        if (QDir(transaction + "/previous").exists())
            return replaceCode(transaction + "/previous", paths.code(id), transaction);
        return {};
    }
    error = replaceCode(journal.value("hadCode").toBool() ? transaction + "/previous" : QString(),
                        paths.code(id), transaction);
    if (!error.isEmpty())
        return error;
    error = StorageSnapshot::restore(paths.data(id), transaction + "/data-snapshot");
    if (error.isEmpty() && !journal.value("hadData").toBool() &&
        !QDir(paths.data(id)).removeRecursively())
        return "Cannot restore absent plugin data";
    return error;
}
QPair<QString, QString> PluginInstallTransaction::remove(const QString &root, const QString &id,
                                                         const QString &installation,
                                                         bool removeData)
{
    const PluginPaths paths(root);
    if (!PluginPaths::validId(id) || !PluginInstallationStore::validToken(installation))
        return {{}, "Invalid uninstall identity"};
    const auto directory =
        paths.transactions(id) + '/' + QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (!QDir().mkpath(directory))
        return {{}, "Cannot create uninstall transaction"};
    auto error = writeJournal(directory, {{"version", 1},
                                          {"id", id},
                                          {"remove", true},
                                          {"installation", installation},
                                          {"removeData", removeData}});
    if (error.isEmpty() && QDir(paths.code(id)).exists() &&
        !QDir().rename(paths.code(id), directory + "/previous"))
        error = "Cannot detach uninstalled code";
    if (!error.isEmpty()) {
        discard(directory);
        return {{}, error};
    }
    return {directory, {}};
}
QString PluginInstallTransaction::finishRemoval(const QString &root, const QString &transaction)
{
    const PluginPaths paths(root);
    QString error;
    const auto journal = readJournal(paths, transaction, error);
    if (!error.isEmpty() || !journal.value("remove").toBool())
        return error.isEmpty() ? "Invalid uninstall transaction" : error;
    if (journal.value("removeData").toBool() &&
        !QDir(paths.data(journal.value("id").toString())).removeRecursively())
        return "Cannot remove plugin private data";
    if (!QDir(paths.cache(journal.value("id").toString())).removeRecursively())
        return "Cannot remove plugin private cache";
    return discard(transaction);
}
QString PluginInstallTransaction::discard(const QString &transaction)
{
    return QDir(transaction).removeRecursively() ? QString() : "Cannot clean plugin transaction";
}
QString PluginInstallTransaction::recover(const QString &root, const QString &pluginId)
{
    PluginPaths paths(root);
    QString error;
    error = PluginInstallationStore(root).recoverDataMove();
    if (!error.isEmpty())
        return error;
    const auto records = PluginInstallationStore(root).records(&error);
    if (!error.isEmpty())
        return error;
    for (const auto &plugin :
         QDir(paths.transactions()).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (!pluginId.isEmpty() && plugin.fileName() != pluginId)
            continue;
        if (plugin.isSymLink() || !PluginPaths::validId(plugin.fileName()))
            return "Invalid plugin transaction directory";
        for (const auto &entry :
             QDir(plugin.filePath()).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            const auto dir = entry.filePath();
            if (entry.isSymLink() || !PluginInstallationStore::validToken(entry.fileName()))
                return "Invalid plugin transaction token";
            if (!QFile::exists(dir + "/journal.json")) {
                if (const auto failure = discard(dir); !failure.isEmpty())
                    return failure;
                continue;
            }
            const auto journal = readJournal(paths, dir, error);
            if (!error.isEmpty())
                return error;
            bool committed = false;
            if (journal.value("remove").toBool()) {
                committed = true;
                for (const auto &value : records)
                    if (value.toMap().value("installation") ==
                        journal.value("installation").toString())
                        committed = false;
                if (committed) {
                    const auto failure = finishRemoval(root, dir);
                    if (!failure.isEmpty())
                        return failure;
                    continue;
                }
            }
            for (const auto &value : records) {
                const auto record = value.toMap();
                if (record.value("id") == journal.value("id").toString() &&
                    record.value("transaction") == entry.fileName())
                    committed = true;
            }
            if (!committed)
                error = rollback(root, dir);
            if (!error.isEmpty())
                return error;
            if (const auto failure = discard(dir); !failure.isEmpty())
                return failure;
        }
    }
    return {};
}
} // namespace EchoMusic
