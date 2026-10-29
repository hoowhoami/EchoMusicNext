#include "ApiClient.h"
#include "StorageService.h"
#include "AccountService.h"
#include "UserPlaylistModel.h"
#include <QAbstractItemModelTester>
#include "QrLoginService.h"
#include "QrLoginTestAccess.h"
#include "VerificationService.h"
#include "LoginCandidateModel.h"
#include "DeviceRegistrationService.h"
#include "NetworkConfiguration.h"
#include "NetworkPreferences.h"
#include <QBuffer>
#include <QImage>
#include <utility>
#include <limits>
#include <iostream>
#include <iterator>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include "SettingsStore.h"
#include <QtTest>

using namespace EchoMusic;
namespace EchoMusic
{
class UserPlaylistModelTestAccess
{
public:
    static quint64 prepare(UserPlaylistModel &model, ApiClient &api)
    {
        model.m_sessionRevision = api.sessionRevision();
        model.m_request = api.capabilities();
        model.m_state = UserPlaylistModel::State::Loading;
        return model.m_request;
    }
    static void succeed(UserPlaylistModel &model, quint64 id, const QByteArray &payload)
    {
        model.succeed(id, payload);
    }
    static void reset(UserPlaylistModel &model)
    {
        model.resetState();
    }
};
// Control-flow injection only. These events do not serve as live wire fixtures
// or provider acceptance evidence, and do not invoke a CAPTCHA service.
class DeviceRegistrationServiceTestAccess
{
public:
    static quint64 request(const DeviceRegistrationService &service)
    {
        return service.m_request;
    }
    static quint64 prepare(DeviceRegistrationService &service, ApiClient &api)
    {
        service.m_request = api.capabilities();
        service.m_revision = api.sessionRevision();
        service.m_registrationDevice = api.deviceIdentitySnapshot();
        service.m_state = DeviceRegistrationService::State::Registering;
        return service.m_request;
    }
    static void succeed(DeviceRegistrationService &service, quint64 id)
    {
        service.succeed(id, QByteArrayLiteral("{\"dfid\":\"fixture-dfid\"}"));
    }
};
class AccountServiceTestAccess
{
public:
    static quint64 refresh(AccountService &service, ApiClient &api)
    {
        service.m_refresh = api.capabilities(); // No-network control seam.
        service.m_state = AccountService::State::Refreshing;
        return service.m_refresh;
    }
    static void succeed(AccountService &service, quint64 id, const QByteArray &payload)
    {
        service.succeed(id, payload);
    }
};
class ApiClientTestAccess
{
public:
    static quint64 completedNativeAwaitingGui(ApiClient &client)
    {
        // A worker with no such job returns false from native cancel. Inject
        // the remaining GUI envelope to deterministically exercise that gap,
        // without network/sleeps or relying on thread scheduling.
        const quint64 id = 999999;
        ApiClient::Pending pending;
        pending.revision = client.m_revision;
        pending.logicalId = id;
        client.m_pending.insert(id, std::move(pending));
        return id;
    }
    static QJsonObject request(ApiClient &client, quint64 id)
    {
        return client.m_pending.value(id).request;
    }
    static void challenge(ApiClient &client, quint64 id, const QString &event)
    {
        const auto payload =
            QJsonDocument(
                QJsonObject{{"kind", "challenge"}, {"challenge", QJsonObject{{"event_id", event}}}})
                .toJson();
        client.deliver(id, client.m_revision, 5, payload);
    }
    static quint64 acknowledge(ApiClient &client, quint64 target)
    {
        const auto id = pendingAcknowledgement(client, target);
        completeAcknowledgement(client, id);
        return id;
    }
    static quint64 pendingAcknowledgement(ApiClient &client, quint64 target)
    {
        const auto id = client.capabilities(); // Actual no-network FFI request.
        client.m_pending[id].verifiesRequest = target;
        return id;
    }
    static void completeAcknowledgement(ApiClient &client, quint64 id)
    {
        client.deliver(id, client.m_revision, 0, QByteArrayLiteral("{\"acknowledged\":true}"));
    }
    static quint64 operationId(ApiClient &client, const QString &operation)
    {
        for (auto it = client.m_pending.cbegin(); it != client.m_pending.cend(); ++it)
            if (it->request.value("operation").toString() == operation)
                return it.key();
        return 0;
    }
    static void succeed(ApiClient &client, quint64 id, const QByteArray &payload)
    {
        client.deliver(id, client.m_revision, 0, payload);
    }
    static quint64 retryId(ApiClient &client, quint64 logical)
    {
        for (auto it = client.m_pending.cbegin(); it != client.m_pending.cend(); ++it)
            if (it->logicalId == logical && it->retried)
                return it.key();
        return 0;
    }
    static void authenticationFailure(ApiClient &client, quint64 id)
    {
        client.deliver(id, client.m_revision, 5,
                       QByteArrayLiteral("{\"kind\":\"authentication\"}"));
    }
};
} // namespace EchoMusic
class FoundationTest : public QObject
{
    Q_OBJECT
private slots:
    void networkTimeoutPreferencesPersistTogetherAndRejectInvalidOrClosedUpdates()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto path = directory.filePath("network.ini");
        EchoMusic::SettingsStore settings(path);
        ApiClient api(DeviceIdentity::temporary());
        const auto revision = api.sessionRevision();
        NetworkPreferences preferences(settings, api);
        QSignalSpy changed(&preferences, &NetworkPreferences::timeoutsChanged);
        QCOMPARE(preferences.apiTimeoutSeconds(), 0.);
        QCOMPARE(preferences.playerTimeoutSeconds(), 60.);
        QVERIFY(preferences.applyTimeouts(1.125, 2.375));
        QCOMPARE(changed.size(), 1);
        QCOMPARE(api.sessionRevision(), revision);
        QVERIFY(preferences.applyTimeouts(1.125, 2.375));
        QCOMPARE(changed.size(), 1);
        QVERIFY(!preferences.applyTimeouts(0, 0));
        QVERIFY(!preferences.applyTimeouts(std::numeric_limits<double>::quiet_NaN(), 60));
        QVERIFY(!preferences.applyTimeouts(301, 60));
        QCOMPARE(preferences.apiTimeoutSeconds(), 1.125);
        QCOMPARE(preferences.playerTimeoutSeconds(), 2.375);
        EchoMusic::SettingsStore reloaded(path);
        NetworkPreferences restored(reloaded, api);
        QCOMPARE(restored.apiTimeoutSeconds(), 1.125);
        QCOMPARE(restored.playerTimeoutSeconds(), 2.375);
        QCOMPARE(reloaded.allKeys(), (QStringList{"network/proxyPolicy", "network/timeouts"}));
        api.shutdown();
        QVERIFY(!preferences.applyTimeouts(0, 10));
        QCOMPARE(changed.size(), 1);
        QCOMPARE(preferences.playerTimeoutSeconds(), 2.375);
    }
    void networkTimeoutSaveFailureKeepsAppliedPair()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        // A directory cannot be replaced by an INI file, including when tests
        // run as a privileged user. This exercises real EchoMusic::SettingsStore sync failure.
        EchoMusic::SettingsStore settings(directory.path());
        ApiClient api(DeviceIdentity::temporary());
        NetworkPreferences preferences(settings, api);
        QSignalSpy changed(&preferences, &NetworkPreferences::timeoutsChanged);
        QVERIFY(!preferences.applyTimeouts(2, 3));
        QCOMPARE(changed.size(), 0);
        QCOMPARE(preferences.apiTimeoutSeconds(), 0.);
        QCOMPARE(preferences.playerTimeoutSeconds(), 60.);
        QVERIFY(!settings.contains("network/proxyPolicy"));
        QCOMPARE(preferences.errorMessage(), QString("无法保存网络设置"));
    }
    void networkTimeoutSaveCanRetryAfterStorageRecovery()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto parent = directory.filePath("blocked");
        QFile blocker(parent);
        QVERIFY(blocker.open(QIODevice::WriteOnly));
        blocker.write("not a directory");
        blocker.close();
        EchoMusic::SettingsStore settings(parent + "/preferences.ini");
        ApiClient api(DeviceIdentity::temporary());
        NetworkPreferences preferences(settings, api);
        QSignalSpy changed(&preferences, &NetworkPreferences::timeoutsChanged);
        QVERIFY(!preferences.applyTimeouts(1.5, 2.5));
        QVERIFY(blocker.remove());
        QVERIFY(QDir().mkpath(parent));
        QVERIFY(preferences.applyTimeouts(1.5, 2.5));
        QCOMPARE(changed.size(), 1);
        QVERIFY(preferences.errorMessage().isEmpty());
        EchoMusic::SettingsStore reopened(settings.fileName());
        NetworkPreferences restored(reopened, api);
        QCOMPARE(restored.apiTimeoutSeconds(), 1.5);
        QCOMPARE(restored.playerTimeoutSeconds(), 2.5);
    }
    void networkTimeoutFreshStoreRetainsItsProfile()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        EchoMusic::SettingsStore settings(directory.path() + "/portable/settings.ini");
        ApiClient api(DeviceIdentity::temporary());
        NetworkPreferences preferences(settings, api);
        QVERIFY(preferences.applyTimeouts(0.731, 4.625));
        EchoMusic::SettingsStore reopened(settings.fileName());
        NetworkPreferences restored(reopened, api);
        QCOMPARE(reopened.fileName(), settings.fileName());
        // Clean only this uniquely named test store before further assertions.
        reopened.clear();
        reopened.sync();
        QCOMPARE(restored.apiTimeoutSeconds(), 0.731);
        QCOMPARE(restored.playerTimeoutSeconds(), 4.625);
        QCOMPARE(reopened.status(), EchoMusic::SettingsStore::NoError);
    }
    void networkConfigurationPreservesSourceModesAndOrderedProxyRules()
    {
        using N = NetworkConfiguration;
        using P = NetworkProxyEndpoint;
        for (const auto &mode : {"direct", "system", "auto_detect", "pac_script", "fixed_servers"})
            QCOMPARE(N::modeKey(N::modeFromKey(mode)), QString(mode));
        QCOMPARE(N::modeFromKey("unknown"), N::Mode::Direct);
        const auto groups = N::parseProxyRules("http=127.0.0.1:7890,direct://;https=https://"
                                               "[::1]:8443;socks=socks5://proxy.example:1080");
        QVERIFY(groups);
        QCOMPARE(groups->size(), 3);
        QCOMPARE(groups->at(0).protocol, QString("http"));
        QCOMPARE(groups->at(0).endpoints.size(), 2);
        QCOMPARE(groups->at(0).endpoints[0].kind, P::Kind::Http);
        QCOMPARE(groups->at(0).endpoints[0].address.port(), 7890);
        QCOMPARE(groups->at(0).endpoints[1].kind, P::Kind::Direct);
        QCOMPARE(groups->at(1).endpoints[0].kind, P::Kind::Https);
        QCOMPARE(groups->at(1).endpoints[0].address.host(), QString("::1"));
        QCOMPARE(groups->at(2).endpoints[0].kind, P::Kind::Socks5);
        const auto socks4 =
            N::parseProxyRules("socks://proxy.example:1080,socks4://other.example:1080");
        QVERIFY(socks4);
        QCOMPARE(socks4->at(0).endpoints[0].kind, P::Kind::Socks5);
        QCOMPARE(socks4->at(0).endpoints[1].kind, P::Kind::Socks4);
        const auto implicitSocks4 = N::parseProxyRules("socks=proxy.example:1080");
        QVERIFY(implicitSocks4);
        QCOMPARE(implicitSocks4->at(0).endpoints[0].kind, P::Kind::Socks4);
        for (const auto &invalid :
             {"http=", "http://user:secret@proxy.example:80", "http://proxy.example/path",
              "http://proxy.example?query=1", "socks5h://proxy.example", "unknown=proxy.example",
              ";", "http://proxy.example:99999", "proxy.example,", "host\nname"})
            QVERIFY2(!N::parseProxyRules(invalid), invalid);
        QVERIFY(!N::parseProxyRules(QString(4097, 'a')));
        QCOMPARE(N::normalizedPacScript(" https://example.com/proxy.pac "),
                 QString("https://example.com/proxy.pac"));
        QCOMPARE(N::normalizedPacScript("file:///tmp/proxy.pac"), QString("file:///tmp/proxy.pac"));
        QVERIFY(N::normalizedPacScript("https://user:secret@example.com/proxy.pac").isEmpty());
        QVERIFY(N::normalizedPacScript("ftp://example.com/proxy.pac").isEmpty());
        QCOMPARE(N::normalizedBypassRules(" <local> ; *.example.com,,127.0.0.0/8 "),
                 QString("<local>,*.example.com,127.0.0.0/8"));
        N config;
        config.mode = N::Mode::FixedServers;
        QVERIFY(!config.validationError().isEmpty());
        config.proxyRules = "proxy.example:7890";
        config.apiTimeoutSeconds = 1.25;
        config.playerTimeoutSeconds = 301;
        const auto normalized = config.normalized();
        QVERIFY(normalized.validationError().isEmpty());
        QCOMPARE(normalized.apiTimeoutSeconds, 1.25);
        QCOMPARE(normalized.playerTimeoutSeconds, 300.0);
        config.apiTimeoutSeconds = std::numeric_limits<double>::quiet_NaN();
        config.playerTimeoutSeconds = std::numeric_limits<double>::infinity();
        QCOMPARE(config.normalized().apiTimeoutSeconds, 0.0);
        QCOMPARE(config.normalized().playerTimeoutSeconds, 60.0);
    }
    void apiNetworkTimeoutValidatesWithoutChangingAccountSession()
    {
        ApiClient api;
        const auto revision = api.sessionRevision();
        QVERIFY(api.setNetworkTimeout(0));
        QVERIFY(api.setNetworkTimeout(1.25));
        QVERIFY(api.setNetworkTimeout(300));
        QVERIFY(!api.setNetworkTimeout(-1));
        QVERIFY(!api.setNetworkTimeout(301));
        QVERIFY(!api.setNetworkTimeout(std::numeric_limits<double>::quiet_NaN()));
        QCOMPARE(api.sessionRevision(), revision);
        QSignalSpy closed(&api, &ApiClient::closed);
        api.shutdown();
        QVERIFY(!api.setNetworkTimeout(60));
        QTRY_COMPARE_WITH_TIMEOUT(closed.size(), 1, 15000);
    }
    void cancelCompletedNativeBeforeGuiDeliveryHasOneTerminal()
    {
        ApiClient api;
        QSignalSpy succeeded(&api, &ApiClient::succeeded);
        QSignalSpy failed(&api, &ApiClient::failed);
        const auto id = ApiClientTestAccess::completedNativeAwaitingGui(api);
        QVERIFY(api.cancel(id));
        QVERIFY(!api.cancel(id));
        ApiClientTestAccess::succeed(api, id, QByteArrayLiteral("{\"acknowledged\":true}"));
        QTRY_COMPARE(failed.size(), 1);
        QCOMPARE(succeeded.size(), 0);
        QCOMPARE(failed.first().at(0).toULongLong(), id);
        QCOMPARE(failed.first().at(1).toInt(), 1); // EchoStatus::Cancelled.
    }
    void sharedVerificationCancelsRejectsStaleAndRetriesOnce()
    {
        ApiClient api;
        VerificationService service(api);
        auto trigger = [&] {
            const auto id = api.search(ApiClient::SearchKind::Song, QString());
            // Native validation rejects this generated overlong event before HTTP;
            // only the canonical GUI control flow below is injected.
            ApiClientTestAccess::challenge(api, id, QString(4097, QChar(u'v')));
            return id;
        };
        const auto original = trigger();
        QVERIFY(service.open());
        QCOMPARE(service.state(), VerificationService::State::Loading);
        const auto info = ApiClientTestAccess::operationId(api, QStringLiteral("get_verify_info"));
        QVERIFY(info);
        ApiClientTestAccess::succeed(
            api, info,
            QByteArrayLiteral(
                "{\"provider\":\"tencent\",\"app_id\":\"197787253\",\"verification_type\":23}"));
        QCOMPARE(service.state(), VerificationService::State::Ready);
        const auto generation = service.generation();
        service.submitTencent(generation - 1, QStringLiteral("fixture-ticket"),
                              QStringLiteral("fixture-randstr"));
        QVERIFY(!ApiClientTestAccess::operationId(api, QStringLiteral("sidedt")));
        service.submitTencent(generation, QStringLiteral("fixture-ticket"),
                              QStringLiteral("fixture-randstr"));
        const auto submit = ApiClientTestAccess::operationId(api, QStringLiteral("sidedt"));
        QVERIFY(submit);
        const auto proof = ApiClientTestAccess::request(api, submit)
                               .value("query")
                               .toObject()
                               .value("proof")
                               .toString();
        QVERIFY(proof.startsWith(QStringLiteral("KGCodeTX|")));
        const auto fields = QJsonDocument::fromJson(proof.mid(9).toUtf8()).object();
        QCOMPARE(fields.value("txappid").toString(), QStringLiteral("197787253"));
        ApiClientTestAccess::succeed(api, submit, QByteArrayLiteral("{\"acknowledged\":true}"));
        QVERIFY(!service.open());
        const auto retry = ApiClientTestAccess::retryId(api, original);
        QVERIFY(retry);
        QSignalSpy failure(&api, &ApiClient::failed);
        ApiClientTestAccess::challenge(api, retry, QStringLiteral("control-flow-fixture-event"));
        QCOMPARE(failure.size(), 1);
        QVERIFY(!service.open());
        trigger();
        QVERIFY(service.open());
        service.cancel();
        QVERIFY(!service.open());
        service.submitTencent(generation, QStringLiteral("stale-ticket"),
                              QStringLiteral("stale-randstr"));
        QVERIFY(!ApiClientTestAccess::operationId(api, QStringLiteral("sidedt")));
        trigger();
        api.advanceSession();
        QVERIFY(!service.open());
    }
    void loginConfirmationIsExplicitAndRejectsAnotherAccountReplay()
    {
        ApiClient api;
        VerificationService service(api);
        QVERIFY(api.setAccount(41, QStringLiteral("unit-old-token")));
        const auto original = api.search(ApiClient::SearchKind::Song, QString());
        ApiClientTestAccess::challenge(api, original, QString(4097, QChar(u'v')));
        const auto info = ApiClientTestAccess::operationId(api, QStringLiteral("get_verify_info"));
        ApiClientTestAccess::succeed(
            api, info, QByteArrayLiteral("{\"provider\":\"login\",\"verification_type\":38}"));
        QSignalSpy login(&service, &VerificationService::loginRequested);
        service.beginLogin();
        QCOMPARE(login.size(), 1);
        QVERIFY(!service.open());
        QSignalSpy failures(&api, &ApiClient::failed);
        QVERIFY(api.setAccount(42, QStringLiteral("unit-other-token")));
        QTRY_COMPARE(failures.size(), 1);
        QVERIFY(!ApiClientTestAccess::retryId(api, original));
        const auto confirmation = api.search(ApiClient::SearchKind::Song, QString());
        ApiClientTestAccess::challenge(api, confirmation, QString(4098, QChar(u'v')));
        ApiClientTestAccess::succeed(
            api, ApiClientTestAccess::operationId(api, QStringLiteral("get_verify_info")),
            QByteArrayLiteral("{\"provider\":\"login\",\"verification_type\":38}"));
        service.beginLogin();
        QVERIFY(api.setAccount(42, QStringLiteral("unit-fresh-token")));
        QTRY_VERIFY(ApiClientTestAccess::retryId(api, confirmation));
        const auto retry = ApiClientTestAccess::retryId(api, confirmation);
        const auto identity = ApiClientTestAccess::request(api, retry).value("identity").toObject();
        QCOMPARE(identity.value("token").toString(), QStringLiteral("unit-fresh-token"));
        ApiClientTestAccess::succeed(api, retry, QByteArrayLiteral("{}"));
    }
    void initialLoginAndSmsCandidateModelDoNotInventCredentials()
    {
        ApiClient api;
        AccountService account(api);
        const auto authorization = QByteArrayLiteral(
            "{\"user_id\":17,\"token\":\"unit-token\",\"nickname\":\"unit\",\"avatar_url\":\"\","
            "\"refresh_credential\":null,\"expires_at_seconds\":null}");
        QVERIFY(!account.adoptSession(authorization));
        QVERIFY(account.adoptLoginAuthorization(authorization));
        QCOMPARE(account.userId(), quint64(17));
        LoginCandidateModel model;
        QAbstractItemModelTester tester(&model,
                                        QAbstractItemModelTester::FailureReportingMode::QtTest);
        QJsonArray rows;
        for (int i = 1; i <= 250; ++i)
            rows.append(QJsonObject{{"user_id", i},
                                    {"nickname", QString::number(i)},
                                    {"avatar_url", ""},
                                    {"username", ""}});
        QVERIFY(model.replace(rows));
        QCOMPARE(model.rowCount(), 250);
        QCOMPARE(model.userIdAt(249), quint64(250));
        QVERIFY(!model.replace(QJsonArray{QJsonObject{{"user_id", 0}}}));
        QCOMPARE(model.rowCount(), 250);
        model.clear();
        QCOMPARE(model.rowCount(), 0);
        QCOMPARE(model.userIdAt(0), quint64(0));
    }
    void sharedSmsVerificationUsesCodeAndCancelsStaleAcknowledgement()
    {
        ApiClient api;
        VerificationService service(api);
        const auto original = api.search(ApiClient::SearchKind::Song, QString());
        ApiClientTestAccess::challenge(api, original, QString(4097, QChar(u'v')));
        ApiClientTestAccess::succeed(
            api, ApiClientTestAccess::operationId(api, QStringLiteral("get_verify_info")),
            QByteArrayLiteral("{\"provider\":\"sms\",\"verification_type\":32}"));
        service.submitSms(QStringLiteral(" 123456 "));
        const auto submit = ApiClientTestAccess::operationId(api, QStringLiteral("sidedt"));
        QVERIFY(submit);
        const auto query = ApiClientTestAccess::request(api, submit).value("query").toObject();
        QCOMPARE(query.value("verify_type").toInteger(), qint64(32));
        QCOMPARE(query.value("proof").toString(), QStringLiteral("123456"));
        service.cancel();
        QVERIFY(!service.open());
        ApiClientTestAccess::succeed(api, submit, QByteArrayLiteral("{\"acknowledged\":true}"));
        QVERIFY(!ApiClientTestAccess::retryId(api, original));
    }
    void playlistModelPaginationScopeAndReentrantChange()
    {
        ApiClient api;
        UserPlaylistModel model(api);
        QAbstractItemModelTester tester(&model,
                                        QAbstractItemModelTester::FailureReportingMode::QtTest);
        tester.setUseFetchMore(false);
        QVERIFY(api.setAccount(1, QStringLiteral("fixture-token")));
        auto page = [](int number, int count, quint64 revision = 7) {
            QJsonArray rows;
            for (int i = 0; i < count; ++i)
                rows.append(QJsonObject{
                    {"cloud_list_id", (number - 1) * 30 + i + 1},
                    {"global_id",
                     QStringLiteral("collection-fixture-%1").arg((number - 1) * 30 + i + 1)},
                    {"creator_global_id",
                     QStringLiteral("creator-fixture-%1").arg((number - 1) * 30 + i + 1)},
                    {"title", "Fixture"},
                    {"description", ""},
                    {"tags", ""},
                    {"cover_url", ""},
                    {"creator_id", 1},
                    {"creator_name", "Fixture"},
                    {"song_count", 5},
                    {"revision", 2},
                    {"sort_index", i},
                    {"source_code", 1},
                    {"kind", i % 2 ? "collected" : "owned"},
                    {"editable", i % 2 == 0},
                    {"private", false},
                    {"default_kind", QJsonValue::Null}});
            return QJsonDocument(QJsonObject{{"user_id", 1},
                                             {"total_revision", qint64(revision)},
                                             {"page", QJsonObject{{"page", number},
                                                                  {"page_size", 30},
                                                                  {"total", 32},
                                                                  {"items", rows}}}})
                .toJson();
        };
        auto request = UserPlaylistModelTestAccess::prepare(model, api);
        UserPlaylistModelTestAccess::succeed(model, request, page(1, 30));
        QCOMPARE(model.rowCount(), 30);
        QVERIFY(model.canFetchMore({}));
        QCOMPARE(model.totalCount(), quint64(32));
        QVERIFY(!model.cloudIdAt(1).has_value());
        QVERIFY(model.cloudIdAt(0).has_value());
        const auto selected = model.requestTracksAt(1, 1, 2);
        QCOMPARE(ApiClientTestAccess::request(api, selected).value("operation").toString(),
                 QStringLiteral("playlist_track_all"));
        QCOMPARE(ApiClientTestAccess::request(api, selected)
                     .value("query")
                     .toObject()
                     .value("global_id")
                     .toString(),
                 QStringLiteral("creator-fixture-2"));
        QVERIFY(model.roleNames().contains(UserPlaylistModel::GlobalIdRole));
        request = UserPlaylistModelTestAccess::prepare(model, api);
        UserPlaylistModelTestAccess::succeed(model, request, page(2, 2, 8));
        QCOMPARE(model.state(), UserPlaylistModel::State::RestartRequired);
        QCOMPARE(model.rowCount(), 30);
        QVERIFY(!model.canFetchMore({}));
        UserPlaylistModelTestAccess::reset(model);
        request = UserPlaylistModelTestAccess::prepare(model, api);
        UserPlaylistModelTestAccess::succeed(model, request, page(1, 30));
        request = UserPlaylistModelTestAccess::prepare(model, api);
        UserPlaylistModelTestAccess::succeed(model, request, page(2, 2));
        QCOMPARE(model.rowCount(), 32);
        QVERIFY(!model.canFetchMore({}));
        const auto old = UserPlaylistModelTestAccess::prepare(model, api);
        QVERIFY(api.setAccount(2, QStringLiteral("fixture-other-token")));
        QCOMPARE(model.rowCount(), 0);
        UserPlaylistModelTestAccess::succeed(model, old, page(1, 30));
        QCOMPARE(model.rowCount(), 0);
        QVERIFY(api.setAccount(1, QStringLiteral("fixture-token")));
        request = UserPlaylistModelTestAccess::prepare(model, api);
        connect(&model, &QAbstractItemModel::rowsAboutToBeInserted, &model,
                [&] { api.setAccount(0, {}); });
        UserPlaylistModelTestAccess::succeed(model, request, page(1, 30));
        QCOMPARE(model.rowCount(), 0);
        QCOMPARE(model.state(), UserPlaylistModel::State::SignedOut);
    }
    void playlistRequestKindsAndAuthenticationRemainExplicit()
    {
        ApiClient api;
        QVERIFY(!api.userPlaylists());
        QVERIFY(api.setAccount(1, QStringLiteral("fixture-token"), QStringLiteral("fixture-t1")));
        const auto owned = api.cloudPlaylistTracks({1});
        const auto collected = api.cloudPlaylistTracks({2});
        QCOMPARE(ApiClientTestAccess::request(api, owned)
                     .value("query")
                     .toObject()
                     .value("cloud_list_id")
                     .toInteger(),
                 qint64(1));
        QCOMPARE(ApiClientTestAccess::request(api, collected)
                     .value("query")
                     .toObject()
                     .value("cloud_list_id")
                     .toInteger(),
                 qint64(2));
        const auto publicRequest = api.publicPlaylistTracks({QStringLiteral("fixture-global")}, 1,
                                                            2, {AuthenticationMode::DeviceOnly});
        const auto request = ApiClientTestAccess::request(api, publicRequest);
        QVERIFY(!request.value("query").toObject().contains("cloud_list_id"));
        QCOMPARE(request.value("identity").toObject().value("userid").toInteger(), qint64(0));
        QVERIFY(request.value("identity").toObject().value("t1").toString().isEmpty());
        QVERIFY(!api.cloudPlaylistTracks({0}));
        QVERIFY(!api.userPlaylists(0));
    }
    void liveUserPlaylistModelThroughQt()
    {
        if (qEnvironmentVariable("ECHO_LIVE_PLAYLIST") != QStringLiteral("1"))
            QSKIP("Opt-in account read-only integration");
        std::string input((std::istreambuf_iterator<char>(std::cin)),
                          std::istreambuf_iterator<char>());
        const auto identity = QJsonDocument::fromJson(QByteArray::fromStdString(input)).object();
        DeviceIdentity device;
        device.mid = identity.value("mid").toString();
        device.dfid = identity.value("dfid").toString();
        device.guid = identity.value("guid").toString();
        device.mac = identity.value("mac").toString();
        device.deviceName = identity.value("device_name").toString();
        ApiClient api(device);
        QVERIFY(api.setAccount(quint64(identity.value("userid").toInteger()),
                               identity.value("token").toString(),
                               identity.value("t1").toString()));
        UserPlaylistModel model(api);
        QAbstractItemModelTester tester(&model,
                                        QAbstractItemModelTester::FailureReportingMode::QtTest);
        tester.setUseFetchMore(false);
        model.reload();
        QTRY_VERIFY_WITH_TIMEOUT(model.state() != UserPlaylistModel::State::Loading, 30000);
        QCOMPARE(model.state(), UserPlaylistModel::State::Ready);
        while (model.canFetchMore({})) {
            model.fetchMore({});
            QTRY_VERIFY_WITH_TIMEOUT(model.state() != UserPlaylistModel::State::Loading, 30000);
            QCOMPARE(model.state(), UserPlaylistModel::State::Ready);
        }
        QCOMPARE(quint64(model.rowCount()), model.totalCount());
        QVERIFY(model.rowCount() > 0);
        auto cloud = model.cloudIdAt(0);
        QVERIFY(cloud.has_value());
        QSignalSpy success(&api, &ApiClient::succeeded);
        QSignalSpy failure(&api, &ApiClient::failed);
        const auto request = api.cloudPlaylistTracks(*cloud, 1, 1);
        QTRY_VERIFY_WITH_TIMEOUT(!success.isEmpty() || !failure.isEmpty(), 30000);
        QVERIFY(failure.isEmpty());
        QCOMPARE(success.first()[0].toULongLong(), request);
        const auto tracks = QJsonDocument::fromJson(success.first()[1].toByteArray()).object();
        QCOMPARE(quint64(tracks.value("cloud_list_id").toInteger()), cloud->value);
        int collected = -1;
        for (int i = 0; i < model.rowCount(); ++i)
            if (model.data(model.index(i, 0), UserPlaylistModel::KindRole) ==
                    QStringLiteral("collected") &&
                model.data(model.index(i, 0), UserPlaylistModel::SourceCodeRole).toUInt() == 1) {
                collected = i;
                break;
            }
        QVERIFY(collected >= 0);
        success.clear();
        failure.clear();
        const auto publicRequest = model.requestTracksAt(collected, 1, 2);
        QVERIFY(publicRequest);
        QTRY_VERIFY_WITH_TIMEOUT(!success.isEmpty() || !failure.isEmpty(), 30000);
        QVERIFY(failure.isEmpty());
        QCOMPARE(success.first()[0].toULongLong(), publicRequest);
        const auto publicTracks =
            QJsonDocument::fromJson(success.first()[1].toByteArray()).object();
        QVERIFY(!publicTracks.value("items").toArray().isEmpty());
        QVERIFY(api.setAccount(0, {}));
        QCOMPARE(model.rowCount(), 0);
        QSignalSpy closed(&api, &ApiClient::closed);
        api.shutdown();
        QTRY_COMPARE_WITH_TIMEOUT(closed.size(), 1, 10000);
    }
    void registrationKeepsPersistentIdentityAndDoesNotRetryEmptyAcknowledgements()
    {
        QTemporaryDir directory;
        EchoMusic::SettingsStore settings(directory.filePath("device.ini"));
        const auto initial = DeviceIdentity::load(settings);
        ApiClient api(initial);
        NetworkPreferences network(settings, api);
        network.apply("fixed_servers", {}, "http://127.0.0.1:1", {}, {}, false, {}, 1, 1);
        QTRY_VERIFY(!network.busy());
        bool applied = false;
        network.afterPendingMediaWork([&] { applied = true; });
        QTRY_VERIFY(applied);
        QVERIFY(api.setAccount(42, "fixture-token", "fixture-t1"));
        DeviceRegistrationService registration(api, settings);
        QList<QJsonObject> requests;
        QList<quint64> ids;
        connect(&registration, &DeviceRegistrationService::changed, this, [&] {
            if (registration.state() != DeviceRegistrationService::State::Registering)
                return;
            const auto id = DeviceRegistrationServiceTestAccess::request(registration);
            QVERIFY(id);
            ids << id;
            requests << ApiClientTestAccess::request(api, id).value("identity").toObject();
            // Domain control-flow events, not fabricated upstream wire fixtures.
            if (requests.size() == 1)
                api.failed(id, 5,
                           QByteArrayLiteral("{\"kind\":\"device_registration_unassigned\"}"));
            else
                DeviceRegistrationServiceTestAccess::succeed(registration, id);
        });
        registration.start();
        QTRY_COMPARE(registration.state(), DeviceRegistrationService::State::Error);
        QCOMPARE(requests.size(), 1);
        QTest::qWait(800); // Failure stays failed until an explicit user retry.
        QCOMPARE(requests.size(), 1);
        QVERIFY(!settings.contains("network/registeredDevice"));
        QCOMPARE(api.deviceIdentitySnapshot().guid, initial.guid);
        registration.retry();
        QTRY_COMPARE(registration.state(), DeviceRegistrationService::State::Ready);
        QCOMPARE(requests.size(), 2);
        for (const auto &request : requests) {
            QCOMPARE(request.value("guid").toString(), initial.guid);
            QCOMPARE(request.value("mid").toString(), initial.mid);
            QCOMPARE(request.value("mac").toString(), initial.mac);
            QCOMPARE(request.value("render_fingerprint").toString(), initial.renderFingerprint);
            QCOMPARE(request.value("authentication").toString(), "device_only");
            QCOMPARE(request.value("userid").toInteger(), qint64(0));
            QVERIFY(request.value("token").toString().isEmpty());
            QVERIFY(request.value("t1").toString().isEmpty());
        }
        QCOMPARE(QSet<quint64>(ids.begin(), ids.end()).size(), 2);
        EchoMusic::SettingsStore reopened(settings.fileName());
        const auto saved = DeviceIdentity::load(reopened);
        QCOMPARE(saved.guid, initial.guid);
        QCOMPARE(saved.mid, initial.mid);
        QCOMPARE(saved.dfid, "fixture-dfid");
        QCOMPARE(saved.renderFingerprint, initial.renderFingerprint);
        ApiClient next(saved);
        DeviceRegistrationService warm(next, reopened);
        QCOMPARE(warm.state(), DeviceRegistrationService::State::Ready);
        QSignalSpy warmChanged(&warm, &DeviceRegistrationService::changed);
        warm.start();
        QCOMPARE(warmChanged.size(), 0);
        registration.shutdown();
        warm.shutdown();
    }
    void registeredDeviceSnapshotAndWriterShutdown()
    {
        QTemporaryDir directory;
        EchoMusic::SettingsStore settings(directory.path() + QStringLiteral("/device.ini"));
        auto device = DeviceIdentity::temporary();
        device.dfid = QStringLiteral("fixture-dfid");
        QVERIFY(DeviceIdentity::saveRegistration(settings, device));
        EchoMusic::SettingsStore reopened(settings.fileName());
        const auto loaded = DeviceIdentity::load(reopened);
        QCOMPARE(loaded.guid, device.guid);
        QCOMPARE(loaded.mid, device.mid);
        QCOMPARE(loaded.dfid, device.dfid);
        QCOMPARE(loaded.deviceName, QStringLiteral("EchoMusicNext"));
        // The application-owned registration name cannot be overridden by a
        // persisted old generated name, while the registered GUID/MID survive.
        auto saved = reopened.value("network/registeredDevice").toMap();
        saved.insert("deviceName", "ABCDEF0123");
        reopened.setValue("network/registeredDevice", saved);
        const auto renamed = DeviceIdentity::load(reopened);
        QCOMPARE(renamed.deviceName, QStringLiteral("EchoMusicNext"));
        QCOMPARE(renamed.mid, loaded.mid);
        QCOMPARE(renamed.dfid, loaded.dfid);
        auto invalid = device;
        invalid.mid = QStringLiteral("wrong-mid");
        QVERIFY(!DeviceIdentity::saveRegistration(settings, invalid));
        ApiClient api;
        DeviceRegistrationService registration(api, settings);
        QSignalSpy closed(&registration, &DeviceRegistrationService::closed);
        const auto original = api.search(ApiClient::SearchKind::Song, QString());
        const auto identity =
            ApiClientTestAccess::request(api, original).value("identity").toObject();
        const auto pending = DeviceRegistrationServiceTestAccess::prepare(registration, api);
        DeviceRegistrationServiceTestAccess::succeed(registration, pending);
        registration.shutdown();
        QTRY_COMPARE(closed.size(), 1);
        const auto after = api.search(ApiClient::SearchKind::Song, QString());
        QCOMPARE(ApiClientTestAccess::request(api, after).value("identity").toObject().value("mid"),
                 identity.value("mid"));
    }
    void accountRefreshPreservesIdentityAndRejectsLateResults()
    {
        ApiClient api;
        AccountService account(api);
        auto session = [](quint64 user, const QString &token, const QString &t1) {
            return QJsonDocument(QJsonObject{{"user_id", qint64(user)},
                                             {"token", token},
                                             {"refresh_credential", t1},
                                             {"expires_at_seconds", qint64(2000000000)},
                                             {"nickname", "Fixture"},
                                             {"avatar_url", ""}})
                .toJson();
        };
        QVERIFY(account.adoptSession(
            session(1, QStringLiteral("fixture-token"), QStringLiteral("fixture-t1"))));
        QCOMPARE(account.state(), AccountService::State::SignedIn);
        auto registered = DeviceIdentity::temporary();
        registered.dfid = QStringLiteral("fixture-dfid");
        QVERIFY(api.setDeviceIdentity(registered));
        QCOMPARE(account.state(), AccountService::State::SignedIn);
        QCOMPARE(account.userId(), quint64(1));
        const auto revision = api.sessionRevision();
        const auto first = AccountServiceTestAccess::refresh(account, api);
        AccountServiceTestAccess::succeed(
            account, first,
            session(1, QStringLiteral("rotated-token"), QStringLiteral("rotated-t1")));
        QCOMPARE(account.state(), AccountService::State::SignedIn);
        QCOMPARE(api.sessionRevision(), revision + 1);
        const auto check = api.search(ApiClient::SearchKind::Song, QString());
        QCOMPARE(ApiClientTestAccess::request(api, check)
                     .value("identity")
                     .toObject()
                     .value("t1")
                     .toString(),
                 QStringLiteral("rotated-t1"));
        const auto late = AccountServiceTestAccess::refresh(account, api);
        QVERIFY(account.adoptSession(
            session(2, QStringLiteral("second-token"), QStringLiteral("second-t1"))));
        AccountServiceTestAccess::succeed(
            account, late, session(1, QStringLiteral("stale-token"), QStringLiteral("stale-t1")));
        QCOMPARE(account.userId(), quint64(2));
        account.signOut();
        QCOMPARE(account.state(), AccountService::State::SignedOut);
        QCOMPARE(account.userId(), quint64(0));
        const auto anonymous = api.search(ApiClient::SearchKind::Song, QString());
        QVERIFY(ApiClientTestAccess::request(api, anonymous)
                    .value("identity")
                    .toObject()
                    .value("token")
                    .toString()
                    .isEmpty());
        // A direct sessionChanged slot signs out during adoption; outer data
        // must not restore an account after that newer user action.
        bool once = true;
        const auto connection = connect(&api, &ApiClient::sessionChanged, &account, [&] {
            if (std::exchange(once, false))
                account.signOut();
        });
        QVERIFY(!account.adoptSession(
            session(3, QStringLiteral("third-token"), QStringLiteral("third-t1"))));
        QCOMPARE(account.state(), AccountService::State::SignedOut);
        QCOMPARE(account.userId(), quint64(0));
        disconnect(connection);
    }
    void qrPollingRejectsInvalidPixelsAndStaleSessions()
    {
        ApiClient api;
        QrLoginService qr(api);
        QImage image(16, 16, QImage::Format_ARGB32);
        image.fill(Qt::black);
        QByteArray png;
        QBuffer output(&png);
        output.open(QIODevice::WriteOnly);
        QVERIFY(image.save(&output, "PNG"));
        const auto valid =
            QJsonDocument(QJsonObject{{"key", "fixture-qr-key"},
                                      {"image_data_url", QStringLiteral("data:image/png;base64,") +
                                                             QString::fromLatin1(png.toBase64())}})
                .toJson();
        const auto first = QrLoginServiceTestAccess::prepare(qr, api);
        QrLoginServiceTestAccess::succeed(qr, first, valid);
        QCOMPARE(qr.state(), QrLoginService::State::Waiting);
        QVERIFY(!qr.imageDataUrl().isEmpty());
        const auto check = QrLoginServiceTestAccess::check(qr);
        QVERIFY(check); // First check starts immediately; there is no initial delay.
        QVERIFY(!QrLoginServiceTestAccess::waitingForNextCheck(qr));
        QrLoginServiceTestAccess::succeed(qr, check, QByteArrayLiteral("{\"state\":\"scanned\"}"));
        QCOMPARE(qr.state(), QrLoginService::State::Scanned);
        QVERIFY(!QrLoginServiceTestAccess::check(qr));
        QVERIFY(QrLoginServiceTestAccess::waitingForNextCheck(qr));
        api.advanceSession();
        QCOMPARE(qr.state(), QrLoginService::State::Idle);
        QVERIFY(!QrLoginServiceTestAccess::waitingForNextCheck(qr));
        QrLoginServiceTestAccess::succeed(qr, first, valid);
        QCOMPARE(qr.state(), QrLoginService::State::Idle);
        const auto malformed = QrLoginServiceTestAccess::prepare(qr, api);
        QrLoginServiceTestAccess::succeed(
            qr, malformed,
            QByteArrayLiteral(
                "{\"key\":\"fixture\",\"image_data_url\":\"data:image/png;base64,AAAA\"}"));
        QCOMPARE(qr.state(), QrLoginService::State::Error);
        QVERIFY(qr.imageDataUrl().isEmpty());
        qr.cancel();
        QCOMPARE(qr.state(), QrLoginService::State::Idle);
    }
    void liveAccountRefreshThroughQtService()
    {
        if (!qEnvironmentVariableIsSet("ECHO_LIVE_ACCOUNT"))
            QSKIP("Opt-in current-account snapshot via stdin; never stored by test");
        const std::string input((std::istreambuf_iterator<char>(std::cin)),
                                std::istreambuf_iterator<char>());
        const auto identity = QJsonDocument::fromJson(QByteArray::fromStdString(input)).object();
        const auto expectedUser = identity.value("userid").toInteger();
        QVERIFY(expectedUser > 0);
        DeviceIdentity device{identity.value("mid").toString(),
                              identity.value("dfid").toString(),
                              identity.value("guid").toString(),
                              identity.value("mac").toString(),
                              identity.value("device_name").toString(),
                              QStringLiteral("-"),
                              QStringLiteral("-")};
        ApiClient api(device);
        AccountService account(api);
        QSignalSpy succeeded(&api, &ApiClient::succeeded);
        QSignalSpy failed(&api, &ApiClient::failed);
        QSignalSpy closed(&api, &ApiClient::closed);
        // Bootstrap the current snapshot for this dev-only test. This timestamp
        // is test setup, not sampled token validity or a shipping import path.
        const auto initial = QJsonDocument(QJsonObject{{"user_id", expectedUser},
                                                       {"token", identity.value("token")},
                                                       {"refresh_credential", identity.value("t1")},
                                                       {"expires_at_seconds",
                                                        QDateTime::currentSecsSinceEpoch() + 60},
                                                       {"nickname", "Probe bootstrap"},
                                                       {"avatar_url", ""}})
                                 .toJson();
        QVERIFY(account.adoptSession(initial));
        account.refresh();
        QCOMPARE(account.state(), AccountService::State::Refreshing);
        QTRY_VERIFY_WITH_TIMEOUT(account.state() != AccountService::State::Refreshing, 25000);
        QCOMPARE(account.state(), AccountService::State::SignedIn);
        QCOMPARE(failed.size(), 0);
        QVERIFY(qint64(account.userId()) == expectedUser); // Never print the actual ID on failure.
        QVERIFY(account.expiresAtSeconds() > quint64(QDateTime::currentSecsSinceEpoch()));
        const auto detail = api.userDetail();
        QVERIFY(detail > 0);
        QTRY_VERIFY_WITH_TIMEOUT(succeeded.size() >= 2 || !failed.isEmpty(), 25000);
        QCOMPARE(failed.size(), 0);
        const auto profile = QJsonDocument::fromJson(succeeded.back().at(1).toByteArray()).object();
        QVERIFY(profile.value("id").toInteger() == expectedUser);
        account.signOut();
        QCOMPARE(account.state(), AccountService::State::SignedOut);
        api.shutdown();
        QTRY_COMPARE(closed.size(), 1);
    }
    void wechatCodeExchangesOnceAndOnlyFinalLoginIsReplayed()
    {
        ApiClient api;
        QrLoginService qr(api);
        qr.setProvider(ApiClient::QrProvider::Wechat);
        QImage image(32, 32, QImage::Format_RGB32);
        image.fill(Qt::black); // Canonical pixel fixture, not a provider QR sample.
        QByteArray jpeg;
        QBuffer buffer(&jpeg);
        buffer.open(QIODevice::WriteOnly);
        QVERIFY(image.save(&buffer, "JPEG"));
        const auto created = QrLoginServiceTestAccess::prepare(qr, api);
        QrLoginServiceTestAccess::succeed(
            qr, created,
            QJsonDocument(QJsonObject{{"key", "unit-wx-key"},
                                      {"image_data_url", QStringLiteral("data:image/jpeg;base64,") +
                                                             QString::fromLatin1(jpeg.toBase64())}})
                .toJson());
        const auto check = QrLoginServiceTestAccess::check(qr);
        QVERIFY(check);
        ApiClientTestAccess::succeed(
            api, check,
            QByteArrayLiteral("{\"state\":\"provider_code\",\"code\":\"unit-wx-code\"}"));
        QCOMPARE(qr.state(), QrLoginService::State::Authorizing);
        const auto exchange = QrLoginServiceTestAccess::exchange(qr);
        QVERIFY(exchange);
        QCOMPARE(ApiClientTestAccess::request(api, exchange)
                     .value("query")
                     .toObject()
                     .value("stage")
                     .toString(),
                 QStringLiteral("authorize"));
        ApiClientTestAccess::succeed(
            api, exchange,
            QByteArrayLiteral("{\"openid\":\"unit-id\",\"access_token\":\"unit-oauth-token\"}"));
        const auto finish = QrLoginServiceTestAccess::finish(qr);
        QVERIFY(finish);
        QCOMPARE(ApiClientTestAccess::request(api, finish)
                     .value("identity")
                     .toObject()
                     .value("userid")
                     .toInteger(),
                 qint64(0));
        ApiClientTestAccess::challenge(api, finish, QStringLiteral("unit-final-login-event"));
        ApiClientTestAccess::acknowledge(api, finish);
        const auto retry = ApiClientTestAccess::retryId(api, finish);
        QVERIFY(retry);
        QCOMPARE(ApiClientTestAccess::request(api, retry)
                     .value("query")
                     .toObject()
                     .value("stage")
                     .toString(),
                 QStringLiteral("finish"));
        QVERIFY(!ApiClientTestAccess::operationId(api, QStringLiteral("login_wx_check")));
        QSignalSpy authorized(&qr, &QrLoginService::authorized);
        ApiClientTestAccess::succeed(
            api, retry,
            QByteArrayLiteral(
                "{\"user_id\":17,\"token\":\"unit-token\",\"nickname\":\"unit\",\"avatar_url\":"
                "\"\",\"refresh_credential\":null,\"expires_at_seconds\":null}"));
        QCOMPARE(authorized.size(), 1);
        QVERIFY(!qr.takeAuthorization().isEmpty());
        QVERIFY(qr.imageDataUrl().isEmpty());
    }
    void leavingQrMethodRejectsLateCreationExpiryAndFailures()
    {
        ApiClient api;
        QrLoginService qr(api);
        const auto oldCreate = QrLoginServiceTestAccess::prepare(qr, api);
        qr.pause();
        QCOMPARE(qr.state(), QrLoginService::State::Idle);
        QVERIFY(qr.imageDataUrl().isEmpty());
        QrLoginServiceTestAccess::succeed(qr, oldCreate,
                                          QByteArrayLiteral("{\"state\":\"expired\"}"));
        QrLoginServiceTestAccess::fail(qr, oldCreate,
                                       QByteArrayLiteral("{\"kind\":\"cancelled\"}"));
        QCOMPARE(qr.state(), QrLoginService::State::Idle);
        const auto current = QrLoginServiceTestAccess::prepare(qr, api);
        QVERIFY(current != oldCreate);
        QrLoginServiceTestAccess::succeed(qr, oldCreate,
                                          QByteArrayLiteral("{\"state\":\"expired\"}"));
        QrLoginServiceTestAccess::succeed(qr, 0, QByteArrayLiteral("{\"state\":\"expired\"}"));
        QCOMPARE(qr.state(), QrLoginService::State::Creating);
        QCOMPARE(QrLoginServiceTestAccess::create(qr), current);
        qr.pause();
        QrLoginServiceTestAccess::present(qr, QrLoginService::State::Scanned);
        qr.pause();
        QCOMPARE(qr.state(), QrLoginService::State::Idle);
        QVERIFY(!QrLoginServiceTestAccess::waitingForNextCheck(qr));
        QSignalSpy closed(&api, &ApiClient::closed);
        api.shutdown();
        QTRY_COMPARE(closed.size(), 1);
    }
    void qrDisplayTrimsOnlyExteriorMarginsAndPreservesCodePixels()
    {
        ApiClient api;
        QrLoginService qr(api);
        for (int margin : {0, 9, 30}) {
            QImage source(264 + 2 * margin, 264 + 2 * margin, QImage::Format_RGB32);
            source.fill(Qt::white);
            for (int y = 0; y < 264; ++y)
                for (int x = 0; x < 264; ++x)
                    source.setPixelColor(x + margin, y + margin,
                                         x == 0 || y == 0 || x == 263 || y == 263 ||
                                                 (x / 8 + y / 8) % 2
                                             ? Qt::black
                                             : Qt::white);
            QByteArray png;
            QBuffer output(&png);
            QVERIFY(output.open(QIODevice::WriteOnly));
            QVERIFY(source.save(&output, "PNG"));
            const auto id = QrLoginServiceTestAccess::prepare(qr, api);
            QrLoginServiceTestAccess::succeed(
                qr, id,
                QJsonDocument(
                    QJsonObject{{"key", "canonical-image-test"},
                                {"image_data_url",
                                 "data:image/png;base64," + QString::fromLatin1(png.toBase64())}})
                    .toJson());
            QCOMPARE(qr.state(), QrLoginService::State::Waiting);
            const auto displayed = QImage::fromData(
                QByteArray::fromBase64(qr.imageDataUrl().section(',', 1).toLatin1()));
            QCOMPARE(displayed.size(), QSize(332, 332));
            QCOMPARE(displayed.pixelColor(0, 0), QColor(Qt::white));
            QCOMPARE(displayed.copy(34, 34, 264, 264).convertToFormat(QImage::Format_RGB32),
                     source.copy(margin, margin, 264, 264));
            qr.pause();
        }
        QSignalSpy closed(&api, &ApiClient::closed);
        api.shutdown();
        QTRY_COMPARE(closed.size(), 1);
    }
    void liveQrImageGeometry()
    {
        if (!qEnvironmentVariableIsSet("ECHO_LIVE_QR_GEOMETRY"))
            QSKIP("Anonymous QR image geometry only; no login authorization");
        ApiClient api;
        for (const auto provider : {ApiClient::QrProvider::Kugou, ApiClient::QrProvider::Qq,
                                    ApiClient::QrProvider::Wechat}) {
            QrLoginService qr(api);
            qr.setProvider(provider);
            qr.start();
            QTRY_VERIFY_WITH_TIMEOUT(qr.state() != QrLoginService::State::Creating, 65000);
            QCOMPARE(qr.state(), QrLoginService::State::Waiting);
            const auto displayed = qr.imageDataUrl();
            qr.pause();
            const auto image =
                QImage::fromData(QByteArray::fromBase64(displayed.section(',', 1).toLatin1()));
            QVERIFY(!image.isNull());
            QRect bounds;
            for (int y = 0; y < image.height(); ++y)
                for (int x = 0; x < image.width(); ++x)
                    if (qGray(image.pixel(x, y)) < 128)
                        bounds |= QRect(x, y, 1, 1);
            qInfo() << "QR image geometry" << int(provider) << image.size() << bounds;
            qr.cancel();
        }
        QSignalSpy closed(&api, &ApiClient::closed);
        api.shutdown();
        QTRY_COMPARE(closed.size(), 1);
    }
    void liveNativeProviderQrCreationAndPolling()
    {
        if (!qEnvironmentVariableIsSet("ECHO_LIVE_PROVIDER_QR"))
            QSKIP("Opt-in anonymous provider create/poll; no successful account login claim");
        ApiClient api;
        for (const auto provider : {ApiClient::QrProvider::Qq, ApiClient::QrProvider::Wechat}) {
            QrLoginService qr(api);
            qr.setProvider(provider);
            QSignalSpy success(&api, &ApiClient::succeeded);
            QSignalSpy failure(&api, &ApiClient::failed);
            qr.start();
            QTRY_VERIFY_WITH_TIMEOUT(qr.state() != QrLoginService::State::Creating, 65000);
            QCOMPARE(qr.state(), QrLoginService::State::Waiting);
            QVERIFY(!qr.imageDataUrl().isEmpty());
            QTRY_VERIFY_WITH_TIMEOUT(success.size() >= 2 || !failure.isEmpty(), 55000);
            QVERIFY(failure.isEmpty());
            QCOMPARE(qr.state(), QrLoginService::State::Waiting);
            qr.pause();
            QVERIFY(!QrLoginServiceTestAccess::waitingForNextCheck(qr));
            qr.cancel();
            QVERIFY(qr.imageDataUrl().isEmpty());
        }
        QSignalSpy closed(&api, &ApiClient::closed);
        api.shutdown();
        QTRY_COMPARE(closed.size(), 1);
    }
    void livePersistentDeviceRegistration()
    {
        if (!qEnvironmentVariableIsSet("ECHO_LIVE_REGISTRATION"))
            QSKIP("Opt-in real anonymous cold registration, save and warm reopening");
        QTemporaryDir directory;
        const auto path = directory.filePath("device.ini");
        DeviceIdentity before, registered;
        {
            EchoMusic::SettingsStore settings(path);
            before = DeviceIdentity::load(settings);
            settings.sync();
            ApiClient api(before);
            NetworkPreferences network(settings, api);
            QTRY_VERIFY(!network.busy());
            QVERIFY(network.ensureStartupTransport());
            DeviceRegistrationService registration(api, settings);
            QSignalSpy success(&api, &ApiClient::succeeded), failure(&api, &ApiClient::failed);
            registration.start();
            QTRY_VERIFY_WITH_TIMEOUT(
                registration.state() != DeviceRegistrationService::State::Registering, 25000);
            QCOMPARE(registration.state(), DeviceRegistrationService::State::Ready);
            EchoMusic::SettingsStore reopened(path);
            registered = DeviceIdentity::load(reopened);
            QVERIFY(!registered.dfid.isEmpty() && registered.dfid != "-");
            QCOMPARE(registered.guid, before.guid);
            QCOMPARE(registered.mid, before.mid);
            QCOMPARE(registered.mac, before.mac);
            QCOMPARE(registered.renderFingerprint, before.renderFingerprint);
            QCOMPARE(api.deviceIdentitySnapshot().dfid, registered.dfid);
            QCOMPARE(success.size(), 1);
            QCOMPARE(failure.size(), 0);
            qInfo() << "Actual registration assigned after attempts:"
                    << success.size() + failure.size() << "; complete device snapshot preserved";
            QSignalSpy deviceClosed(&registration, &DeviceRegistrationService::closed),
                apiClosed(&api, &ApiClient::closed);
            registration.shutdown();
            api.shutdown();
            QTRY_COMPARE(deviceClosed.size(), 1);
            QTRY_COMPARE(apiClosed.size(), 1);
        }
        {
            EchoMusic::SettingsStore reopened(path);
            ApiClient api(DeviceIdentity::load(reopened));
            DeviceRegistrationService warm(api, reopened);
            QSignalSpy changes(&warm, &DeviceRegistrationService::changed);
            warm.start();
            QCOMPARE(warm.state(), DeviceRegistrationService::State::Ready);
            QCOMPARE(changes.size(), 0);
            QCOMPARE(api.deviceIdentitySnapshot().guid, before.guid);
            QCOMPARE(api.deviceIdentitySnapshot().dfid, registered.dfid);
            warm.shutdown();
            QSignalSpy closed(&api, &ApiClient::closed);
            api.shutdown();
            QTRY_COMPARE(closed.size(), 1);
        }
    }
    void liveQrLoginAndRegistration()
    {
        if (!qEnvironmentVariableIsSet("ECHO_LIVE_SESSION"))
            QSKIP("Opt-in anonymous QR/registration requests; no account credentials");
        ApiClient api;
        QVERIFY(api.setAccount(1, QStringLiteral("fixture-token"), QStringLiteral("fixture-t1")));
        QrLoginService qr(api);
        QSignalSpy succeeded(&api, &ApiClient::succeeded);
        QSignalSpy failed(&api, &ApiClient::failed);
        QSignalSpy closed(&api, &ApiClient::closed);
        qr.start();
        QTRY_VERIFY_WITH_TIMEOUT(qr.state() != QrLoginService::State::Creating, 25000);
        QCOMPARE(qr.state(), QrLoginService::State::Waiting);
        QVERIFY(!qr.imageDataUrl().isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(succeeded.size() >= 2 || !failed.isEmpty(),
                                 25000); // A real polling completion.
        QCOMPARE(failed.size(), 0);
        QCOMPARE(qr.state(), QrLoginService::State::Waiting);
        qr.cancel();
        QTemporaryDir directory;
        EchoMusic::SettingsStore settings(directory.path() + QStringLiteral("/device.ini"));
        DeviceRegistrationService registration(api, settings);
        QSignalSpy registrationClosed(&registration, &DeviceRegistrationService::closed);
        registration.start();
        QTRY_VERIFY_WITH_TIMEOUT(
            registration.state() != DeviceRegistrationService::State::Registering, 25000);
        QCOMPARE(registration.state(), DeviceRegistrationService::State::Ready);
        EchoMusic::SettingsStore reopened(settings.fileName());
        const auto device = DeviceIdentity::load(reopened);
        QVERIFY(!device.dfid.isEmpty() && device.dfid != QStringLiteral("-"));
        QCOMPARE(device.mid, DeviceIdentity::midForGuid(device.guid));
        QCOMPARE(device.guid, api.deviceIdentitySnapshot().guid);
        qInfo("registration: actual identity assigned and persisted");
        registration.shutdown();
        QTRY_COMPARE(registrationClosed.size(), 1);
        api.shutdown();
        QTRY_COMPARE(closed.size(), 1);
    }
    void livePasswordVerificationThroughQtBridge()
    {
        if (!qEnvironmentVariableIsSet("ECHO_LIVE_VERIFICATION"))
            QSKIP("Opt-in fictional password login and invalid proof; no real account");
        ApiClient client;
        // Fixture account proves password login's mandatory DeviceOnly policy.
        QVERIFY(
            client.setAccount(1, QStringLiteral("fixture-token"), QStringLiteral("fixture-t1")));
        QSignalSpy success(&client, &ApiClient::succeeded);
        QSignalSpy failure(&client, &ApiClient::failed);
        QSignalSpy challenge(&client, &ApiClient::verificationRequired);
        QSignalSpy dismissed(&client, &ApiClient::verificationDismissed);
        QSignalSpy expired(&client, &ApiClient::authenticationExpired);
        QSignalSpy closed(&client, &ApiClient::closed);
        const auto original = client.passwordLogin(
            QStringLiteral("EchoNextNoUser7c9f"), QStringLiteral("EchoMusicNext-invalid-password"));
        QVERIFY(original > 0);
        const auto identity =
            ApiClientTestAccess::request(client, original).value("identity").toObject();
        QCOMPARE(identity.value("userid").toInteger(), qint64(0));
        QVERIFY(identity.value("token").toString().isEmpty() &&
                identity.value("t1").toString().isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(!challenge.isEmpty() || !failure.isEmpty(), 25000);
        QCOMPARE(failure.size(), 0);
        QCOMPARE(challenge.size(), 1);
        QCOMPARE(challenge.front().at(0).toULongLong(), original);
        QVERIFY(!challenge.front().at(1).toString().isEmpty()); // Never log the event.
        const auto info = client.verificationInfo(original);
        QVERIFY(info > 0);
        QTRY_VERIFY_WITH_TIMEOUT(!success.isEmpty() || !failure.isEmpty(), 25000);
        QCOMPARE(failure.size(), 0);
        QCOMPARE(success.front().at(0).toULongLong(), info);
        const auto config = QJsonDocument::fromJson(success.front().at(1).toByteArray()).object();
        QCOMPARE(config.value("provider").toString(), QStringLiteral("tencent"));
        QCOMPARE(config.value("verification_type").toInt(), 23);
        QVERIFY(!config.value("app_id").toString().isEmpty());
        QVERIFY(!config.value("session_id").toString().isEmpty());
        const auto proof =
            client.submitVerification(original, 23, QStringLiteral("EchoMusicNext-invalid-proof"));
        QVERIFY(proof > 0);
        QTRY_VERIFY_WITH_TIMEOUT(!failure.isEmpty(), 25000);
        QCOMPARE(failure.front().at(0).toULongLong(), proof);
        const auto error = QJsonDocument::fromJson(failure.front().at(2).toByteArray()).object();
        QCOMPARE(error.value("kind").toString(), QStringLiteral("business"));
        QCOMPARE(error.value("code").toInt(), 30791);
        QCOMPARE(challenge.size(), 1);
        QCOMPARE(dismissed.size(), 0); // Failed proof keeps the original suspended.
        QCOMPARE(expired.size(), 0);
        QCOMPARE(success.size(), 1); // No false success/retry of password login.
        QVERIFY(client.cancel(original));
        QCOMPARE(dismissed.size(), 1);
        QCOMPARE(failure.size(), 2);
        QCOMPARE(failure.back().at(0).toULongLong(), original);
        QCOMPARE(failure.back().at(1).toInt(), 1); // EchoStatus::Cancelled
        client.shutdown();
        QTRY_COMPARE(closed.size(), 1);
    }
    void livePublicRequestThroughQtBridge()
    {
        if (!qEnvironmentVariableIsSet("ECHO_LIVE_API"))
            QSKIP("Explicit opt-in; ordinary CI remains offline");
        ApiClient client;
        QSignalSpy success(&client, &ApiClient::succeeded);
        QSignalSpy failure(&client, &ApiClient::failed);
        QSignalSpy closed(&client, &ApiClient::closed);
        // A fresh anonymous device exercises typed host serialization, native HTTP,
        // domain conversion and GUI-thread delivery.
        const auto id = client.searchHot();
        QVERIFY(id > 0);
        QTRY_VERIFY_WITH_TIMEOUT(!success.isEmpty() || !failure.isEmpty(), 25000);
        QCOMPARE(failure.size(), 0);
        const auto groups = QJsonDocument::fromJson(success.front().at(1).toByteArray()).array();
        QVERIFY(!groups.isEmpty());
        QVERIFY(!groups.first().toObject().value("title").toString().isEmpty());
        client.shutdown();
        QTRY_COMPARE(closed.size(), 1);
    }
    void persistentDeviceUsesFullDecimalMid()
    {
        QCOMPARE(DeviceIdentity::midForGuid(QStringLiteral("0123456789abcdef0123456789abcdef")),
                 QStringLiteral(
                     "176905054734845483393488264506404844848")); // Deterministic device-ID vector.
        QTemporaryDir directory;
        const auto path = directory.path() + "/preferences.ini";
        QString firstMid;
        {
            EchoMusic::SettingsStore settings(path);
            firstMid = DeviceIdentity::load(settings).mid;
            settings.sync();
            QCOMPARE(settings.status(), EchoMusic::SettingsStore::NoError);
        }
        EchoMusic::SettingsStore reopened(path);
        const auto snapshot = DeviceIdentity::load(reopened);
        QCOMPARE(snapshot.mid, firstMid);
        QCOMPARE(snapshot.dfid, QStringLiteral("-"));
        QCOMPARE(snapshot.mid, DeviceIdentity::midForGuid(snapshot.guid));
        QVERIFY(!snapshot.mac.isEmpty());
        QCOMPARE(snapshot.deviceName, QStringLiteral("EchoMusicNext"));
        QCOMPARE(DeviceIdentity::temporary().deviceName, QStringLiteral("EchoMusicNext"));
        QVERIFY(!snapshot.uuid.isEmpty());
        QVERIFY(!snapshot.renderFingerprint.isEmpty());
        QCOMPARE(DeviceIdentity::load(reopened).renderFingerprint, snapshot.renderFingerprint);
        QVERIFY(firstMid.size() > 19); // Not truncated to a 64-bit integer.
        QVERIFY(!reopened.allKeys().contains(QStringLiteral("token")));
    }
    void authenticationPolicyPreservesDeviceAndExcludesAllAccountSecrets()
    {
        const auto device = DeviceIdentity::temporary();
        ApiClient client(device);
        QVERIFY(
            client.setAccount(1, QStringLiteral("fixture-token"), QStringLiteral("fixture-t1")));
        const auto account = client.search(ApiClient::SearchKind::Song, QString());
        const auto accountIdentity =
            ApiClientTestAccess::request(client, account).value("identity").toObject();
        QCOMPARE(accountIdentity.value("t1").toString(), QStringLiteral("fixture-t1"));
        const auto deviceId = client.search(ApiClient::SearchKind::Song, QString(), 1, 30,
                                            {AuthenticationMode::DeviceOnly});
        const auto deviceIdentity =
            ApiClientTestAccess::request(client, deviceId).value("identity").toObject();
        QCOMPARE(deviceIdentity.value("mid"), accountIdentity.value("mid"));
        QCOMPARE(deviceIdentity.value("guid"), accountIdentity.value("guid"));
        QCOMPARE(deviceIdentity.value("userid").toInteger(), qint64(0));
        QVERIFY(deviceIdentity.value("token").toString().isEmpty());
        QVERIFY(deviceIdentity.value("t1").toString().isEmpty());
        const auto anonymousId = client.search(ApiClient::SearchKind::Song, QString(), 1, 30,
                                               {AuthenticationMode::IndependentDevice});
        const auto anonymous =
            ApiClientTestAccess::request(client, anonymousId).value("identity").toObject();
        QVERIFY(anonymous.value("mid") != accountIdentity.value("mid"));
        QVERIFY(anonymous.value("guid") != accountIdentity.value("guid"));
        QVERIFY(anonymous.value("t1").toString().isEmpty());
        const auto login =
            client.passwordLogin(QString(), QString()); // Invalid input keeps this check offline.
        const auto loginIdentity =
            ApiClientTestAccess::request(client, login).value("identity").toObject();
        QCOMPARE(loginIdentity.value("userid").toInteger(), qint64(0));
        QVERIFY(loginIdentity.value("token").toString().isEmpty() &&
                loginIdentity.value("t1").toString().isEmpty());
        QCOMPARE(loginIdentity.value("mid"), accountIdentity.value("mid"));
        const auto revision = client.sessionRevision();
        QVERIFY(
            client.setAccount(1, QStringLiteral("fixture-token"), QStringLiteral("rotated-t1")));
        QCOMPARE(client.sessionRevision(), revision + 1);
        QVERIFY(!client.setAccount(0, QString(), QStringLiteral("orphan-t1")));
        QVERIFY(client.verificationInfo(account) == 0);
        QVERIFY(client.submitVerification(account, 23, QStringLiteral("proof")) == 0);
    }
    void verificationDeduplicatesAndRetriesOnceWithOriginalLogicalId()
    {
        ApiClient client;
        QSignalSpy challenge(&client, &ApiClient::verificationRequired);
        QSignalSpy failure(&client, &ApiClient::failed);
        const auto first = client.search(ApiClient::SearchKind::Song, QString());
        const auto second = client.search(ApiClient::SearchKind::Song, QString());
        const auto snapshot = ApiClientTestAccess::request(client, first);
        ApiClientTestAccess::challenge(client, first, QStringLiteral("fixture-event"));
        ApiClientTestAccess::challenge(client, second, QStringLiteral("fixture-event"));
        QCOMPARE(challenge.size(), 1);
        QCOMPARE(failure.size(), 0);
        ApiClientTestAccess::acknowledge(client, first);
        const auto retry = ApiClientTestAccess::retryId(client, first);
        QVERIFY(retry > 0 && retry != first);
        QCOMPARE(ApiClientTestAccess::request(client, retry), snapshot);
        ApiClientTestAccess::challenge(client, retry, QStringLiteral("fixture-event-again"));
        QCOMPARE(challenge.size(), 1); // No endless verification/retry cycle.
        QCOMPARE(failure.size(), 1);
        QCOMPARE(failure.front().at(0).toULongLong(), first);
        QCOMPARE(QJsonDocument::fromJson(failure.front().at(2).toByteArray())
                     .object()
                     .value("kind")
                     .toString(),
                 QStringLiteral("verification_retry_rejected"));
        QTRY_COMPARE(failure.size(),
                     2); // Second invalid search completes once using its original ID.
        QCOMPARE(failure.back().at(0).toULongLong(), second);
    }
    void verificationQueueCancellationAndSessionChangeDiscardContinuations()
    {
        ApiClient client;
        QSignalSpy challenge(&client, &ApiClient::verificationRequired);
        QSignalSpy dismissed(&client, &ApiClient::verificationDismissed);
        QSignalSpy failure(&client, &ApiClient::failed);
        const auto first = client.search(ApiClient::SearchKind::Song, QString());
        const auto duplicate = client.search(ApiClient::SearchKind::Song, QString());
        const auto next = client.search(ApiClient::SearchKind::Song, QString());
        ApiClientTestAccess::challenge(client, first, QStringLiteral("same"));
        ApiClientTestAccess::challenge(client, duplicate, QStringLiteral("same"));
        ApiClientTestAccess::challenge(client, next, QStringLiteral("next"));
        QCOMPARE(challenge.size(), 1);
        const auto proof = ApiClientTestAccess::pendingAcknowledgement(client, first);
        QVERIFY(
            client.cancel(duplicate)); // Cancelling a deduplicated waiter closes the shared event.
        QCOMPARE(failure.size(), 3);   // Both originals plus the in-flight proof have terminals.
        ApiClientTestAccess::completeAcknowledgement(client,
                                                     proof); // Late success cannot resume them.
        QCOMPARE(failure.size(), 3);
        QCOMPARE(dismissed.size(), 1);
        QCOMPARE(challenge.size(), 2);
        QCOMPARE(challenge.back().at(0).toULongLong(), next);
        client.advanceSession();
        QCOMPARE(dismissed.size(), 2);
        QVERIFY(client.verificationInfo(next) == 0);
        QVERIFY(client.submitVerification(next, 23, QStringLiteral("late-proof")) == 0);
        QTest::qWait(50);
        QCOMPARE(failure.size(), 3);
    }
    void reportOnlyVerificationAndAnonymousAuthenticationNeverPromptForCurrentAccount()
    {
        ApiClient client;
        QVERIFY(client.setAccount(1, QStringLiteral("fixture-token")));
        QSignalSpy challenge(&client, &ApiClient::verificationRequired);
        QSignalSpy expired(&client, &ApiClient::authenticationExpired);
        QSignalSpy failure(&client, &ApiClient::failed);
        const auto report = client.search(ApiClient::SearchKind::Song, QString(), 1, 30,
                                          {AuthenticationMode::CurrentAccount, false});
        ApiClientTestAccess::challenge(client, report, QStringLiteral("fixture"));
        QCOMPARE(challenge.size(), 0);
        QCOMPARE(failure.size(), 1);
        const auto anonymous = client.search(ApiClient::SearchKind::Song, QString(), 1, 30,
                                             {AuthenticationMode::DeviceOnly});
        ApiClientTestAccess::authenticationFailure(client, anonymous);
        QCOMPARE(expired.size(), 0);
        const auto account = client.search(ApiClient::SearchKind::Song, QString());
        ApiClientTestAccess::authenticationFailure(client, account);
        QCOMPARE(expired.size(), 1);
    }
    void typedRequestsValidateAndAccountChangesInvalidate()
    {
        ApiClient client;
        QSignalSpy failure(&client, &ApiClient::failed);
        QSignalSpy closed(&client, &ApiClient::closed);
        const auto id = client.search(ApiClient::SearchKind::Song, QString());
        QVERIFY(id > 0);
        QTRY_COMPARE(failure.size(), 1);
        const auto error = QJsonDocument::fromJson(failure.front().at(2).toByteArray()).object();
        QCOMPARE(error.value("kind").toString(), QStringLiteral("input"));
        const auto original = client.sessionRevision();
        QVERIFY(!client.setAccount(1, QString()));
        QCOMPARE(client.sessionRevision(), original);
        QVERIFY(client.setAccount(1, QStringLiteral("fixture-token")));
        QCOMPARE(client.sessionRevision(), original + 1);
        QVERIFY(client.setAccount(0, QString()));
        QCOMPARE(client.sessionRevision(), original + 2);
        client.shutdown();
        QTRY_COMPARE(closed.size(), 1);
    }
    void nativeDeliveryAndAsyncDrain()
    {
        ApiClient client;
        QSignalSpy success(&client, &ApiClient::succeeded);
        QSignalSpy closed(&client, &ApiClient::closed);
        bool guiThread = false;
        connect(&client, &ApiClient::succeeded, &client,
                [&] { guiThread = QThread::currentThread() == client.thread(); });
        const auto id = client.capabilities();
        QVERIFY(id > 0);
        QTRY_COMPARE(success.size(), 1);
        QCOMPARE(success.front().at(0).toULongLong(), id);
        QCOMPARE(QJsonDocument::fromJson(success.front().at(1).toByteArray())
                     .object()
                     .value("abi_version")
                     .toInt(),
                 1);
        QVERIFY(guiThread);
        client.shutdown();
        QCOMPARE(client.capabilities(), quint64(0));
        QTRY_COMPARE(closed.size(), 1);
    }
    void staleSessionNeverUpdatesNewSession()
    {
        ApiClient client;
        QSignalSpy success(&client, &ApiClient::succeeded);
        QSignalSpy failure(&client, &ApiClient::failed);
        QSignalSpy closed(&client, &ApiClient::closed);
        for (int i = 0; i < 64; ++i)
            QVERIFY(client.capabilities() > 0);
        client.advanceSession();
        const auto current = client.capabilities();
        QTRY_COMPARE(success.size(), 1);
        QCOMPARE(success.front().at(0).toULongLong(), current);
        QCOMPARE(failure.size(), 0);
        client.shutdown();
        QTRY_COMPARE(closed.size(), 1);
    }
    void independentStorageInitializesAndReopens()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto path = directory.path() + "/music.sqlite";
        for (int i = 0; i < 2; ++i) {
            StorageService storage;
            QSignalSpy initialized(&storage, &StorageService::initialized);
            QSignalSpy failure(&storage, &StorageService::failed);
            QSignalSpy closed(&storage, &StorageService::closed);
            storage.initialize(path);
            QTRY_COMPARE(initialized.size(), 1);
            QCOMPARE(failure.size(), 0);
            storage.shutdown();
            QTRY_COMPARE(closed.size(), 1);
        }
    }
};
QTEST_GUILESS_MAIN(FoundationTest)
#include "FoundationTest.moc"
