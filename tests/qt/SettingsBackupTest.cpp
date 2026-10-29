#include "PluginPaths.h"
#include "PluginInstallationStore.h"
#include "SettingsBackupArchive.h"
#include "SettingsBackupRestore.h"
#include "StorageService.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLockFile>
#include "SettingsStore.h"
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest>
using namespace EchoMusic;
namespace
{
void writeFile(const QString &path, const QByteArray &bytes)
{
    QVERIFY(QDir().mkpath(QFileInfo(path).absolutePath()));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(bytes), bytes.size());
}
QByteArray readFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}
QByteArray packageManifest()
{
    return R"({"format":"echomod","schemaVersion":1,"id":"test.backup","name":"Backup test","version":"1.0.0","kind":"qml-js","entry":"index.mjs","contexts":["main"],"requires":{"hostVersion":">=0.1.0 <1.0.0","sdkVersion":">=1.0.0 <2.0.0","features":[]},"capabilities":[]})";
}
} // namespace
class SettingsBackupTest : public QObject
{
    Q_OBJECT
private slots:
    void resetPreservesLiveInstanceLockAndLogs()
    {
        QTemporaryDir directory;
        const auto path = directory.filePath("instance.lock");
        QLockFile owner(path);
        QVERIFY(owner.tryLock());
        writeFile(directory.filePath("logs/current.log"), "diagnostic");
        writeFile(directory.filePath("cache/value"), "remove");
        QVERIFY(SettingsBackupRestore::clearManagedFiles(directory.path(), path).isEmpty());
        QVERIFY(QFile::exists(path));
        QLockFile contender(path);
        QVERIFY(!contender.tryLock());
        QVERIFY(QFile::exists(directory.filePath("logs/current.log")));
        QVERIFY(!QFile::exists(directory.filePath("cache")));
        owner.unlock();
        QVERIFY(contender.tryLock());
    }
    void portablePreferencesExcludeCredentialsAndPreserveTypes()
    {
        QTemporaryDir directory;
        SettingsStore preferences(directory.filePath("preferences.ini"));
        preferences.setValue("interface/uiScale", 1.25);
        preferences.setValue("shortcuts/globalEnabled", true);
        preferences.setValue("appearance/preferences", QByteArray("{\"mode\":2}"));
        preferences.setValue("recognition/source", "mic");
        preferences.setValue("network/timeouts",
                             QVariantMap{{"apiSeconds", 1.25}, {"playerSeconds", 60.}});
        preferences.setValue("audio/outputDevice", "/private/device");
        preferences.setValue("network/proxyPolicy",
                             QVariantMap{{"passwordId", "keep-local-reference"}});
        QString error;
        const auto encoded =
            SettingsBackupArchive::encodePreferences(preferences.portableValues(), &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(encoded.size(), 5);
        QVERIFY(!QJsonDocument(encoded).toJson().contains("passwordId"));
        const auto decoded = SettingsBackupArchive::decodePreferences(encoded, &error);
        QVERIFY(error.isEmpty());
        QCOMPARE(decoded.value("interface/uiScale").toDouble(), 1.25);
        QCOMPARE(decoded.value("appearance/preferences").metaType(),
                 QMetaType::fromType<QByteArray>());
        QVERIFY(decoded.value("shortcuts/globalEnabled").toBool());
        QCOMPARE(decoded.value("recognition/source").toString(), "mic");
        SettingsBackupArchive::applyPreferences(preferences, decoded);
        QCOMPARE(preferences.value("network/proxyPolicy").toMap().value("passwordId").toString(),
                 "keep-local-reference");
        auto invalid = encoded;
        invalid.insert("account/session", QJsonObject{{"type", "string"}, {"value", "bad"}});
        SettingsBackupArchive::decodePreferences(invalid, &error);
        QVERIFY(!error.isEmpty());
    }
    void streamingRoundTripCancellationAndCorruption()
    {
        QTemporaryDir directory;
        const auto base = directory.path();
        std::atomic_bool cancel = false;
        writeFile(base + "/package/manifest.json", packageManifest());
        writeFile(base + "/package/index.mjs", "export function activate() {}\n");
        QFile large(base + "/package/large.bin");
        QVERIFY(large.open(QIODevice::WriteOnly));
        const QByteArray chunk(64 * 1024, 'x');
        for (int i = 0; i < 512; ++i)
            QCOMPARE(large.write(chunk), chunk.size());
        large.close();
        const QVariantList entries{
            QVariantMap{{"id", "test.backup"},
                        {"name", "Backup test"},
                        {"root", base + "/package"},
                        {"installation", "00000000-0000-0000-0000-000000000001"},
                        {"origin", QVariantMap{{"kind", "local"}}},
                        {"enabled", true}}};
        const auto archive = base + "/file.echonext-backup";
        auto result = SettingsBackupArchive::create(archive, base + "/capture", {}, false, entries,
                                                    true, base + "/plugins", {}, "0.1.0", cancel);
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        auto inspected = SettingsBackupArchive::inspect(archive, base + "/inspection", cancel);
        QVERIFY2(inspected.error.isEmpty(), qPrintable(inspected.error));
        QCOMPARE(QFileInfo(base + "/inspection/plugins/0/package/large.bin").size(),
                 32ll * 1024 * 1024);
        QCOMPARE(readFile(base + "/inspection/plugins/0/package/manifest.json"), packageManifest());
        QVERIFY(QDir(base + "/inspection/plugins/0/data").exists());
        cancel = true;
        writeFile(base + "/existing.backup", "keep existing output");
        result =
            SettingsBackupArchive::create(base + "/existing.backup", base + "/cancelled", {}, false,
                                          entries, true, base + "/plugins", {}, "0.1.0", cancel);
        QVERIFY(!result.error.isEmpty());
        QCOMPARE(readFile(base + "/existing.backup"), QByteArray("keep existing output"));
        cancel = false;
        QFile broken(archive);
        QVERIFY(broken.open(QIODevice::ReadWrite));
        QVERIFY(broken.seek(broken.size() / 2));
        QCOMPARE(broken.write("z"), 1);
        broken.close();
        inspected = SettingsBackupArchive::inspect(archive, base + "/broken", cancel);
        QVERIFY(inspected.error.contains("checksum"));
    }
    void postShutdownRestoreCanRollbackAcrossProcessBoundary()
    {
        QTemporaryDir directory;
        const auto root = QFileInfo(directory.path()).canonicalFilePath();
        const auto id = "00000000-0000-0000-0000-000000000001";
        writeFile(root + "/plugins/test.backup/manifest.json", packageManifest());
        writeFile(root + "/plugins/test.backup/index.mjs", "export function activate() {}\n");
        writeFile(root + "/plugins/.data/test.backup" + "/value.txt", "before");
        writeFile(root + "/input/plugins/0/package/manifest.json", packageManifest());
        writeFile(root + "/input/plugins/0/package/index.mjs", "export function activate() {}\n");
        writeFile(root + "/input/plugins/0/data/value.txt", "after");
        const QVariantList catalog{QVariantMap{{"id", "test.backup"},
                                               {"root", root + "/plugins/test.backup"},
                                               {"source", root + "/plugins/test.backup"},
                                               {"installation", id},
                                               {"origin", QVariantMap{{"kind", "local"}}},
                                               {"enabled", true}}};
        const QJsonObject manifest{
            {"plugins", QJsonArray{QJsonObject{{"id", "test.backup"},
                                               {"prefix", "plugins/0"},
                                               {"origin", QJsonObject{{"kind", "local"}}},
                                               {"enabled", false}}}}};
        const auto previousValues =
            SettingsBackupArchive::encodePreferences({{"interface/uiScale", 1.0}});
        const QJsonObject previous{{"values", previousValues},
                                   {"absent", QJsonArray{"shortcuts/globalEnabled"}}};
        QVERIFY(QDir().mkpath(root + "/input/files"));
        auto result = SettingsBackupRestore::prepare(root + "/input", manifest, true, true, root,
                                                     catalog, previous);
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        QCOMPARE(readFile(root + "/plugins/.data/test.backup" + "/value.txt"), QByteArray("after"));
        QVERIFY(!result.catalog.first().toMap().value("enabled").toBool());
        EchoMusic::SettingsStore preferences(root + "/preferences.ini");
        preferences.setValue("interface/uiScale", 1.5);
        preferences.setValue("shortcuts/globalEnabled", true);
        QVERIFY(PluginInstallationStore(root + "/plugins").save(result.catalog).isEmpty());
        preferences.sync();
        // Simulate a process dying between filesystem and durable settings commit.
        QVERIFY2(SettingsBackupRestore::recover(root, preferences).isEmpty(), "recovery failed");
        QCOMPARE(readFile(root + "/plugins/.data/test.backup" + "/value.txt"),
                 QByteArray("before"));
        QCOMPARE(preferences.value("interface/uiScale").toDouble(), 1.0);
        QVERIFY(!preferences.contains("shortcuts/globalEnabled"));
        QCOMPARE(PluginInstallationStore(root + "/plugins").records(), catalog);
        QVERIFY(!QFile::exists(root + "/settings-restore.json"));
        QVERIFY(!QDir(root + '/' + result.journal.value("transaction").toString()).exists());
    }
    void committedRestoreRecoveryKeepsCurrentStateAndRemovesJournal()
    {
        QTemporaryDir directory;
        const auto root = QFileInfo(directory.path()).canonicalFilePath();
        const QString transaction = ".settings-restore-00000000000000000000000000000001";
        writeFile(root + '/' + transaction + "/0/value.txt", "before");
        writeFile(root + "/plugins/.data/test.backup/value.txt", "after");
        const QJsonObject journal{
            {"version", 1},
            {"transaction", transaction},
            {"committed", true},
            {"previous", QJsonObject{{"values", SettingsBackupArchive::encodePreferences(
                                                    {{"interface/uiScale", 1.0}})}}},
            {"plugins",
             QJsonArray{QJsonObject{{"installation", "00000000-0000-0000-0000-000000000001"},
                                    {"backup", "0"}}}}};
        QVERIFY(SettingsBackupRestore::writeJournal(root, journal));
        EchoMusic::SettingsStore preferences(root + "/preferences.ini");
        preferences.setValue("interface/uiScale", 1.5);
        preferences.sync();

        const auto error = SettingsBackupRestore::recover(root, preferences);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(preferences.value("interface/uiScale").toDouble(), 1.5);
        QCOMPARE(readFile(root + "/plugins/.data/test.backup/value.txt"), QByteArray("after"));
        QVERIFY(!QFile::exists(root + "/settings-restore.json"));
        QVERIFY(!QDir(root + '/' + transaction).exists());
        QVERIFY(SettingsBackupRestore::recover(root, preferences).isEmpty());
    }
    void clearRemovesLocalCredentialDatabases()
    {
        QTemporaryDir directory;
        const auto root = QFileInfo(directory.path()).canonicalFilePath();
        for (const auto &name : {"music.sqlite", "music.sqlite-wal", "music.sqlite-shm",
                                 "network.sqlite", "network.sqlite-wal", "network.sqlite-shm"}) {
            QFile file(root + '/' + name);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("test-only-local-data");
        }
        const auto error = SettingsBackupRestore::clearManagedFiles(root, {});
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY(!QFile::exists(root + "/music.sqlite"));
        QVERIFY(!QFile::exists(root + "/network.sqlite"));
        QVERIFY(!QFile::exists(root + "/network.sqlite-wal"));
        QVERIFY(!QFile::exists(root + "/network.sqlite-shm"));
    }
};
QTEST_GUILESS_MAIN(SettingsBackupTest)
#include "SettingsBackupTest.moc"
