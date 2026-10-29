#include "PluginPaths.h"
#include "PluginInstallationStore.h"
#include "PluginInstallTransaction.h"
#include "SettingsKeys.h"
#include "SettingsBackupRestore.h"
#include "SettingsBackupArchive.h"
#include "StorageSnapshot.h"
#include "PluginPackage.h"
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include "SettingsStore.h"
#include <QUuid>
#include <QScopeGuard>
#include <QSet>
namespace EchoMusic
{
namespace
{
QString ownedDirectoryError(const QString &root, const QString &relative)
{
    if (relative.isEmpty() || relative.startsWith('/') || relative.contains('\\') ||
        relative.contains(':') || relative.contains(QChar(0)))
        return "Invalid portable storage path";
    QString checked = canonicalStoragePath(root);
    for (const auto &part : relative.split('/')) {
        if (part.isEmpty() || part == "." || part == "..")
            return "Invalid portable storage path";
        checked += '/' + part;
        const QFileInfo info(checked);
        if (info.isSymLink() || info.isJunction() || (info.exists() && !info.isDir()))
            return "Portable storage is not an owned directory";
    }
    return {};
}
} // namespace
bool SettingsBackupRestore::writeJournal(const QString &root, const QJsonObject &journal)
{
    QSaveFile file(root + "/settings-restore.json");
    const auto bytes = QJsonDocument(journal).toJson(QJsonDocument::Compact);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
    return file.write(bytes) == bytes.size() && file.commit();
}
SettingsRestoreResult
SettingsBackupRestore::prepare(const QString &staging, const QJsonObject &manifest, bool settings,
                               bool plugins, const QString &root, const QVariantList &catalog,
                               const QJsonObject &previous, const QString &filesRoot)
{
    SettingsRestoreResult result;
    result.catalog = catalog;
    const auto transaction = ".settings-restore-" + QUuid::createUuid().toString(QUuid::Id128);
    if (QFile::exists(root + "/settings-restore.json") || !QDir().mkpath(root + '/' + transaction))
        return {"Pending restore must be recovered first", {}, {}};
    auto cleanup = qScopeGuard([&] {
        if (!QFile::exists(root + "/settings-restore.json"))
            QDir(root + '/' + transaction).removeRecursively();
    });
    QJsonArray plans;
    result.journal = {{"includesSettings", settings},
                      {"version", 1},
                      {"transaction", transaction},
                      {"previous", previous},
                      {"oldCatalog", QJsonArray::fromVariantList(catalog)},
                      {"plugins", plans},
                      {"includesPlugins", plugins},
                      {"committed", false}};
    if (!writeJournal(root, result.journal))
        return {"Cannot persist restore journal", {}, {}};
    if (plugins) {
        const auto entries = manifest.value("plugins").toArray();
        QSet<QString> ids;
        for (int i = 0; i < entries.size(); ++i) {
            const auto entry = entries[i].toObject();
            const auto id = entry.value("id").toString();
            const auto prefix = QString(SettingsKeys::Plugins::EntryFormat).arg(i);
            const auto origin = entry.value("origin").toObject().toVariantMap();
            if (entry.value("prefix") != prefix || !PluginPaths::validId(id) || ids.contains(id) ||
                (origin.value("kind") != "local" && origin.value("kind") != "marketplace"))
                return {"Invalid backup plugin path or identity", {}, result.journal};
            ids.insert(id);
            int existingIndex = -1;
            PluginPackage previousPackage;
            for (int j = 0; j < result.catalog.size(); ++j) {
                const auto existing = result.catalog[j].toMap();
                if (existing.value("id") != id)
                    continue;
                if (PluginPaths::sourceIdentity(existing.value("origin").toMap()) !=
                    PluginPaths::sourceIdentity(origin))
                    return {
                        "Backup plugin source differs from installed identity", {}, result.journal};
                existingIndex = j;
                previousPackage.root = PluginPaths(root + "/plugins").code(id);
                break;
            }
            const auto installation = QUuid::createUuid().toString(QUuid::WithoutBraces);
            const PluginPaths paths(root + "/plugins");
            const auto package = PluginPackage::importDirectory(staging + '/' + prefix + "/package",
                                                                paths.images(), installation);
            if (!package.error.isEmpty() || package.id != id)
                return {package.error.isEmpty() ? "Backup plugin identity mismatch" : package.error,
                        {},
                        result.journal};
            const auto installed =
                PluginInstallTransaction::begin(paths.root(), package, previousPackage);
            if (!installed.second.isEmpty())
                return {installed.second, {}, result.journal};
            const auto token = QFileInfo(installed.first).fileName();
            plans.append(QJsonObject{{"id", id}, {"transaction", token}});
            result.journal["plugins"] = plans;
            if (!writeJournal(root, result.journal))
                return {"Cannot persist restore plan", {}, result.journal};
            const auto error =
                StorageSnapshot::restore(paths.data(id), staging + '/' + prefix + "/data");
            if (!error.isEmpty())
                return {error, {}, result.journal};
            QVariantMap record{{"id", id},
                               {"source", paths.code(id)},
                               {"root", paths.code(id)},
                               {"installation", installation},
                               {"origin", origin},
                               {"transaction", token},
                               {"contentHash", package.contentHash},
                               {"enabled", entry.value("enabled").toBool()}};
            if (existingIndex >= 0)
                result.catalog[existingIndex] = record;
            else
                result.catalog.append(record);
        }
    }
    if (settings) {
        const auto target =
            filesRoot.isEmpty() ? canonicalStoragePath(root) + "/portable/files" : filesRoot;
        const auto relative = QDir(canonicalStoragePath(root)).relativeFilePath(target);
        result.error = ownedDirectoryError(root, relative);
        if (!result.error.isEmpty())
            return result;
        const bool hadFiles = QDir(target).exists();
        const auto snapshot =
            StorageSnapshot::capture(target, root + '/' + transaction + "/previous-files");
        if (!snapshot.isEmpty()) {
            result.error = snapshot;
            return result;
        }
        result.journal["filesPath"] = relative;
        result.journal["hadFiles"] = hadFiles;
        if (!writeJournal(root, result.journal)) {
            result.error = "Cannot journal portable file restore";
            return result;
        }
        result.error = StorageSnapshot::restore(target, staging + "/files");
        if (!result.error.isEmpty())
            return result;
    }
    return result;
}

QString SettingsBackupRestore::rollbackFiles(const QString &root, const QJsonObject &journal)
{
    const auto transaction = journal.value("transaction").toString();
    static const QRegularExpression directory("^\\.settings-restore-[a-f0-9]{32}$");
    if (!directory.match(transaction).hasMatch())
        return "Invalid restore transaction";
    const auto filesPath = journal.value("filesPath").toString();
    if (!filesPath.isEmpty()) {
        if (const auto error = ownedDirectoryError(root, filesPath); !error.isEmpty())
            return error;
        const auto target = root + '/' + filesPath;
        const auto error =
            StorageSnapshot::restore(target, root + '/' + transaction + "/previous-files");
        if (!error.isEmpty())
            return error;
        if (!journal.value("hadFiles").toBool() && !QDir(target).removeRecursively())
            return "Cannot remove newly restored portable files";
    }
    if (journal.value("includesPlugins").toBool()) {
        const auto error = PluginInstallationStore(root + "/plugins")
                               .save(journal.value("oldCatalog").toArray().toVariantList());
        if (!error.isEmpty())
            return error;
        return PluginInstallTransaction::recover(root + "/plugins");
    }

    return {};
}
QString SettingsBackupRestore::restorePreferences(EchoMusic::SettingsStore &settings,
                                                  const QJsonObject &journal)
{
    if (!journal.value("includesSettings").toBool())
        return {};
    const auto previous = journal.value("previous").toObject();
    QString error;
    const auto values = SettingsBackupArchive::decodePreferences(
        previous.value("values").toObject(), &error, settings.portableFilesRoot());
    if (!error.isEmpty())
        return error;
    SettingsBackupArchive::applyPreferences(settings, values);
    settings.sync();
    return settings.status() == EchoMusic::SettingsStore::NoError
               ? QString()
               : "Cannot restore application preferences";
}
void SettingsBackupRestore::discard(const QString &root, const QJsonObject &journal)
{
    static const QRegularExpression directory("^\\.settings-restore-[a-f0-9]{32}$");
    const auto transaction = journal.value("transaction").toString();
    if (directory.match(transaction).hasMatch())
        QDir(root + '/' + transaction).removeRecursively();
    QFile::remove(root + "/settings-restore.json");
}
QString SettingsBackupRestore::recover(const QString &root, EchoMusic::SettingsStore &settings)
{
    QFile file(root + "/settings-restore.json");
    if (!file.exists())
        return {};
    if (!file.open(QIODevice::ReadOnly) || file.size() > 4 * 1024 * 1024)
        return "Cannot read pending settings restore";
    const auto journal = QJsonDocument::fromJson(file.readAll()).object();
    // discard() removes this path through another QFile; an open reader prevents
    // that removal on Windows, leaving a stale journal after a successful recovery.
    file.close();
    if (journal.value("version").toInt() != 1)
        return "Invalid settings restore journal";
    if (!journal.value("committed").toBool()) {
        const auto error = rollbackFiles(root, journal);
        if (!error.isEmpty())
            return error;
        if (const auto failure = restorePreferences(settings, journal); !failure.isEmpty())
            return failure;
        if (settings.status() != EchoMusic::SettingsStore::NoError)
            return "Cannot roll back application preferences";
    }
    if (const auto error = PluginInstallTransaction::recover(root + "/plugins"); !error.isEmpty())
        return error;
    discard(root, journal);
    return {};
}
QString SettingsBackupRestore::clearManagedFiles(const QString &root, const QString &instanceLock)
{
    const auto lockPath = QFileInfo(instanceLock).absoluteFilePath();
    QDir directory(root);
    for (const auto &entry :
         directory.entryInfoList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot)) {
        if (entry.fileName() == "logs" || entry.absoluteFilePath() == lockPath)
            continue;
        const bool removed = entry.isDir() && !entry.isSymLink()
                                 ? QDir(entry.filePath()).removeRecursively()
                                 : QFile::remove(entry.filePath());
        if (!removed)
            return QString("Cannot remove application data: ") + entry.fileName();
    }
    return {};
}
} // namespace EchoMusic
