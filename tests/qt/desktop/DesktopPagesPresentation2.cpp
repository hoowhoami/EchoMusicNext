#include "DesktopPagesTest.h"
#include "DesktopPagesTestFixture.h"

void DesktopPagesTest::deviceRemovalRequiresConfirmationRefreshAndCurrentSession()
{
    ApiClient api;
    AccountService accountService(api);
    ProfileService profile(api, accountService);
    QVERIFY(AccountServiceTestAccess::adopt(accountService, account(900001)));
    QSignalSpy notices(&profile, &ProfileService::deviceRemovalFinished);
    const auto device = [](const QString &id, bool current) {
        return QJsonObject{{"id", id},
                           {"current", current},
                           {"model", "EchoMusicNext"},
                           {"platform", "PC"},
                           {"active_at_seconds", qint64(1700000000)},
                           {"app_id", 3116},
                           {"client_version", 11440},
                           {"transport_fingerprint", "domain-fingerprint"}};
    };
    const QJsonArray rows{device("current", true), device("other", false)};
    const auto populate = [&](const QJsonArray &data) {
        const auto read = ProfileServiceTestAccess::deviceRequest(profile, api);
        ProfileServiceTestAccess::complete(profile, read, QJsonDocument(data).toJson());
    };
    populate(rows);
    profile.activateDevices();
    profile.requestDeviceRemoval("current");
    QVERIFY(!profile.pendingDeviceRemoval());
    profile.requestDeviceRemoval("other");
    QVERIFY(profile.pendingDeviceRemoval());
    QCOMPARE(profile.pendingDeviceTitle(), QStringLiteral("EchoMusicNext"));
    QVERIFY(!profile.removingDevice());
    profile.cancelDeviceRemoval();
    QVERIFY(!profile.pendingDeviceRemoval());
    profile.confirmDeviceRemoval();
    QVERIFY(!profile.removingDevice());
    profile.requestDeviceRemoval("other");
    profile.confirmDeviceRemoval();
    const auto first = ProfileServiceTestAccess::deviceKick(profile);
    QVERIFY(first);
    const auto request = ApiClientTestAccess::request(api, first);
    QCOMPARE(request.value("operation").toString(), QStringLiteral("login_device_kick"));
    QCOMPARE(request.value("query").toObject().value("target_mid").toString(),
             QStringLiteral("other"));
    QVERIFY(!ApiClientTestAccess::automaticVerification(api, first));
    profile.confirmDeviceRemoval();
    QCOMPARE(ProfileServiceTestAccess::deviceKick(profile), first);
    ProfileServiceTestAccess::fail(profile, first, "{}");
    QVERIFY(!profile.removingDevice());
    QVERIFY(profile.pendingDeviceRemoval());
    QCOMPARE(profile.devices()->count(), 2);
    QVERIFY(!profile.devicesError().isEmpty());
    profile.confirmDeviceRemoval();
    const auto retry = ProfileServiceTestAccess::deviceKick(profile);
    const auto oldRead = ProfileServiceTestAccess::deviceRequest(profile, api);
    ProfileServiceTestAccess::complete(profile, retry, R"({"removed":true})");
    QVERIFY(profile.removingDevice());
    QVERIFY(profile.pendingDeviceRemoval()); // Keep confirmation busy through the forced read.
    const auto refresh = ProfileServiceTestAccess::deviceRead(profile);
    QVERIFY(refresh && refresh != oldRead);
    ProfileServiceTestAccess::complete(profile, oldRead, QJsonDocument(rows).toJson());
    QCOMPARE(ProfileServiceTestAccess::deviceRead(profile), refresh);
    ProfileServiceTestAccess::complete(profile, refresh,
                                       QJsonDocument(QJsonArray{rows[0]}).toJson());
    QVERIFY(!profile.removingDevice());
    QCOMPARE(profile.devices()->count(), 1);
    QVERIFY(notices.last()[0].toBool());
    populate(rows);
    profile.requestDeviceRemoval("other");
    profile.confirmDeviceRemoval();
    const auto hidden = ProfileServiceTestAccess::deviceKick(profile);
    const auto noticeCount = notices.size();
    profile.deactivateDevices();
    QVERIFY(!profile.pendingDeviceRemoval());
    ProfileServiceTestAccess::fail(profile, hidden, "{}");
    QCOMPARE(notices.size(), noticeCount);
    profile.activateDevices();
    profile.requestDeviceRemoval("other");
    profile.confirmDeviceRemoval();
    ProfileServiceTestAccess::complete(profile, ProfileServiceTestAccess::deviceKick(profile),
                                       R"({"removed":true})");
    ProfileServiceTestAccess::fail(profile, ProfileServiceTestAccess::deviceRead(profile),
                                   R"({"kind":"authentication","code":20018})");
    QVERIFY(!profile.removingDevice());
    QVERIFY(!profile.pendingDeviceRemoval());
    QVERIFY(profile.devicesError().contains(QStringLiteral("重新登录")));
    QCOMPARE(profile.devices()->count(), 2); // Failed refresh never fabricates an empty list.
    profile.requestDeviceRemoval("other");
    profile.confirmDeviceRemoval();
    const auto late = ProfileServiceTestAccess::deviceKick(profile);
    QVERIFY(AccountServiceTestAccess::adopt(accountService, account(900002)));
    ProfileServiceTestAccess::complete(profile, late, R"({"removed":true})");
    QCOMPARE(profile.devices()->count(), 0);
    QVERIFY(!profile.removingDevice());
    QCOMPARE(notices.size(), noticeCount);
}
