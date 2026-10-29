#include "ExperimentalSettingsService.h"
#include "StorageService.h"
#include <QClipboard>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace EchoMusic;
class ExperimentalSettingsTest : public QObject
{
    Q_OBJECT
private slots:
    void explicitInspectionAndReset()
    {
        QTemporaryDir directory;
        EchoMusic::SettingsStore settings(directory.filePath("settings.ini"));
        auto device = DeviceIdentity::load(settings);
        device.dfid = "synthetic-dfid";
        QVERIFY(DeviceIdentity::saveRegistration(settings, device));
        settings.setValue("interface/keepAlive", false);
        settings.setValue("network/proxyPolicy", QVariantMap{{"mode", "direct"}});
        ApiClient api(device);
        AccountService account(api);
        StorageService storage;
        SavedAccountModel saved(api, account, storage);
        RelaunchService relaunch({}, {}, false);
        ExperimentalSettingsService service(api, account, saved, settings, relaunch);
        QSignalSpy notices(&service, &ExperimentalSettingsService::notice);
        QSignalSpy readyToQuit(&relaunch, &RelaunchService::readyToQuit);
        storage.initialize(directory.filePath("state.sqlite"));
        QTRY_VERIFY(saved.ready());
        const auto session = QByteArrayLiteral(
            "{\"user_id\":42,\"token\":\"synthetic-token\",\"refresh_credential\":\"synthetic-t1\","
            "\"nickname\":\"实验室测试账号\"}");
        QVERIFY(account.adoptLoginAuthorization(session));
        QTRY_VERIFY(!saved.busy());
        const auto entries = service.userInfo();
        QMap<QString, QString> values;
        for (const auto &value : entries) {
            const auto entry = value.toMap();
            values.insert(entry.value("label").toString(), entry.value("value").toString());
        }
        QCOMPARE(values.value("token"), QString("synthetic-token"));
        QCOMPARE(values.value("t1"), QString("synthetic-t1"));
        QCOMPARE(values.value("guid"), device.guid);
        QCOMPARE(values.value("dev"), QString("EchoMusicNext"));
        auto *clipboard = QGuiApplication::clipboard();
        clipboard->setText("untouched-until-copy");
        service.userInfo();
        QCOMPARE(clipboard->text(), QString("untouched-until-copy"));
        service.copyAuthorization();
        QCOMPARE(
            clipboard->text(),
            QString("token=synthetic-token;userid=42;t1=synthetic-t1;dfid=synthetic-dfid;KUGOU_API_"
                    "MID=%1;uuid=%2;KUGOU_API_GUID=%3;KUGOU_API_DEV=EchoMusicNext;KUGOU_API_MAC=%4")
                .arg(device.mid, device.uuid, device.guid, device.mac));
        clipboard->clear();
        QCOMPARE(notices.size(), 1);
        service.setMaintenanceGuard([] { return false; });
        service.resetDeviceIdentity();
        QCOMPARE(account.userId(), quint64(42));
        QVERIFY(!service.resetPending());
        service.setMaintenanceGuard([] { return true; });
        QSignalSpy accountsReady(&storage, &StorageService::accountsReady);
        StoredAccount other;
        other.userId = 7;
        other.token = "synthetic-other-token";
        storage.saveAccount(999, other, false);
        QTRY_VERIFY(!accountsReady.isEmpty());
        accountsReady.clear();
        service.resetDeviceIdentity();
        QCOMPARE(account.userId(), quint64(0));
        QTRY_VERIFY(!service.busy());
        QVERIFY(service.resetPending());
        QVERIFY(!settings.contains("network/registeredDevice"));
        QVERIFY(!settings.contains("network/deviceGuid"));
        QVERIFY(!accountsReady.isEmpty());
        QCOMPARE(accountsReady.last().at(2).toULongLong(), quint64(0));
        QCOMPARE(qvariant_cast<StoredAccounts>(accountsReady.last().at(1)).size(), 1);
        QCOMPARE(qvariant_cast<StoredAccounts>(accountsReady.last().at(1)).first().userId,
                 quint64(7));
        QCOMPARE(readyToQuit.size(), 0); // Development launch asks for a manual restart.
        QCOMPARE(notices.last().at(0).toString(),
                 QStringLiteral("设备身份已清除，请手动重启应用后再登录"));
        QVERIFY(service.userInfo().isEmpty());
        const auto noticeCount = notices.size();
        service.resetDeviceIdentity();
        QCOMPARE(notices.size(), noticeCount);
        // An already-running registration writer cannot resurrect the old identity.
        QVERIFY(DeviceIdentity::saveRegistration(settings, device));
        EchoMusic::SettingsStore nextLaunch(settings.fileName());
        const auto replacement = DeviceIdentity::load(nextLaunch);
        QVERIFY(replacement.guid != device.guid);
        QVERIFY(replacement.mid != device.mid);
        QCOMPARE(replacement.mid, DeviceIdentity::midForGuid(replacement.guid));
        QCOMPARE(replacement.dfid, QString("-"));
        QVERIFY(!nextLaunch.contains("network/registeredDevice"));
        QVERIFY(!nextLaunch.contains("network/resetDeviceIdentity"));
        QCOMPARE(nextLaunch.value("network/proxyPolicy").toMap().value("mode").toString(),
                 QString("direct"));
        QCOMPARE(nextLaunch.value("interface/keepAlive").toBool(), false);
        QSignalSpy closed(&storage, &StorageService::closed);
        saved.shutdown();
        storage.shutdown();
        QTRY_COMPARE(closed.size(), 1);
        StorageService reopened;
        QSignalSpy reopenedAccounts(&reopened, &StorageService::accountsReady);
        reopened.initialize(directory.filePath("state.sqlite"));
        QTRY_VERIFY(!reopenedAccounts.isEmpty());
        QCOMPARE(reopenedAccounts.last().at(2).toULongLong(), quint64(0));
        QCOMPARE(qvariant_cast<StoredAccounts>(reopenedAccounts.last().at(1)).size(), 1);
        QCOMPARE(qvariant_cast<StoredAccounts>(reopenedAccounts.last().at(1)).first().userId,
                 quint64(7));
        reopened.shutdown();
        api.shutdown();
    }
};
QTEST_MAIN(ExperimentalSettingsTest)
#include "ExperimentalSettingsTest.moc"
