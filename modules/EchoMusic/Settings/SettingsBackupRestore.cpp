#include "SettingsKeys.h"
#include "SettingsBackupRestore.h"
#include "SettingsBackupArchive.h"
#include "PluginDataTransaction.h"
#include "PluginPackage.h"
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QUuid>
#include <QScopeGuard>
namespace EchoMusic
{
namespace
{
bool validInstallation(const QString &id)
{
    static const QRegularExpression uuid(
        "^[a-f0-9]{8}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{12}$");
    return uuid.match(id).hasMatch();
}
QString identity(const QVariantMap &origin)
{
    return origin.value("kind") == "marketplace" ? "marketplace/" + origin.value("id").toString()
                                                 : "local";
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
SettingsRestoreResult SettingsBackupRestore::prepare(const QString &staging,
                                                     const QJsonObject &manifest, bool settings,
                                                     bool plugins, const QString &root,
                                                     const QVariantList &catalog,
                                                     const QJsonObject &previous)
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
    if (plugins) {
        const auto entries = manifest.value("plugins").toArray();
        for (int i = 0; i < entries.size(); ++i) {
            const auto entry = entries[i].toObject();
            const auto prefix = QString(EchoMusic::SettingsKeys::Plugins::EntryFormat).arg(i);
            if (entry.value("prefix").toString() != prefix)
                return {"Invalid backup plugin path", {}, {}};
            const auto origin = entry.value("origin").toObject().toVariantMap();
            if (origin.value("kind") != "local" && origin.value("kind") != "marketplace")
                return {"Invalid plugin source identity", {}, {}};
            QString installation;
            int existingIndex = -1;
            for (int j = 0; j < result.catalog.size(); ++j) {
                const auto existing = result.catalog[j].toMap();
                QFile file(existing.value("root").toString() + "/manifest.json");
                if (!file.open(QIODevice::ReadOnly) || file.size() > 256 * 1024)
                    continue;
                if (PluginPackage::inspectManifest(file.readAll()).id !=
                    entry.value("id").toString())
                    continue;
                if (identity(existing.value("origin").toMap()) != identity(origin))
                    return {"Backup plugin source differs from the installed identity", {}, {}};
                installation = existing.value("installation").toString();
                existingIndex = j;
                break;
            }
            if (installation.isEmpty())
                installation = QUuid::createUuid().toString(QUuid::WithoutBraces);
            if (!validInstallation(installation))
                return {"Invalid plugin installation identity", {}, {}};
            const auto package = PluginPackage::importDirectory(
                staging + '/' + prefix + "/package", root + "/plugins/packages", installation);
            if (!package.error.isEmpty() || package.id != entry.value("id").toString())
                return {package.error.isEmpty() ? "Backup plugin identity mismatch" : package.error,
                        {},
                        {}};
            QVariantMap record{{"source", package.root},
                               {"root", package.root},
                               {"installation", installation},
                               {"origin", origin},
                               {"enabled", entry.value("enabled").toBool()}};
            if (existingIndex >= 0)
                result.catalog[existingIndex] = record;
            else
                result.catalog.append(record);
            plans.append(QJsonObject{{"installation", installation},
                                     {"input", prefix + "/data"},
                                     {"backup", QString::number(i)}});
        }
    }
    result.journal = {{"version", 1},         {"transaction", transaction},
                      {"previous", previous}, {"oldCatalog", QJsonArray::fromVariantList(catalog)},
                      {"plugins", plans},     {"includesPlugins", plugins},
                      {"committed", false}};
    // Capture every previous data directory before publishing a single change.
    for (const auto &value : plans) {
        const auto plan = value.toObject();
        const auto error = PluginDataTransaction::capture(
            root + "/plugins/data/" + plan.value("installation").toString(),
            root + '/' + transaction + '/' + plan.value("backup").toString());
        if (!error.isEmpty())
            return {error, {}, result.journal};
    }
    if (!writeJournal(root, result.journal))
        return {"Cannot persist restore journal", {}, result.journal};
    for (const auto &value : plans) {
        const auto plan = value.toObject();
        const auto error = PluginDataTransaction::restore(
            root + "/plugins/data/" + plan.value("installation").toString(),
            staging + '/' + plan.value("input").toString());
        if (!error.isEmpty()) {
            result.error = error;
            const auto rollback = rollbackFiles(root, result.journal);
            if (!rollback.isEmpty())
                result.error += "; rollback: " + rollback;
            return result;
        }
    }
    if (settings) {
        QDir images(staging + "/theme-images");
        QDir().mkpath(root + "/theme-images");
        static const QRegularExpression name("^[a-f0-9]{64}\\.png$");
        for (const auto &file : images.entryInfoList(QDir::Files | QDir::Hidden)) {
            if (!name.match(file.fileName()).hasMatch() || file.size() > 32 * 1024 * 1024) {
                result.error = "Invalid managed theme asset";
                return result;
            }
            QFile source(file.filePath());
            if (!source.open(QIODevice::ReadOnly)) {
                result.error = source.errorString();
                return result;
            }
            QCryptographicHash hash(QCryptographicHash::Sha256);
            if (!hash.addData(&source) || hash.result().toHex() != file.baseName().toLatin1()) {
                result.error = "Theme asset identity mismatch";
                return result;
            }
            const auto target = root + "/theme-images/" + file.fileName();
            if (!QFile::exists(target) && !QFile::copy(file.filePath(), target)) {
                result.error = "Cannot restore theme asset";
                return result;
            }
        }
    }
    return result;
}
QString SettingsBackupRestore::rollbackFiles(const QString &root, const QJsonObject &journal)
{
    const auto transaction = journal.value("transaction").toString();
    static const QRegularExpression directory("^\\.settings-restore-[a-f0-9]{32}$");
    if (!directory.match(transaction).hasMatch())
        return "Invalid restore transaction";
    for (const auto &value : journal.value("plugins").toArray()) {
        const auto plan = value.toObject();
        const auto id = plan.value("installation").toString();
        bool valid = false;
        const int index = plan.value("backup").toString().toInt(&valid);
        if (!validInstallation(id) || !valid || index < 0 || index > 127)
            return "Invalid restore rollback entry";
        const auto error = PluginDataTransaction::restore(
            root + "/plugins/data/" + id, root + '/' + transaction + '/' + QString::number(index));
        if (!error.isEmpty())
            return error;
    }
    return {};
}
void SettingsBackupRestore::restorePreferences(QSettings &settings, const QJsonObject &journal)
{
    const auto previous = journal.value("previous").toObject();
    QString error;
    const auto values =
        SettingsBackupArchive::decodePreferences(previous.value("values").toObject(), &error);
    SettingsBackupArchive::applyPreferences(settings, values);
    for (const auto &value : previous.value("absent").toArray())
        if (SettingsBackupArchive::portableKey(value.toString()))
            settings.remove(value.toString());
    if (journal.value("includesPlugins").toBool())
        settings.setValue(EchoMusic::SettingsKeys::Plugins::Installations,
                          journal.value("oldCatalog").toArray().toVariantList());
    settings.sync();
}
void SettingsBackupRestore::discard(const QString &root, const QJsonObject &journal)
{
    static const QRegularExpression directory("^\\.settings-restore-[a-f0-9]{32}$");
    const auto transaction = journal.value("transaction").toString();
    if (directory.match(transaction).hasMatch())
        QDir(root + '/' + transaction).removeRecursively();
    QFile::remove(root + "/settings-restore.json");
}
QString SettingsBackupRestore::recover(const QString &root, QSettings &settings)
{
    QFile file(root + "/settings-restore.json");
    if (!file.exists())
        return {};
    if (!file.open(QIODevice::ReadOnly) || file.size() > 4 * 1024 * 1024)
        return "Cannot read pending settings restore";
    const auto journal = QJsonDocument::fromJson(file.readAll()).object();
    if (journal.value("version").toInt() != 1)
        return "Invalid settings restore journal";
    if (!journal.value("committed").toBool()) {
        const auto error = rollbackFiles(root, journal);
        if (!error.isEmpty())
            return error;
        restorePreferences(settings, journal);
        if (settings.status() != QSettings::NoError)
            return "Cannot roll back application preferences";
    }
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
