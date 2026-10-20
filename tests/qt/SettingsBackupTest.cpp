#include "SettingsBackupArchive.h"
#include "SettingsBackupRestore.h"
#include "StorageService.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLockFile>
#include <QSettings>
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
        const QVariantMap input{
            {"interface/uiScale", 1.25},
            {"shortcuts/globalEnabled", true},
            {"appearance/preferences", QByteArray("{\"mode\":2}")},
            {"account/session", QByteArray("secret")},
            {"device/id", "private"},
            {"network/localPolicy", QVariantMap{{"passwordId", "sqlite-record"}}},
            {"audio/outputDevice", "/private/device"},
            {"recognition/source", "mic"},
            {"plugins/installations", QVariantList{}}};
        const auto encoded = SettingsBackupArchive::encodePreferences(input);
        QCOMPARE(encoded.size(), 5);
        QVERIFY(!QJsonDocument(encoded).toJson().contains("passwordId"));
        QString error;
        const auto decoded = SettingsBackupArchive::decodePreferences(encoded, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(decoded.value("interface/uiScale").toDouble(), 1.25);
        QCOMPARE(decoded.value("appearance/preferences").metaType(),
                 QMetaType::fromType<QByteArray>());
        QVERIFY(decoded.value("shortcuts/globalEnabled").toBool());
        QCOMPARE(decoded.value("recognition/source").toString(), "mic");
        QTemporaryDir directory;
        QSettings preferences(directory.filePath("preferences.ini"), QSettings::IniFormat);
        preferences.setValue(
            "network/localPolicy",
            QVariantMap{{"mode", "fixed_servers"}, {"passwordId", "keep-local-reference"}});
        SettingsBackupArchive::applyPreferences(preferences, decoded);
        QCOMPARE(preferences.value("network/localPolicy").toMap().value("passwordId").toString(),
                 QString("keep-local-reference"));
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
        const auto root = directory.path();
        const auto id = "00000000-0000-0000-0000-000000000001";
        writeFile(root + "/old-package/manifest.json", packageManifest());
        writeFile(root + "/old-package/index.mjs", "export function activate() {}\n");
        writeFile(root + "/plugins/data/" + id + "/value.txt", "before");
        writeFile(root + "/input/plugins/0/package/manifest.json", packageManifest());
        writeFile(root + "/input/plugins/0/package/index.mjs", "export function activate() {}\n");
        writeFile(root + "/input/plugins/0/data/value.txt", "after");
        const QVariantList catalog{QVariantMap{{"root", root + "/old-package"},
                                               {"source", root + "/old-package"},
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
        auto result = SettingsBackupRestore::prepare(root + "/input", manifest, false, true, root,
                                                     catalog, previous);
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        QCOMPARE(readFile(root + "/plugins/data/" + id + "/value.txt"), QByteArray("after"));
        QVERIFY(!result.catalog.first().toMap().value("enabled").toBool());
        QSettings preferences(root + "/preferences.ini", QSettings::IniFormat);
        preferences.setValue("interface/uiScale", 1.5);
        preferences.setValue("shortcuts/globalEnabled", true);
        preferences.setValue("plugins/installations", result.catalog);
        preferences.sync();
        // Simulate a process dying between filesystem and durable settings commit.
        QVERIFY2(SettingsBackupRestore::recover(root, preferences).isEmpty(), "recovery failed");
        QCOMPARE(readFile(root + "/plugins/data/" + id + "/value.txt"), QByteArray("before"));
        QCOMPARE(preferences.value("interface/uiScale").toDouble(), 1.0);
        QVERIFY(!preferences.contains("shortcuts/globalEnabled"));
        QCOMPARE(preferences.value("plugins/installations").toList(), catalog);
        QVERIFY(!QFile::exists(root + "/settings-restore.json"));
    }
    void clearRemovesLocalCredentialDatabases()
    {
        QTemporaryDir directory;
        const auto root = directory.path();
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
