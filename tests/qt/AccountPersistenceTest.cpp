#include "AccountService.h"
#include "SavedAccountModel.h"
#include "StorageService.h"
#include <QAbstractItemModelTester>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTimer>
#include <QUuid>
#include <optional>
#include <QtTest>
using namespace EchoMusic;
namespace EchoMusic
{
class AccountServiceTestAccess
{
public:
    static QByteArray session(const AccountService &account)
    {
        return account.storedSession();
    }
};
class SavedAccountModelTestAccess
{
public:
    static void timeout(SavedAccountModel &model)
    {
        QMetaObject::invokeMethod(&model.m_validationDeadline, "timeout");
    }
};
class ApiClientTestAccess
{
public:
    static quint64 candidate(ApiClient &api)
    {
        for (auto it = api.m_pending.cbegin(); it != api.m_pending.cend(); ++it)
            if (it->request.value("operation") == QStringLiteral("user_detail"))
                return it.key();
        return 0;
    }
    static bool suspended(ApiClient &api, quint64 id)
    {
        return api.m_suspended.contains(id);
    }
    static quint64 retry(ApiClient &api, quint64 id)
    {
        for (auto it = api.m_pending.cbegin(); it != api.m_pending.cend(); ++it)
            if (it->logicalId == id && it->retried)
                return it.key();
        return 0;
    }
    static void acknowledge(ApiClient &api, quint64 id)
    {
        const auto info = api.capabilities();
        api.m_pending[info].verifiesRequest = id;
        deliver(api, info, 0, R"({"acknowledged":true})");
    }
    static QJsonObject request(ApiClient &api, quint64 id)
    {
        return api.m_pending.value(id).request;
    }
    static void deliver(ApiClient &api, quint64 id, int status, const QByteArray &payload)
    {
        api.deliver(id, api.m_revision, status, payload);
    }
    // Domain DTOs only: these test state transitions, never provider conversion.
    static void validate(ApiClient &api, qint64 user)
    {
        deliver(api, candidate(api), 0,
                QJsonDocument(QJsonObject{{"id", user}, {"nickname", "Verified"}}).toJson());
    }
    static QList<QJsonObject> requests(const ApiClient &api)
    {
        QList<QJsonObject> requests;
        for (const auto &pending : api.m_pending)
            requests.append(pending.request);
        return requests;
    }
    static std::optional<bool> verificationForOperation(const ApiClient &api,
                                                        const QString &operation, bool explicitUser)
    {
        for (const auto &pending : api.m_pending)
            if (pending.request.value("operation").toString() == operation &&
                pending.request.value("query").toObject().value("user_id").isNull() != explicitUser)
                return pending.automaticVerification;
        return std::nullopt;
    }
};
} // namespace EchoMusic
static QByteArray authorization(qint64 id,
                                const QString &token = QStringLiteral("test-private-token"))
{
    return QJsonDocument(QJsonObject{{"user_id", id},
                                     {"token", token},
                                     {"nickname", QStringLiteral("Test")},
                                     {"avatar_url", QString()}})
        .toJson(QJsonDocument::Compact);
}
class AccountPersistenceTest final : public QObject
{
    Q_OBJECT
private slots:
    void playlistCoverCachePersistsAndRejectsOlderVersionsAcrossAccounts()
    {
        QTemporaryDir dir;
        const auto path = dir.filePath("data.sqlite");
        {
            StorageService storage;
            QSignalSpy initialized(&storage, &StorageService::initialized);
            QSignalSpy replies(&storage, &StorageService::playlistCoversReady);
            QSignalSpy failed(&storage, &StorageService::playlistCoversFailed);
            storage.initialize(path);
            QTRY_COMPARE(initialized.size(), 1);
            storage.savePlaylistCover(100, {7, 9, "https://example.com/initial.jpg"});
            storage.savePlaylistCover(100, {7, 9, "https://example.com/enriched.jpg"});
            storage.savePlaylistCover(100, {7, 8, "https://example.com/old.jpg"});
            storage.savePlaylistCover(200, {7, 20, "https://example.com/other.jpg"});
            storage.loadPlaylistCovers(1, 100);
            QTRY_COMPARE(replies.size(), 1);
            const auto covers = qvariant_cast<StoredPlaylistCovers>(replies.first()[2]);
            QCOMPARE(covers.size(), 1);
            QCOMPARE(covers.first().revision, quint64(9));
            QCOMPARE(covers.first().url, QString("https://example.com/enriched.jpg"));
            QCOMPARE(failed.size(), 0);
            QSignalSpy closed(&storage, &StorageService::closed);
            storage.shutdown();
            QTRY_COMPARE(closed.size(), 1);
        }
        StorageService reopened;
        QSignalSpy initialized(&reopened, &StorageService::initialized);
        QSignalSpy replies(&reopened, &StorageService::playlistCoversReady);
        reopened.initialize(path);
        QTRY_COMPARE(initialized.size(), 1);
        reopened.loadPlaylistCovers(2, 100);
        QTRY_COMPARE(replies.size(), 1);
        QCOMPARE(qvariant_cast<StoredPlaylistCovers>(replies.first()[2]).first().url,
                 QString("https://example.com/enriched.jpg"));
        reopened.loadPlaylistCovers(3, 200);
        QTRY_COMPARE(replies.size(), 2);
        QCOMPARE(qvariant_cast<StoredPlaylistCovers>(replies.last()[2]).first().url,
                 QString("https://example.com/other.jpg"));
        QSignalSpy closed(&reopened, &StorageService::closed);
        reopened.shutdown();
        QTRY_COMPARE(closed.size(), 1);
    }
    void identityUpdatePreservesCredentialsAndSelection()
    {
        QTemporaryDir dir;
        const auto path = dir.filePath("identity.sqlite");
        {
            StorageService storage;
            QSignalSpy ready(&storage, &StorageService::accountsReady);
            storage.initialize(path);
            QTRY_COMPARE(ready.size(), 1);
            storage.saveAccount(1, {1, "Saved", {}, "token-a", 42, {}, "refresh-a", 123456789},
                                true);
            QTRY_COMPARE(ready.size(), 2);
            storage.updateAccountIdentity(1, "talent");
            QTRY_COMPARE(ready.size(), 3);
            const auto row = qvariant_cast<StoredAccounts>(ready.last()[1]).first();
            QCOMPARE(row.token, QString("token-a"));
            QCOMPARE(row.refreshCredential, QString("refresh-a"));
            QCOMPARE(row.expiresAtSeconds, qint64(123456789));
            QCOMPARE(row.lastUsedAt, qint64(42));
            QCOMPARE(row.avatarIdentityKind, QString("talent"));
            QCOMPARE(ready.last()[2].toULongLong(), quint64(1));
        }
        StorageService reopened;
        QSignalSpy ready(&reopened, &StorageService::accountsReady);
        reopened.initialize(path);
        QTRY_COMPARE(ready.size(), 1);
        const auto row = qvariant_cast<StoredAccounts>(ready.last()[1]).first();
        QCOMPARE(row.token, QString("token-a"));
        QCOMPARE(row.refreshCredential, QString("refresh-a"));
        QCOMPARE(row.expiresAtSeconds, qint64(123456789));
        QCOMPARE(row.avatarIdentityKind, QString("talent"));
    }
    void oldCredentialReferencesAreDiscardedWithoutImport()
    {
        QTemporaryDir dir;
        const auto path = dir.filePath("data.sqlite");
        const QString name = "old-catalog";
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", name);
            db.setDatabaseName(path);
            QVERIFY(db.open());
            QSqlQuery q(db);
            QVERIFY(q.exec("CREATE TABLE accounts(user_id INTEGER,secret_reference TEXT)"));
            QVERIFY(q.exec("INSERT INTO accounts VALUES(1,'obsolete-external-reference')"));
            QVERIFY(q.exec("CREATE TABLE account_selection(singleton INTEGER,account_id INTEGER)"));
            QVERIFY(q.exec("INSERT INTO account_selection VALUES(1,1)"));
            QVERIFY(q.exec("CREATE TABLE credential_cleanup(secret_reference TEXT)"));
            QVERIFY(q.exec("PRAGMA user_version=5"));
        }
        QSqlDatabase::removeDatabase(name);
        {
            StorageService storage;
            QSignalSpy ready(&storage, &StorageService::accountsReady);
            storage.initialize(path);
            QTRY_COMPARE(ready.size(), 1);
            QVERIFY(qvariant_cast<StoredAccounts>(ready.first()[1]).isEmpty());
            QCOMPARE(ready.first()[2].toULongLong(), quint64(0));
        }
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", name);
            db.setDatabaseName(path);
            QVERIFY(db.open());
            QSqlQuery q(db);
            QVERIFY(q.exec("SELECT COUNT(*) FROM sqlite_master WHERE name='credential_cleanup'"));
            QVERIFY(q.next());
            QCOMPARE(q.value(0).toInt(), 0);
            QVERIFY(q.exec("SELECT token,refresh_credential FROM accounts"));
            QVERIFY(!q.next());
        }
        QSqlDatabase::removeDatabase(name);
    }
    void versionOneUpgradePreservesNewProjectHistory()
    {
        QTemporaryDir dir;
        const auto path = dir.filePath("data.sqlite");
        const QString name = "v1-upgrade-fixture";
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", name);
            db.setDatabaseName(path);
            QVERIFY(db.open());
            QSqlQuery q(db);
            QVERIFY(q.exec("CREATE TABLE recent_searches(query TEXT PRIMARY KEY NOT "
                           "NULL,searched_at_ms INTEGER NOT NULL)"));
            QVERIFY(q.exec("INSERT INTO recent_searches VALUES('Existing search',1)"));
            QVERIFY(q.exec("PRAGMA user_version=1"));
            db.close();
        }
        QSqlDatabase::removeDatabase(name);
        StorageService storage;
        QSignalSpy initialized(&storage, &StorageService::initialized);
        storage.initialize(path);
        QTRY_COMPARE(initialized.size(), 1);
        QCOMPARE(storage.searchHistory(), QStringList{QStringLiteral("Existing search")});
        QSignalSpy closed(&storage, &StorageService::closed);
        storage.shutdown();
        QTRY_COMPARE(closed.size(), 1);
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", name);
            db.setDatabaseName(path);
            QVERIFY(db.open());
            QSqlQuery q(db);
            QVERIFY(q.exec("PRAGMA user_version"));
            QVERIFY(q.next());
            QCOMPARE(q.value(0).toInt(), 6);
            db.close();
        }
        QSqlDatabase::removeDatabase(name);
    }
    void failedCatalogReadbackRollsBackCredentialsAndMetadata()
    {
        QTemporaryDir dir;
        const auto path = dir.filePath("data.sqlite");
        StorageService storage;
        QSignalSpy initialized(&storage, &StorageService::initialized);
        QSignalSpy ready(&storage, &StorageService::accountsReady);
        QSignalSpy failed(&storage, &StorageService::accountsFailed);
        storage.initialize(path);
        QTRY_COMPARE(initialized.size(), 1);
        ready.clear();
        storage.saveAccount(1, {1, "First", {}, "live-reference", 1}, true);
        QTRY_COMPARE(ready.size(), 1);
        ready.clear();
        // Inject damaged selection metadata: post-write result reading fails.
        const QString name = "readback-failure-fixture";
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", name);
            db.setDatabaseName(path);
            QVERIFY(db.open());
            QSqlQuery q(db);
            QVERIFY(q.exec("DELETE FROM account_selection"));
            db.close();
        }
        QSqlDatabase::removeDatabase(name);
        storage.saveAccount(2, {1, "Replacement", {}, "new-reference", 2}, false);
        QTRY_COMPARE(failed.size(), 1);
        QCOMPARE(failed.first()[0].toULongLong(), quint64(2));
        QVERIFY(ready.isEmpty());
        QSignalSpy closed(&storage, &StorageService::closed);
        storage.shutdown();
        QTRY_COMPARE(closed.size(), 1);
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", name);
            db.setDatabaseName(path);
            QVERIFY(db.open());
            QSqlQuery q(db);
            QVERIFY(q.exec("SELECT token FROM accounts WHERE user_id=1"));
            QVERIFY(q.next());
            QCOMPARE(q.value(0).toString(), QStringLiteral("live-reference"));
            db.close();
        }
        QSqlDatabase::removeDatabase(name);
    }
    void qrDoesNotRequireOrInventRefreshCredentials()
    {
        ApiClient api;
        AccountService account(api);
        QSignalSpy adopted(&account, &AccountService::sessionAdopted);
        QVERIFY(account.adoptQrAuthorization(authorization(1)));
        QCOMPARE(account.expiresAtSeconds(), quint64(0));
        QCOMPARE(adopted.size(), 1);
        QCOMPARE(account.state(), AccountService::State::SignedIn);
        const auto meta = account.metaObject();
        QVERIFY(meta->indexOfProperty("token") < 0);
        QVERIFY(meta->indexOfProperty("refresh_credential") < 0);
        QVERIFY(!account.adoptSession(authorization(2)));
        QCOMPARE(account.userId(), quint64(1));
        auto invalid = QJsonDocument::fromJson(authorization(2)).object();
        invalid.insert("expires_at_seconds", 1);
        QVERIFY(account.adoptQrAuthorization(QJsonDocument(invalid).toJson()));
        QCOMPARE(account.userId(), quint64(2));
        QCOMPARE(adopted.size(), 2);
        connect(&account, &AccountService::changed, &account, [&] {
            if (account.userId() == 3)
                account.signOut();
        });
        QVERIFY(!account.adoptQrAuthorization(authorization(3)));
        QCOMPARE(account.userId(), quint64(0));
        QCOMPARE(adopted.size(), 2);
    }
    void missingLoginDisplayFieldsDoNotRejectValidAuthorization()
    {
        ApiClient api;
        AccountService account(api);
        QVERIFY(account.adoptQrAuthorization(
            R"({"user_id":1,"token":"test-proof","unrecognized":99})"));
        QCOMPARE(account.userId(), quint64(1));
        QVERIFY(account.adoptSession(
            R"({"user_id":1,"token":"test-proof","refresh_credential":"refresh-proof","expires_at_seconds":null})"));
        QCOMPARE(account.expiresAtSeconds(), quint64(0));
        QVERIFY(!account.adoptQrAuthorization(R"({"user_id":2,"token":""})"));
        QCOMPARE(account.userId(), quint64(1));
    }
    void profileCallsAndSavedAccountGradeAuthenticationFollowReference()
    {
        QTemporaryDir dir;
        ApiClient api;
        AccountService account(api);
        StorageService storage;
        SavedAccountModel model(api, account, storage);
        QSignalSpy initialized(&storage, &StorageService::initialized);
        storage.initialize(dir.filePath("data.sqlite"));
        QTRY_COMPARE(initialized.size(), 1);
        QVERIFY(account.adoptQrAuthorization(authorization(1)));
        QTRY_VERIFY(!model.busy());
        QVERIFY(account.adoptQrAuthorization(authorization(2)));
        QTRY_VERIFY(!model.busy());
        QVERIFY(
            ApiClientTestAccess::requests(api).isEmpty()); // QR adoption never adds token refresh.
        account.loadProfileOnce();
        account.loadProfileOnce();
        const auto profileRequests = ApiClientTestAccess::requests(api);
        QCOMPARE(profileRequests.size(), 3);
        QSet<QString> names;
        for (const auto &request : profileRequests) {
            names.insert(request.value("operation").toString());
            QCOMPARE(request.value("identity").toObject().value("userid").toInteger(), qint64(2));
        }
        QCOMPARE(names, (QSet<QString>{"user_detail", "user_vip_detail", "user_info"}));
        model.beginDurationQueries();
        int gradeCalls = 0;
        for (const auto &request : ApiClientTestAccess::requests(api)) {
            if (request.value("operation").toString() != QStringLiteral("user_grade_info"))
                continue;
            ++gradeCalls;
            const auto query = request.value("query").toObject();
            const auto identity = request.value("identity").toObject();
            if (query.value("user_id").isNull()) {
                QCOMPARE(identity.value("userid").toInteger(), qint64(2));
            } else {
                QCOMPARE(query.value("user_id").toInteger(), qint64(1));
                QCOMPARE(identity.value("userid").toInteger(), qint64(0));
                QVERIFY(identity.value("token").toString().isEmpty());
                QVERIFY(identity.value("t1").toString().isEmpty());
            }
        }
        QCOMPARE(gradeCalls, 2);
        const auto verification =
            ApiClientTestAccess::verificationForOperation(api, "user_grade_info", true);
        QVERIFY(verification.has_value());
        QVERIFY(!*verification);
        model.endDurationQueries();
        // Runtime cancellation completes asynchronously; ownership is cleared
        // immediately, and pending transport handles drain on terminal delivery.
        QTRY_VERIFY(([&] {
            for (const auto &request : ApiClientTestAccess::requests(api))
                if (request.value("operation").toString() == QStringLiteral("user_grade_info") &&
                    !request.value("query").toObject().value("user_id").isNull())
                    return false;
            return true;
        })());
        model.shutdown();
        storage.shutdown();
    }
    void explicitSwitchPreservesCurrentAccount_data()
    {
        QTest::addColumn<QString>("outcome");
        for (const auto &outcome :
             {"authentication", "network", "timeout", "invalid_profile", "cancel",
              "cancel_during_sql", "sql_failure", "other_login", "shutdown"})
            QTest::newRow(outcome) << QString::fromLatin1(outcome);
    }
    void explicitSwitchPreservesCurrentAccount()
    {
        QFETCH(QString, outcome);
        QTemporaryDir dir;
        const auto path = dir.filePath("switch.sqlite");
        ApiClient api;
        QVERIFY(api.setNetworkTransport(1)); // All candidate HTTP stays on loopback.
        AccountService account(api);
        StorageService storage;
        SavedAccountModel model(api, account, storage);
        QSignalSpy ready(&storage, &StorageService::accountsReady);
        storage.initialize(path);
        QTRY_COMPARE(ready.size(), 1);
        QVERIFY(account.adoptQrAuthorization(authorization(1, "candidate-token")));
        QTRY_VERIFY(!model.busy());
        QVERIFY(account.adoptQrAuthorization(authorization(2, "current-token")));
        QTRY_VERIFY(!model.busy());
        const auto revision = api.sessionRevision();
        const auto session = AccountServiceTestAccess::session(account);
        QSignalSpy adopted(&account, &AccountService::sessionAdopted);
        QSignalSpy switched(&model, &SavedAccountModel::switched);
        QSignalSpy expired(&api, &ApiClient::authenticationExpired);
        QSignalSpy changedSession(&api, &ApiClient::sessionChanged);
        if (outcome == "sql_failure") {
            const auto connection = QUuid::createUuid().toString();
            {
                auto db = QSqlDatabase::addDatabase("QSQLITE", connection);
                db.setDatabaseName(path);
                QVERIFY(db.open());
                QSqlQuery q(db);
                QVERIFY(q.exec("CREATE TRIGGER reject_switch BEFORE UPDATE ON accounts BEGIN "
                               "SELECT RAISE(ABORT,'test'); END"));
            }
            QSqlDatabase::removeDatabase(connection);
        }
        if (outcome == "cancel_during_sql")
            connect(&storage, &StorageService::saveAccountRequested, &model,
                    [&](quint64, const StoredAccount &, bool) { model.cancelSwitch(); });
        QVERIFY(model.switchAccount(1));
        const auto request = ApiClientTestAccess::candidate(api);
        QVERIFY(request);
        QVERIFY(model.busy());
        QCOMPARE(model.switchingUserId(), quint64(1));
        QCOMPARE(account.userId(), quint64(2));
        QCOMPARE(api.sessionRevision(), revision);
        QVERIFY(!model.switchAccount(1));
        QVERIFY(!model.forgetAccount(1));
        const auto candidate =
            ApiClientTestAccess::request(api, request).value("identity").toObject();
        QCOMPARE(candidate.value("userid").toInteger(), qint64(1));
        QCOMPARE(candidate.value("token").toString(), QString("candidate-token"));
        QCOMPARE(candidate.value("guid").toString(), api.deviceIdentitySnapshot().guid);
        if (outcome == "authentication" || outcome == "network") {
            ApiClientTestAccess::deliver(api, request, 5,
                                         QJsonDocument(QJsonObject{{"kind", outcome}}).toJson());
        } else if (outcome == "timeout") {
            SavedAccountModelTestAccess::timeout(model);
            emit api.succeeded(request, R"({"id":1})");
        } else if (outcome == "invalid_profile") {
            ApiClientTestAccess::validate(api, 9);
        } else if (outcome == "cancel") {
            model.cancelSwitch();
            // Even a queued logical delivery may arrive after owner cancellation.
            emit api.succeeded(request, R"({"id":1,"nickname":"Late"})");
        } else if (outcome == "other_login") {
            QVERIFY(account.adoptQrAuthorization(authorization(3, "new-login-token")));
            emit api.succeeded(request, R"({"id":1,"nickname":"Late"})");
        } else if (outcome == "shutdown") {
            model.shutdown();
            emit api.succeeded(request, R"({"id":1,"nickname":"Late"})");
        } else {
            ApiClientTestAccess::validate(api, 1);
        }
        QTRY_VERIFY(!model.busy());
        QCOMPARE(account.userId(), outcome == "other_login" ? quint64(3) : quint64(2));
        QCOMPARE(switched.size(), 0);
        QCOMPARE(expired.size(), 0);
        if (outcome != "other_login") {
            QCOMPARE(adopted.size(), 0);
            QCOMPARE(changedSession.size(), 0);
            QCOMPARE(AccountServiceTestAccess::session(account), session);
        }
        for (int row = 0; row < model.count(); ++row)
            if (model.data(model.index(row, 0), SavedAccountModel::UserIdRole).toULongLong() == 1)
                QCOMPARE(
                    model.data(model.index(row, 0), SavedAccountModel::LoginExpiredRole).toBool(),
                    outcome == "authentication");
        const auto connection = QUuid::createUuid().toString();
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", connection);
            db.setDatabaseName(path);
            QVERIFY(db.open());
            QSqlQuery q(db);
            QVERIFY(q.exec("SELECT account_id FROM account_selection WHERE singleton=1"));
            QVERIFY(q.next());
            QCOMPARE(q.value(0).toULongLong(), account.userId());
        }
        QSqlDatabase::removeDatabase(connection);
    }
    void successfulValidationCommitsOnceAndReusesProfile()
    {
        QTemporaryDir dir;
        ApiClient api;
        QVERIFY(api.setNetworkTransport(1));
        AccountService account(api);
        StorageService storage;
        SavedAccountModel model(api, account, storage);
        QSignalSpy ready(&storage, &StorageService::accountsReady);
        storage.initialize(dir.filePath("switch.sqlite"));
        QTRY_COMPARE(ready.size(), 1);
        QVERIFY(account.adoptQrAuthorization(authorization(1)));
        QTRY_VERIFY(!model.busy());
        QVERIFY(account.adoptQrAuthorization(authorization(2)));
        QTRY_VERIFY(!model.busy());
        QSignalSpy changedSession(&api, &ApiClient::sessionChanged);
        QSignalSpy switched(&model, &SavedAccountModel::switched);
        QVERIFY(model.switchAccount(1));
        QCOMPARE(account.userId(), quint64(2));
        account.applyAvatar("https://example.test/updated-avatar.png");
        QCOMPARE(model.switchingUserId(),
                 quint64(1)); // Display updates do not retire the candidate.
        ApiClientTestAccess::validate(api, 1);
        QCOMPARE(account.userId(), quint64(2)); // Wait for successful SQL readback.
        QTRY_VERIFY(!model.busy());
        QCOMPARE(account.userId(), quint64(1));
        QCOMPARE(account.nickname(), QString("Verified"));
        QVERIFY(account.profileDetailAvailable());
        QCOMPARE(changedSession.size(), 1);
        QCOMPARE(switched.size(), 1);
        account.loadProfileOnce();
        QSet<QString> operations;
        for (const auto &request : ApiClientTestAccess::requests(api))
            operations.insert(request.value("operation").toString());
        QVERIFY(!operations.contains("user_detail"));
        QVERIFY(operations.contains("user_info"));
        QVERIFY(operations.contains("user_vip_detail"));
    }
    void candidateChallengeDoesNotResumeOrCancelCurrentAccount()
    {
        for (const bool cancel : {false, true}) {
            ApiClient api;
            QVERIFY(api.setNetworkTransport(1));
            QVERIFY(api.setAccount(2, "current-token"));
            const auto candidate = api.validateAccount(1, "candidate-token", {});
            const auto current = api.userDetail();
            const QByteArray challenge =
                R"({"kind":"challenge","challenge":{"event_id":"shared-event"}})";
            ApiClientTestAccess::deliver(api, candidate, 5, challenge);
            ApiClientTestAccess::deliver(api, current, 5, challenge);
            QVERIFY(ApiClientTestAccess::suspended(api, candidate));
            QVERIFY(ApiClientTestAccess::suspended(api, current));
            if (cancel) {
                QVERIFY(api.cancel(candidate));
                QVERIFY(!ApiClientTestAccess::suspended(api, candidate));
            } else {
                ApiClientTestAccess::acknowledge(api, candidate);
                QVERIFY(ApiClientTestAccess::retry(api, candidate));
                QVERIFY(!ApiClientTestAccess::retry(api, current));
            }
            QVERIFY(ApiClientTestAccess::suspended(api, current));
            QCOMPARE(api.currentAccountUserId(), quint64(2));
        }
    }
    void candidateValidationBypassesInterceptorsAndIsolatesExpiry()
    {
        ApiClient api;
        QVERIFY(api.setNetworkTransport(1));
        QVERIFY(api.setAccount(2, "current-token"));
        int intercepted = 0;
        auto intercept = [&](const QJsonObject &, ApiInterceptNext, ApiInterceptDone done) {
            ++intercepted;
            done({0, R"({"id":1})"});
            return std::function<void()>{};
        };
        api.addInterceptor(0, intercept);
        api.addInterceptor(0, intercept, true);
        QSignalSpy expired(&api, &ApiClient::authenticationExpired);
        const auto request = api.validateAccount(1, "candidate-token", "candidate-refresh");
        QVERIFY(request);
        ApiClientTestAccess::deliver(api, request, 5, R"({"kind":"authentication"})");
        QCOMPARE(intercepted, 0);
        QCOMPARE(expired.size(), 0);
        const auto current = api.validateAccount(2, "current-token", {});
        ApiClientTestAccess::deliver(api, current, 5, R"({"kind":"authentication"})");
        QCOMPARE(expired.size(), 1);
    }
    void catalogSaveSwitchForgetAndClose()
    {
        QTemporaryDir dir;
        ApiClient api;
        AccountService account(api);
        StorageService storage;
        SavedAccountModel model(api, account, storage);
        QSignalSpy notices(&model, &SavedAccountModel::notice);
        QAbstractItemModelTester tester(&model,
                                        QAbstractItemModelTester::FailureReportingMode::QtTest);
        tester.setUseFetchMore(false);
        QSignalSpy initialized(&storage, &StorageService::initialized);
        storage.initialize(dir.filePath("data.sqlite"));
        QTRY_COMPARE(initialized.size(), 1);
        QVERIFY(account.adoptQrAuthorization(authorization(1)));
        QTRY_VERIFY(!model.busy());
        QCOMPARE(model.count(), 1);
        QVERIFY(account.adoptQrAuthorization(authorization(2)));
        QTRY_VERIFY(!model.busy());
        QCOMPARE(model.count(), 2);
        QCOMPARE(notices.size(), 0);         // Login persistence is not an explicit account switch.
        QVERIFY(api.setNetworkTransport(1)); // Local unreachable transport, no provider HTTP.
        QVERIFY(model.switchAccount(1));
        ApiClientTestAccess::validate(api, 1);
        QTRY_VERIFY(!model.busy());
        QCOMPARE(account.userId(), quint64(1));
        QCOMPARE(model.data(model.index(0, 0), SavedAccountModel::UserIdRole).toULongLong(),
                 quint64(1));
        QCOMPARE(notices.size(), 1);
        QVERIFY(notices.last()[0].toString().startsWith("已切换至 "));
        QCOMPARE(notices.last()[1].toBool(), false);
        QVERIFY(!model.forgetAccount(1));
        QCOMPARE(notices.size(), 2);
        QCOMPARE(notices.last()[1].toBool(), true);
        QVERIFY(model.forgetAccount(2));
        QTRY_VERIFY(!model.busy());
        QCOMPARE(model.count(), 1);
        QCOMPARE(notices.size(), 3);
        QCOMPARE(notices.last()[0].toString(), QString("已移除账号"));
        model.logout();
        QTRY_VERIFY(!model.busy());
        QCOMPARE(account.userId(), quint64(0));
        QCOMPARE(model.count(), 0);
        QCOMPARE(notices.size(), 3); // Logout does not claim an explicit removal.
        QSignalSpy drained(&model, &SavedAccountModel::closed);
        model.shutdown();
        QTRY_COMPARE(drained.size(), 1);
        model.shutdown();
        QCOMPARE(drained.size(), 1);
        QSignalSpy closed(&storage, &StorageService::closed);
        storage.shutdown();
        QTRY_COMPARE(closed.size(), 1);
        const auto connection = QStringLiteral("catalog-check");
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", connection);
            db.setDatabaseName(dir.filePath("data.sqlite"));
            QVERIFY(db.open());
            QSqlQuery q(db);
            QVERIFY(q.exec("PRAGMA user_version"));
            QVERIFY(q.next());
            QCOMPARE(q.value(0).toInt(), 6);
            q.finish();
            QVERIFY(q.exec("SELECT COUNT(*) FROM accounts"));
            QVERIFY(q.next());
            QCOMPARE(q.value(0).toInt(), 0);
            db.close();
        }
        QSqlDatabase::removeDatabase(connection);
    }
    void queuedSwitchCannotRestoreAfterLogout()
    {
        QTemporaryDir dir;
        ApiClient api;
        AccountService account(api);
        StorageService storage;
        SavedAccountModel model(api, account, storage);
        QSignalSpy initialized(&storage, &StorageService::initialized);
        storage.initialize(dir.filePath("data.sqlite"));
        QTRY_COMPARE(initialized.size(), 1);
        QVERIFY(account.adoptQrAuthorization(authorization(1)));
        QTRY_VERIFY(!model.busy());
        QVERIFY(account.adoptQrAuthorization(authorization(2)));
        QTRY_VERIFY(!model.busy());
        QVERIFY(model.switchAccount(1));
        model.logout();
        QTRY_VERIFY(!model.busy());
        QCOMPARE(account.userId(), quint64(0));
        QCOMPARE(model.count(), 1);
        model.shutdown();
        storage.shutdown();
    }
    void explicitLoginWinsOverPendingStartupRestoration()
    {
        QTemporaryDir dir;
        const auto path = dir.filePath("data.sqlite");
        {
            StorageService seed;
            QSignalSpy ready(&seed, &StorageService::accountsReady);
            seed.initialize(path);
            QTRY_COMPARE(ready.size(), 1);
            seed.saveAccount(1, {1, "Saved", {}, "startup-token", 1}, true);
            QTRY_COMPARE(ready.size(), 2);
        }
        for (const bool beforeCatalog : {true, false}) {
            ApiClient api;
            AccountService account(api);
            StorageService storage;
            SavedAccountModel model(api, account, storage);
            if (beforeCatalog)
                model.suppressStartupRestoration();
            else
                connect(&storage, &StorageService::accountsReady, &model,
                        [&](quint64 id, const StoredAccounts &, quint64) {
                            if (!id)
                                model.suppressStartupRestoration();
                        });
            QSignalSpy initialized(&storage, &StorageService::initialized);
            storage.initialize(path);
            QTRY_COMPARE(initialized.size(), 1);
            QTRY_VERIFY(!model.busy());
            QCOMPARE(account.userId(), quint64(0));
            QCOMPARE(model.count(), 1);
        }
    }
    void deviceRegistrationRebindsOnlyTheSameAccount()
    {
        ApiClient api;
        AccountService account(api);
        QSignalSpy rebound(&account, &AccountService::requestContextChanged);
        QVERIFY(account.adoptQrAuthorization(authorization(1)));
        const auto revision = api.sessionRevision();
        QVERIFY(api.setDeviceIdentity(DeviceIdentity::temporary()));
        QVERIFY(api.sessionRevision() > revision);
        QCOMPARE(account.userId(), quint64(1));
        QCOMPARE(rebound.size(), 1);
        account.signOut();
        QVERIFY(api.setDeviceIdentity(DeviceIdentity::temporary()));
        QCOMPARE(account.userId(), quint64(0));
        QCOMPARE(rebound.size(), 1);
    }
    void heldStartupAccountOnlyAdoptsWhenReleasedAndQuitDrains()
    {
        for (const bool release : {true, false}) {
            QTemporaryDir directory;
            const auto path = directory.filePath("held.sqlite");
            {
                StorageService seed;
                QSignalSpy ready(&seed, &StorageService::accountsReady);
                seed.initialize(path);
                QTRY_COMPARE(ready.size(), 1);
                seed.saveAccount(1, {42, "Saved", {}, "fixture-token", 1, {}, "fixture-refresh", 0},
                                 true);
                QTRY_COMPARE(ready.size(), 2);
            }
            ApiClient api;
            AccountService account(api);
            StorageService storage;
            SavedAccountModel model(api, account, storage);
            model.setStartupHeld(true);
            QSignalSpy initialized(&storage, &StorageService::initialized);
            QSignalSpy adopted(&account, &AccountService::sessionAdopted);
            storage.initialize(path);
            QTRY_COMPARE(initialized.size(), 1);
            QCOMPARE(model.count(), 1);
            QVERIFY(model.busy());
            QCOMPARE(account.userId(), quint64(0));
            QTest::qWait(50);
            QCOMPARE(adopted.size(), 0);
            if (release) {
                model.setStartupHeld(false);
                QTRY_COMPARE(account.userId(), quint64(42));
                QTRY_VERIFY(!model.busy());
                QCOMPARE(adopted.size(), 1);
            }
            QSignalSpy closed(&model, &SavedAccountModel::closed);
            model.shutdown();
            QTRY_COMPARE(closed.size(), 1);
            QCOMPARE(account.userId(), release ? quint64(42) : quint64(0));
            QCOMPARE(adopted.size(), release ? 1 : 0);
        }
    }
    void restartRestoresTokenRefreshAndSelectedAccount()
    {
        QTemporaryDir dir;
        const auto path = dir.filePath("data.sqlite");
        {
            ApiClient api;
            AccountService account(api);
            StorageService storage;
            SavedAccountModel model(api, account, storage);
            QSignalSpy initialized(&storage, &StorageService::initialized);
            storage.initialize(path);
            QTRY_COMPARE(initialized.size(), 1);
            QVERIFY(account.adoptQrAuthorization(authorization(1, "token-1")));
            QTRY_VERIFY(!model.busy());
            auto session = QJsonDocument::fromJson(authorization(2, "token-2")).object();
            session.insert("refresh_credential", "refresh-2");
            session.insert("expires_at_seconds", 9876543210LL);
            QVERIFY(account.adoptSession(QJsonDocument(session).toJson()));
            QTRY_VERIFY(!model.busy());
        }
        ApiClient api;
        AccountService account(api);
        StorageService storage;
        SavedAccountModel model(api, account, storage);
        QList<bool> startupAdoptions;
        connect(&account, &AccountService::sessionAdopted, &model,
                [&] { startupAdoptions.append(model.restoringStartupAccount()); });
        storage.initialize(path);
        QTRY_COMPARE(account.userId(), quint64(2));
        QTRY_VERIFY(!model.busy());
        QCOMPARE(startupAdoptions, QList<bool>{true});
        QVERIFY(!model.restoringStartupAccount());
        const auto session =
            QJsonDocument::fromJson(AccountServiceTestAccess::session(account)).object();
        QCOMPARE(session.value("token").toString(), QString("token-2"));
        QCOMPARE(session.value("refresh_credential").toString(), QString("refresh-2"));
        QCOMPARE(session.value("expires_at_seconds").toInteger(), qint64(9876543210LL));
        QVERIFY(api.setNetworkTransport(1)); // Local unreachable transport, no provider HTTP.
        QVERIFY(model.switchAccount(1));
        ApiClientTestAccess::validate(api, 1);
        QTRY_VERIFY(!model.busy());
        QCOMPARE(account.userId(), quint64(1));
        QCOMPARE(startupAdoptions, (QList<bool>{true, false}));
        const auto qr =
            QJsonDocument::fromJson(AccountServiceTestAccess::session(account)).object();
        QCOMPARE(qr.value("token").toString(), QString("token-1"));
        QVERIFY(qr.value("refresh_credential").toString().isEmpty());
        QCOMPARE(qr.value("expires_at_seconds").toInteger(), qint64(0));
    }
    void sqlFailureDoesNotReplacePreviousAccount()
    {
        QTemporaryDir dir;
        const auto path = dir.filePath("data.sqlite");
        ApiClient api;
        AccountService account(api);
        StorageService storage;
        SavedAccountModel model(api, account, storage);
        QSignalSpy initialized(&storage, &StorageService::initialized);
        storage.initialize(path);
        QTRY_COMPARE(initialized.size(), 1);
        QVERIFY(account.adoptQrAuthorization(authorization(1, "first-token")));
        QTRY_VERIFY(!model.busy());
        const QString name = "reject-account-write";
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", name);
            db.setDatabaseName(path);
            QVERIFY(db.open());
            QSqlQuery q(db);
            QVERIFY(q.exec("CREATE TRIGGER reject_update BEFORE UPDATE ON accounts BEGIN SELECT "
                           "RAISE(ABORT,'test'); END"));
        }
        QSqlDatabase::removeDatabase(name);
        QVERIFY(account.adoptQrAuthorization(authorization(1, "replacement-token")));
        QTRY_VERIFY(!model.busy());
        QVERIFY(!model.errorMessage().isEmpty());
        QCOMPARE(model.count(), 1);
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", name);
            db.setDatabaseName(path);
            QVERIFY(db.open());
            QSqlQuery q(db);
            QVERIFY(q.exec("SELECT token FROM accounts WHERE user_id=1"));
            QVERIFY(q.next());
            QCOMPARE(q.value(0).toString(), QString("first-token"));
        }
        QSqlDatabase::removeDatabase(name);
    }
};
QTEST_MAIN(AccountPersistenceTest)
#include "AccountPersistenceTest.moc"
