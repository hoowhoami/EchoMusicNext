#include "StartupController.h"
#include "StorageService.h"
#include "AccountService.h"
#include "SavedAccountModel.h"
#include "ApiClient.h"
#include "DeviceRegistrationService.h"
#include "NetworkPreferences.h"
#include "PlaybackService.h"
#include "AudioEngine.h"
#include <QTemporaryDir>
#include "SettingsStore.h"
#include <QFile>
#include <QDir>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QUuid>
#include <QSignalSpy>
#include <QElapsedTimer>
#include <QtTest>
using namespace EchoMusic;
namespace EchoMusic
{
class DeviceRegistrationServiceTestAccess
{
public:
    static quint64 request(const DeviceRegistrationService &device)
    {
        return device.m_request;
    }
};
} // namespace EchoMusic
namespace
{
DeviceIdentity registeredDevice()
{
    auto value = DeviceIdentity::temporary();
    value.dfid =
        "stored-device-state-fixture"; // Existing domain identity, not a provider response.
    return value;
}
struct Fixture
{
    QTemporaryDir directory;
    EchoMusic::SettingsStore settings{directory.filePath("settings.ini")};
    ApiClient api;
    NetworkPreferences network{settings, api};
    StorageService storage;
    DeviceRegistrationService device{api, settings};
    AccountService account{api};
    SavedAccountModel accounts{api, account, storage};
    PlaybackService playback{api, settings, nullptr, directory.filePath("playback.sqlite"),
                             &network};
    StartupController startup;
    Fixture(bool cachedDevice = true, const QString &suffix = "music.sqlite")
        : api(cachedDevice ? registeredDevice() : DeviceIdentity::temporary()),
          startup(storage, api, device, playback, network, accounts, directory.filePath(suffix))
    {
    }
    bool close()
    {
        startup.shutdown();
        QSignalSpy a(&playback, &PlaybackService::closed),
            b(&device, &DeviceRegistrationService::closed), c(&storage, &StorageService::closed),
            d(&api, &ApiClient::closed);
        playback.shutdown();
        device.shutdown();
        QSignalSpy catalogClosed(&accounts, &SavedAccountModel::closed);
        accounts.shutdown();
        if (catalogClosed.isEmpty() &&
            !QTest::qWaitFor([&] { return !catalogClosed.isEmpty(); }, 15000))
            return false;
        storage.shutdown();
        api.shutdown();
        QElapsedTimer elapsed;
        elapsed.start();
        while ((a.isEmpty() || b.isEmpty() || c.isEmpty() || d.isEmpty()) &&
               elapsed.elapsed() < 15000)
            QTest::qWait(5);
        return !a.isEmpty() && !b.isEmpty() && !c.isEmpty() && !d.isEmpty();
    }
};
bool writeDatabaseVersion(const QString &path, int version)
{
    const auto name = QUuid::createUuid().toString();
    bool ok = false;
    {
        auto db = QSqlDatabase::addDatabase("QSQLITE", name);
        db.setDatabaseName(path);
        if (db.open()) {
            QSqlQuery query(db);
            ok = query.exec(QString("PRAGMA user_version=%1").arg(version));
        }
        db.close();
    }
    QSqlDatabase::removeDatabase(name);
    return ok;
}
bool ready(const QSignalSpy &events)
{
    for (const auto &event : events)
        if (event[0].toJsonObject().value("event") == "ready")
            return true;
    return false;
}
} // namespace
class StartupTest : public QObject
{
    Q_OBJECT
private slots:
    void realNativeAndSqlInitializationEntersOnce()
    {
        Fixture f;
        QSignalSpy entered(&f.startup, &StartupController::enteredMain);
        f.playback.setStartupHeld(true);
        f.startup.start();
        f.startup.start();
        f.startup.retry();
        QTRY_VERIFY_WITH_TIMEOUT(f.startup.entered(), 15000);
        QCOMPARE(f.storage.state(), StorageService::State::Ready);
        QVERIFY(f.playback.engineReady());
        QCOMPARE(f.device.state(), DeviceRegistrationService::State::Ready);
        QCOMPARE(entered.size(), 1);
        f.startup.forceEnter();
        f.startup.retry();
        QCOMPARE(entered.size(), 1);
        QVERIFY(f.close());
        QVERIFY(f.startup.entered()); // Exit must never reconstruct the loading page.
    }
    void realSqlFailureRetriesOnTheSameOwner()
    {
        Fixture f(true, "blocked/music.sqlite");
        QFile obstruction(f.directory.filePath("blocked"));
        QVERIFY(obstruction.open(QIODevice::WriteOnly));
        obstruction.close();
        f.startup.start();
        QTRY_VERIFY(f.startup.failed());
        QVERIFY(!f.startup.canForceEnter());
        f.startup.forceEnter();
        QVERIFY(!f.startup.entered());
        QCOMPARE(f.storage.state(), StorageService::State::Error);
        QVERIFY(obstruction.remove());
        f.startup.retry();
        QTRY_VERIFY_WITH_TIMEOUT(f.startup.entered(), 15000);
        QVERIFY(f.close());
    }
    void failedOpenSqlConnectionIsRetiredBeforeRetry()
    {
        Fixture f;
        const auto path = f.directory.filePath("music.sqlite");
        QVERIFY(writeDatabaseVersion(path, 999));
        f.startup.start();
        QTRY_VERIFY(f.startup.failed());
        QVERIFY(!f.startup.canForceEnter());
        QVERIFY(writeDatabaseVersion(path, 0));
        f.startup.retry();
        QTRY_VERIFY_WITH_TIMEOUT(f.startup.entered(), 15000);
        QVERIFY(f.close());
    }
    void realAudioClaimFailureDestroysAndRecreatesTheEngine()
    {
        AudioEngine occupied;
        QSignalSpy events(&occupied, &AudioEngine::eventReceived);
        QTRY_VERIFY_WITH_TIMEOUT(ready(events), 15000);
        Fixture f;
        f.startup.start();
        QTRY_VERIFY_WITH_TIMEOUT(f.startup.failed(), 15000);
        QVERIFY(f.startup.canForceEnter());
        QVERIFY(!f.playback.engineReady());
        QVERIFY(!f.playback.engineInitializationError().isEmpty());
        QSignalSpy closed(&occupied, &AudioEngine::closed);
        occupied.shutdown();
        QTRY_COMPARE_WITH_TIMEOUT(closed.size(), 1, 15000);
        f.startup.retry();
        QTRY_VERIFY_WITH_TIMEOUT(f.startup.entered(), 15000);
        QVERIFY(f.playback.engineReady());
        QVERIFY(f.playback.engineInitializationError().isEmpty());
        QVERIFY(f.close());
    }
    void realDeviceTransportFailureCanRetryOrContinue()
    {
        Fixture f(false);
        f.network.apply("fixed_servers", {}, "http://127.0.0.1:1", {}, {}, false, {}, 1, 1);
        QTRY_VERIFY(!f.network.busy());
        bool applied = false;
        f.network.afterPendingMediaWork([&] { applied = true; });
        QTRY_VERIFY(applied);
        QList<quint64> requests;
        bool retiredFailureIgnored = false;
        connect(&f.device, &DeviceRegistrationService::changed, this, [&] {
            if (f.device.state() == DeviceRegistrationService::State::Registering) {
                requests << DeviceRegistrationServiceTestAccess::request(f.device);
                if (requests.size() == 2) {
                    f.api.failed(requests[0], -1, {});
                    retiredFailureIgnored =
                        f.device.state() == DeviceRegistrationService::State::Registering &&
                        DeviceRegistrationServiceTestAccess::request(f.device) == requests[1];
                }
            }
        });
        f.startup.start();
        QTRY_VERIFY_WITH_TIMEOUT(f.startup.failed(), 15000);
        QCOMPARE(f.device.state(), DeviceRegistrationService::State::Error);
        QVERIFY(f.startup.canForceEnter());
        QCOMPARE(requests.size(), 1);
        f.startup.retry();
        QTRY_VERIFY_WITH_TIMEOUT(f.startup.failed(), 15000);
        QCOMPARE(requests.size(), 2);
        QVERIFY(requests[0] && requests[0] != requests[1]);
        QVERIFY(retiredFailureIgnored);
        // A late failure for the retired request cannot replace the active result.
        f.api.failed(requests[0], -1, {});
        QVERIFY(f.startup.canForceEnter());
        f.startup.forceEnter();
        QVERIFY(f.startup.entered());
        QCOMPARE(f.device.state(), DeviceRegistrationService::State::Error);
        QVERIFY(f.close());
    }
    void storedAccountWaitsForRegistrationAndRestoresBeforeNormalEntry()
    {
        for (const bool deviceFailure : {false, true}) {
            Fixture f(!deviceFailure);
            {
                StorageService seed;
                QSignalSpy initialized(&seed, &StorageService::initialized);
                QSignalSpy accounts(&seed, &StorageService::accountsReady);
                seed.initialize(f.directory.filePath("music.sqlite"));
                QTRY_COMPARE(initialized.size(), 1);
                seed.saveAccount(1, {42, "Saved", {}, "fixture-token", 1, {}, "fixture-refresh", 0},
                                 true);
                QTRY_COMPARE(accounts.size(), 2);
                QSignalSpy closed(&seed, &StorageService::closed);
                seed.shutdown();
                QTRY_COMPARE(closed.size(), 1);
            }
            f.network.apply("fixed_servers", {}, "http://127.0.0.1:1", {}, {}, false, {}, 1, 1);
            QTRY_VERIFY(!f.network.busy());
            bool applied = false;
            f.network.afterPendingMediaWork([&] { applied = true; });
            QTRY_VERIFY(applied);
            QSignalSpy adopted(&f.account, &AccountService::sessionAdopted);
            bool restoredAtEntry = false;
            connect(&f.startup, &StartupController::enteredMain, this,
                    [&] { restoredAtEntry = f.account.userId() == 42 && !f.accounts.busy(); });
            f.startup.start();
            if (deviceFailure) {
                QTRY_VERIFY(f.startup.failed());
                QCOMPARE(f.device.state(), DeviceRegistrationService::State::Error);
                QCOMPARE(f.accounts.count(), 1);
                QVERIFY(f.accounts.busy());
                QCOMPARE(f.account.userId(), quint64(0));
                QCOMPARE(adopted.size(), 0);
                f.startup.forceEnter();
            } else {
                QTRY_VERIFY(f.startup.entered());
                QVERIFY(restoredAtEntry);
            }
            QTRY_COMPARE(f.account.userId(), quint64(42));
            QTRY_VERIFY(!f.accounts.busy());
            QCOMPARE(adopted.size(), 1);
            QVERIFY(f.startup.entered());
            QVERIFY(f.close());
        }
    }
    void quitAndLateRepliesCannotEnter()
    {
        Fixture f;
        QSignalSpy entered(&f.startup, &StartupController::enteredMain);
        QSignalSpy quit(&f.startup, &StartupController::quitRequested);
        f.startup.start();
        f.startup.requestQuit();
        QCOMPARE(quit.size(), 1);
        QVERIFY(f.close());
        QTest::qWait(250);
        QCOMPARE(entered.size(), 0);
        QVERIFY(f.startup.closing());
    }
    void recoveryFailureNeverConstructsBusinessServices()
    {
        StartupController startup(QStringLiteral("restore transaction is blocked"));
        QSignalSpy retry(&startup, &StartupController::restartApplicationRequested);
        startup.start();
        QVERIFY(startup.failed());
        QVERIFY(!startup.canForceEnter());
        startup.retry();
        QCOMPARE(retry.size(), 1);
        QVERIFY(!startup.entered());
        startup.reportRestartFailure(QStringLiteral("helper launch failed"));
        QVERIFY(startup.failed());
        startup.retry();
        QCOMPARE(retry.size(), 2);
        startup.shutdown();
    }
};
QTEST_MAIN(StartupTest)
#include "StartupTest.moc"
