#include "SettingsStore.h"
#include "PortableSettingsCodec.h"
#include "StorageSnapshot.h"
#include "StorageFence.h"
#include "SettingsBackupArchive.h"
#include "SettingsBackupRestore.h"
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QTemporaryDir>
#include <QStandardPaths>
#include <QtTest>
#include <future>
#include <thread>
using namespace EchoMusic;
namespace
{
void writeFile(const QString &name, const QByteArray &bytes)
{
    QVERIFY(QDir().mkpath(QFileInfo(name).absolutePath()));
    QFile file(name);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(bytes), bytes.size());
}
QByteArray readFile(const QString &name)
{
    QFile file(name);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}
} // namespace
class SettingsStorageTest : public QObject
{
    Q_OBJECT
private slots:
    void defaultStorageBelongsToTheApplicationLocalProfile()
    {
        SettingsStore store;
        QCOMPARE(store.fileName(), canonicalStoragePath(QStandardPaths::writableLocation(
                                                            QStandardPaths::AppLocalDataLocation) +
                                                        "/portable/settings.ini"));
        QCOMPARE(
            store.deviceFileName(),
            QDir::cleanPath(QFileInfo(store.fileName()).dir().filePath("../local/settings.ini")));
    }
    void domainRoutingAutomaticCoverageAndIsolation()
    {
        QTemporaryDir root;
        SettingsStore store(root.path() + "/new/profile/portable/settings.ini");
        store.setValue("interface/futurePreference",
                       QVariantMap{{"nested", QVariantList{42, true, "hello"}}});
        store.setValue("floating/desktop/fontSize", 24);
        store.setValue("floating/desktop/geometry", QByteArray("local geometry"));
        store.setValue("network/deviceUuid", "local-device");
        store.setValue("network/proxyPolicy", QVariantMap{{"passwordId", "local-reference"}});
        store.sync();
        QCOMPARE(store.status(), SettingsStore::NoError);
        QVERIFY(QFileInfo::exists(store.fileName()));
        QVERIFY(QFileInfo::exists(store.deviceFileName()));
        QVERIFY(!readFile(store.fileName()).contains("local-reference"));
        QVERIFY(!readFile(store.fileName()).contains("local-device"));
        SettingsStore reopened(store.fileName());
        QString error;
        const auto portable = reopened.portableValues(&error);
        QVERIFY(error.isEmpty());
        QVERIFY(portable.contains("interface/futurePreference"));
        QVERIFY(portable.contains("floating/desktop/fontSize"));
        QVERIFY(!portable.contains("floating/desktop/geometry"));
        const auto encoded = PortableSettingsCodec::encode(portable, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        const auto decoded = PortableSettingsCodec::decode(encoded, &error);
        QVERIFY(error.isEmpty());
        QCOMPARE(decoded.value("interface/futurePreference"),
                 portable.value("interface/futurePreference"));
        QCOMPARE(reopened.value("network/deviceUuid").toString(), "local-device");
        reopened.setValue("forgottenDomain/newField", true);
        QCOMPARE(reopened.status(), SettingsStore::FormatError);
        QVERIFY(!reopened.contains("forgottenDomain/newField"));
        QVERIFY(
            PortableSettingsCodec::encode({{"network/deviceUuid", "secret"}}, &error).isEmpty());
        QVERIFY(!error.isEmpty());
    }
    void invalidPortableValuesSurviveBackupRestore()
    {
        QTemporaryDir root;
        const auto sourceFile = root.filePath("source/portable/settings.ini");
        {
            SettingsStore source(sourceFile);
            source.setValue("shell/titlebar/order", QVariant{});
            source.setValue("sidebar/hiddenEntries", QVariant{});
            source.setValue("sidebar/hiddenGroups", QVariant{});
            source.setValue("sidebar/pluginCollapsed", QVariant{});
            source.setValue("interface/futurePreference",
                            QVariantMap{{"empty", QVariant{}},
                                        {"null", QVariant::fromValue(nullptr)},
                                        {"items", QVariantList{QVariant{}, 7}}});
            source.setValue("network/deviceUuid", "source-device");
            source.sync();
            QCOMPARE(source.status(), SettingsStore::NoError);
        }
        SettingsStore source(sourceFile);
        QString error;
        const auto values = source.portableValues(&error);
        QVERIFY(error.isEmpty());
        QVERIFY(values.contains("sidebar/hiddenEntries"));
        QVERIFY(!values.value("sidebar/hiddenEntries").isValid());
        const auto preferences = SettingsBackupArchive::encodePreferences(values, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(preferences.value("sidebar/hiddenEntries").toObject().value("type").toString(),
                 "invalid");
        std::atomic_bool cancel = false;
        const auto archive = SettingsBackupArchive::create(
            root.filePath("settings.backup"), root.filePath("capture"), preferences, true, {},
            false, {}, {}, "0.1.3", cancel);
        QVERIFY2(archive.error.isEmpty(), qPrintable(archive.error));
        const auto inspection = SettingsBackupArchive::inspect(root.filePath("settings.backup"),
                                                               root.filePath("inspection"), cancel);
        QVERIFY2(inspection.error.isEmpty(), qPrintable(inspection.error));
        const auto decoded = SettingsBackupArchive::decodePreferences(
            inspection.manifest.value("settings").toObject(), &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(decoded.size(), values.size());
        QVERIFY(decoded.contains("sidebar/hiddenEntries"));
        QVERIFY(!decoded.value("sidebar/hiddenEntries").isValid());
        const auto nested = decoded.value("interface/futurePreference").toMap();
        QVERIFY(nested.contains("empty") && !nested.value("empty").isValid());
        QCOMPARE(nested.value("null").metaType(), QMetaType::fromType<std::nullptr_t>());
        const auto items = nested.value("items").toList();
        QCOMPARE(items.size(), 2);
        QVERIFY(!items.first().isValid());
        QCOMPARE(items.last().toInt(), 7);
        const auto targetFile = root.filePath("target/portable/settings.ini");
        {
            SettingsStore target(targetFile);
            target.setValue("network/deviceUuid", "target-device");
            SettingsBackupArchive::applyPreferences(target, decoded);
            target.sync();
            QCOMPARE(target.status(), SettingsStore::NoError);
        }
        SettingsStore target(targetFile);
        QVERIFY(target.contains("sidebar/hiddenEntries"));
        QVERIFY(!target.value("sidebar/hiddenEntries").isValid());
        QCOMPARE(target.value("network/deviceUuid").toString(), "target-device");
    }
    void unsupportedValuesAndTamperedPortableStoreFailExplicitly()
    {
        QTemporaryDir root;
        SettingsStore store(root.path() + "/portable/settings.ini");
        store.setValue("interface/futurePreference", QPoint(1, 2));
        QString error;
        QVERIFY(PortableSettingsCodec::encode(store.portableValues(), &error).isEmpty());
        QVERIFY(error.contains("serializer"));
        QSettings raw(store.fileName(), QSettings::IniFormat);
        raw.setValue("network/deviceUuid", "must not export");
        raw.sync();
        store.sync();
        QVERIFY(store.portableValues(&error).isEmpty());
        QVERIFY(!error.isEmpty());
    }
    void arbitraryNamedSqliteUsesConsistentSnapshot()
    {
        QTemporaryDir root;
        const auto data = root.path() + "/data", file = data + "/future/nested/state.blob";
        QVERIFY(QDir().mkpath(QFileInfo(file).absolutePath()));
        const QString connection = "snapshot-arbitrary-source";
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", connection);
            db.setDatabaseName(file);
            QVERIFY(db.open());
            QSqlQuery q(db);
            QVERIFY(q.exec("PRAGMA journal_mode=WAL"));
            QVERIFY(q.exec("CREATE TABLE sample(value INTEGER)"));
            QVERIFY(q.exec("INSERT INTO sample VALUES(41)"));
            QVERIFY(StorageSnapshot::claimSqlite(file).isEmpty());
            QVERIFY(q.exec("BEGIN IMMEDIATE"));
            QVERIFY(q.exec("INSERT INTO sample VALUES(99)"));
            writeFile(data + "/ordinary.sqlite", "an ordinary file, not a database");
            writeFile(data + "/ordinary-wal", "keep this unrelated file");
            const auto error = StorageSnapshot::capture(data, root.path() + "/snapshot");
            QVERIFY2(error.isEmpty(), qPrintable(error));
            QVERIFY(q.exec("ROLLBACK"));
            db.close();
        }
        QSqlDatabase::removeDatabase(connection);
        QCOMPARE(readFile(root.path() + "/snapshot/ordinary.sqlite"),
                 QByteArray("an ordinary file, not a database"));
        QCOMPARE(readFile(root.path() + "/snapshot/ordinary-wal"),
                 QByteArray("keep this unrelated file"));
        QVERIFY(!QFileInfo::exists(root.path() + "/snapshot/future/nested/state.blob-wal"));
        const QString readConnection = "snapshot-arbitrary-read";
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", readConnection);
            db.setDatabaseName(root.path() + "/snapshot/future/nested/state.blob");
            QVERIFY(db.open());
            QSqlQuery q(db);
            QVERIFY(q.exec("SELECT COUNT(*),SUM(value) FROM sample"));
            QVERIFY(q.next());
            QCOMPARE(q.value(0).toInt(), 1);
            QCOMPARE(q.value(1).toInt(), 41);
            db.close();
        }
        QSqlDatabase::removeDatabase(readConnection);
        QVERIFY(StorageSnapshot::restore(data, root.path() + "/snapshot").isEmpty());
    }
    void corruptOwnedDatabaseCannotBecomeAnOrdinaryBackupFile()
    {
        QTemporaryDir root;
        const auto file = root.path() + "/data/named-without-extension";
        writeFile(file, "damaged database header");
        QVERIFY(StorageSnapshot::claimSqlite(file).isEmpty());
        QVERIFY(
            !StorageSnapshot::capture(root.path() + "/data", root.path() + "/snapshot").isEmpty());
        QVERIFY(!QDir(root.path() + "/snapshot").exists());
        QVERIFY(StorageSnapshot::forgetSqlite(file));
        QVERIFY(QFile::remove(file));
        QVERIFY(
            StorageSnapshot::capture(root.path() + "/data", root.path() + "/snapshot").isEmpty());
    }
    void interruptedDatabaseDeletionDoesNotBreakFutureSnapshots()
    {
        QTemporaryDir root;
        const auto data = root.path() + "/data", file = data + "/removed-database";
        writeFile(file, "owned file");
        QVERIFY(StorageSnapshot::claimSqlite(file).isEmpty());
        QVERIFY(QFile::remove(file));
        writeFile(data + "/.echo-store-not-a-descriptor", "ordinary hidden file");
        QVERIFY(StorageSnapshot::capture(data, root.path() + "/snapshot").isEmpty());
        QCOMPARE(readFile(root.path() + "/snapshot/.echo-store-not-a-descriptor"),
                 QByteArray("ordinary hidden file"));
        QCOMPARE(QDir(root.path() + "/snapshot").entryList(QDir::Files | QDir::Hidden).size(), 1);
        QVERIFY(StorageSnapshot::forgetSqlite(file));
        QVERIFY(StorageSnapshot::forgetSqlite(file));
    }
    void snapshotFenceDrainsAcceptedWritesAndCancels()
    {
        QTemporaryDir root;
        auto writer = StorageFence::write(root.path() + "/files/images");
        QVERIFY(writer);
        auto frozen = StorageFence::freeze(root.path() + "/files");
        QVERIFY(frozen);
        QVERIFY(!StorageFence::write(root.path() + "/files/audio").get());
        QVERIFY(StorageFence::write(root.path() + "/local"));
        std::atomic_bool cancel = false;
        auto future =
            std::async(std::launch::async, [&] { return frozen->waitForWriters(cancel); });
        QCOMPARE(future.wait_for(std::chrono::milliseconds(30)), std::future_status::timeout);
        writer.reset();
        QCOMPARE(future.wait_for(std::chrono::seconds(1)), std::future_status::ready);
        QVERIFY(future.get());
        frozen.reset();
        writer = StorageFence::write(root.path() + "/files");
        QVERIFY(writer);
        frozen = StorageFence::freeze(root.path() + "/files");
        cancel = true;
        QVERIFY(!frozen->waitForWriters(cancel));
        frozen.reset();
        QVERIFY(StorageFence::write(root.path() + "/files/images"));
    }
    void recoveryRejectsReplacedStorageAncestors()
    {
        QTemporaryDir root;
        const auto profile = canonicalStoragePath(root.path());
        const QString transaction = ".settings-restore-00000000000000000000000000000001";
        writeFile(profile + '/' + transaction + "/previous-files/value.txt", "before");
        writeFile(profile + "/portable", "unexpected file in place of directory");
        const QJsonObject journal{
            {"transaction", transaction}, {"filesPath", "portable/files"}, {"hadFiles", true}};
        QVERIFY(!SettingsBackupRestore::rollbackFiles(profile, journal).isEmpty());
        QCOMPARE(readFile(profile + "/portable"),
                 QByteArray("unexpected file in place of directory"));
        QVERIFY(QFileInfo::exists(profile + '/' + transaction + "/previous-files/value.txt"));
    }
    void ownedFilesReferencesRestoreAndRollbackAcrossDirectories()
    {
        QTemporaryDir root;
        const auto sourceRoot = root.path() + "/source", targetRoot = root.path() + "/target";
        SettingsStore source(sourceRoot + "/portable/settings.ini");
        const auto asset =
            source.files("new-owner", SettingsDomain::Portable) + "/arbitrary-resource.bin";
        writeFile(asset, "portable bytes");
        source.setValue(
            "interface/newFeature",
            QVariantMap{{"file", asset}, {"fileUrl", QUrl::fromLocalFile(asset).toString()}});
        QString error;
        const auto preferences = PortableSettingsCodec::encode(source.portableValues(), &error,
                                                               source.portableFilesRoot());
        QVERIFY(error.isEmpty());
        std::atomic_bool cancel = false;
        const auto archive = SettingsBackupArchive::create(
            root.path() + "/snapshot.backup", root.path() + "/capture", preferences, true, {},
            false, {}, source.portableFilesRoot(), "0.1.0", cancel);
        QVERIFY2(archive.error.isEmpty(), qPrintable(archive.error));
        const auto inspection = SettingsBackupArchive::inspect(root.path() + "/snapshot.backup",
                                                               root.path() + "/inspection", cancel);
        QVERIFY2(inspection.error.isEmpty(), qPrintable(inspection.error));
        QVERIFY(!QJsonDocument(inspection.manifest).toJson().contains(sourceRoot.toUtf8()));
        SettingsStore target(targetRoot + "/portable/settings.ini");
        target.setValue("interface/newFeature", QString("before"));
        target.setValue("network/deviceUuid", "keep this device");
        target.sync();
        writeFile(target.portableFilesRoot() + "/original.txt", "before");
        const QJsonObject previous{
            {"values", PortableSettingsCodec::encode(target.portableValues(), &error,
                                                     target.portableFilesRoot())},
            {"absent", QJsonArray{}}};
        auto prepared = SettingsBackupRestore::prepare(root.path() + "/inspection",
                                                       inspection.manifest, true, false, targetRoot,
                                                       {}, previous, target.portableFilesRoot());
        QVERIFY2(prepared.error.isEmpty(), qPrintable(prepared.error));
        const auto restored =
            PortableSettingsCodec::decode(preferences, &error, target.portableFilesRoot());
        QVERIFY(error.isEmpty());
        SettingsBackupArchive::applyPreferences(target, restored);
        target.sync();
        const auto relocated =
            target.value("interface/newFeature").toMap().value("file").toString();
        QVERIFY(relocated.startsWith(target.portableFilesRoot() + '/'));
        QCOMPARE(readFile(relocated), QByteArray("portable bytes"));
        QVERIFY(SettingsBackupRestore::recover(targetRoot, target).isEmpty());
        QCOMPARE(target.value("interface/newFeature").toString(), "before");
        QCOMPARE(readFile(target.portableFilesRoot() + "/original.txt"), QByteArray("before"));
        QVERIFY(!QFileInfo::exists(relocated));
        QCOMPARE(target.value("network/deviceUuid").toString(), "keep this device");
        prepared = SettingsBackupRestore::prepare(root.path() + "/inspection", inspection.manifest,
                                                  true, false, targetRoot, {}, previous,
                                                  target.portableFilesRoot());
        QVERIFY(prepared.error.isEmpty());
        SettingsBackupArchive::applyPreferences(target, restored);
        target.sync();
        prepared.journal["committed"] = true;
        QVERIFY(SettingsBackupRestore::writeJournal(targetRoot, prepared.journal));
        QVERIFY(SettingsBackupRestore::recover(targetRoot, target).isEmpty());
        QCOMPARE(readFile(relocated), QByteArray("portable bytes"));
        QCOMPARE(target.value("network/deviceUuid").toString(), "keep this device");
    }
};
QTEST_GUILESS_MAIN(SettingsStorageTest)
#include "SettingsStorageTest.moc"
