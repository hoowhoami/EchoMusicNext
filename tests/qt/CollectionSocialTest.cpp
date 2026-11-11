#include "LibraryCollectionService.h"
#include "ProfileService.h"
#include <QJsonDocument>
#include <QScopeGuard>
#include <iostream>
#include <iterator>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QtTest>
using namespace EchoMusic;
namespace EchoMusic
{
class ApiClientTestAccess
{
public:
    static void expireMessageCooldown(ApiClient &api)
    {
        api.m_messageSendDeadline = QDeadlineTimer();
        api.updateMessageSendCooldown();
    }
    static QJsonObject request(ApiClient &a, quint64 id)
    {
        return a.m_pending.value(id).request;
    }
    static bool verifies(ApiClient &a, quint64 id)
    {
        return a.m_pending.value(id).automaticVerification;
    }
};
class AccountServiceTestAccess
{
public:
    static bool adopt(AccountService &a)
    {
        return a.adoptLoginAuthorization(
            R"({"user_id":900001,"token":"isolated-test-token","nickname":"Test"})");
    }
};
class UserPlaylistModelTestAccess
{
public:
    static void seed(UserPlaylistModel &m, ApiClient &a, const QJsonArray &items)
    {
        m.beginResetModel();
        m.m_rows.clear();
        for (const auto &item : items)
            m.m_rows.append(UserPlaylistModel::readRow(item.toObject()));
        m.m_sessionRevision = a.sessionRevision();
        m.m_total = quint64(items.size());
        m.m_exhausted = true;
        m.m_state = UserPlaylistModel::State::Ready;
        m.endResetModel();
        emit m.changed();
    }
    static void seed(UserPlaylistModel &m, ApiClient &a, int source = 1)
    {
        m.beginResetModel();
        m.m_rows.clear();
        UserPlaylistModel::Row row;
        row.cloudId = {122};
        row.globalId = "local-122";
        row.creatorGlobalId = "public-playlist";
        row.creatorListId = source == 2 ? 960399 : 287;
        row.creatorId = 42;
        row.title = "Test";
        row.sourceCode = quint32(source);
        row.kind = "collected";
        row.collected = true;
        m.m_rows.append(row);
        m.m_sessionRevision = a.sessionRevision();
        m.m_total = 1;
        m.m_exhausted = true;
        m.m_state = UserPlaylistModel::State::Ready;
        m.endResetModel();
        emit m.changed();
    }
};
class PlaylistMutationServiceTestAccess
{
public:
    static void accept(PlaylistMutationService &s, const char *v)
    {
        s.succeed(s.m_request, v);
    }
    static void accept(PlaylistMutationService &s, quint64 id, const char *v)
    {
        s.succeed(id, v);
    }
    static bool hasRefresh(const PlaylistMutationService &s)
    {
        return s.m_refresh.has_value() || s.m_refreshRequest != 0;
    }
    static quint64 request(PlaylistMutationService &s)
    {
        return s.m_request;
    }
    static void reject(PlaylistMutationService &s)
    {
        const auto id = s.m_request;
        s.fail(id, R"({"kind":"business"})");
    }
};
class ContentBlacklistTestAccess
{
public:
    static quint64 read(ContentBlacklistService &s)
    {
        return s.m_buckets[1].request;
    }
    static quint64 write(ContentBlacklistService &s)
    {
        return s.m_singerWrite;
    }
    static void accept(ContentBlacklistService &s, quint64 id, const char *v)
    {
        s.succeeded(id, v);
    }
};
class ProfileServiceTestAccess
{
public:
    static quint64 followRead(const ProfileService &s)
    {
        return s.m_social[1].request;
    }
    static bool observedArtist(const ProfileService &s, quint64 id)
    {
        for (const auto &v : s.m_social[1].model->rows())
            if (quint64(v.toObject()["artist_id"].toInteger()) == id)
                return true;
        return false;
    }
    static void seed(ProfileService &s, int tab, QJsonArray rows)
    {
        auto &t = s.m_social[tab];
        const auto old = std::exchange(t.request, 0);
        if (old)
            s.m_api.cancel(old);
        t.loaded = true;
        t.model->replace(rows);
        emit s.changed();
    }
    static quint64 write(ProfileService &s)
    {
        return s.m_followWrites.isEmpty() ? 0 : s.m_followWrites.keys().first();
    }
    static quint64 chatRead(ProfileService &s)
    {
        return s.m_chatRead;
    }
    static quint64 chatWrite(ProfileService &s)
    {
        return s.m_chatWrite;
    }
    static void accept(ProfileService &s, quint64 id, const char *v)
    {
        s.succeeded(id, v);
    }
    static void fail(ProfileService &s, quint64 id)
    {
        s.failed(id, R"({"kind":"transport"})");
    }
};
} // namespace EchoMusic
class CollectionSocialTest : public QObject
{
    Q_OBJECT
    QTcpServer sink;
private slots:
    void initTestCase()
    {
        QVERIFY(sink.listen(QHostAddress::LocalHost));
        connect(&sink, &QTcpServer::newConnection, this, [this] {
            while (sink.hasPendingConnections()) {
                auto *s = sink.nextPendingConnection();
                connect(s, &QTcpSocket::readyRead, s, [s] {
                    s->readAll();
                    s->write("HTTP/1.1 503 Test isolation\r\nContent-Length: 0\r\n\r\n");
                    s->disconnectFromHost();
                });
                connect(s, &QTcpSocket::disconnected, s, &QObject::deleteLater);
            }
        });
    }
    void liveCollectionAndArtistThroughNativeServices()
    {
        if (!qEnvironmentVariableIsSet("ECHO_LIVE_COLLECTION_SOCIAL"))
            QSKIP("Explicit current-account native probe");
        const std::string input((std::istreambuf_iterator<char>(std::cin)),
                                std::istreambuf_iterator<char>());
        const auto identity = QJsonDocument::fromJson(QByteArray::fromStdString(input)).object();
        QVERIFY(!identity.isEmpty());
        ApiClient api(DeviceIdentity{identity["mid"].toString(), identity["dfid"].toString(),
                                     identity["guid"].toString(), identity["mac"].toString(),
                                     identity["device_name"].toString(), "-", "-"});
        api.setNetworkTimeout(25);
        AccountService account(api);
        ProfileService profile(api, account);
        const QJsonObject session{{"user_id", identity["userid"]},
                                  {"token", identity["token"]},
                                  {"refresh_credential", identity["t1"]}};
        QVERIFY(account.adoptLoginAuthorization(QJsonDocument(session).toJson()));
        QSignalSpy successes(&api, &ApiClient::succeeded), failures(&api, &ApiClient::failed);
        auto read = [&](quint64 id) -> QJsonDocument {
            QElapsedTimer timer;
            timer.start();
            while (timer.elapsed() < 30000) {
                for (const auto &r : successes)
                    if (r[0].toULongLong() == id)
                        return QJsonDocument::fromJson(r[1].toByteArray());
                for (const auto &r : failures)
                    if (r[0].toULongLong() == id)
                        return {};
                QTest::qWait(10);
            }
            return {};
        };
        UserPlaylistModel library(api);
        PlaylistMutationService mutations(api);
        LibraryCollectionService collections(api, library, mutations);
        connect(&mutations, &PlaylistMutationService::libraryInvalidated, &library,
                &UserPlaylistModel::reload);
        library.reload();
        QTRY_COMPARE_WITH_TIMEOUT(library.state(), UserPlaylistModel::State::Ready, 30000);
        collections.ensureLoaded();
        QTRY_VERIFY_WITH_TIMEOUT(!collections.busy(), 30000);
        auto ids = [&] {
            QSet<quint64> result;
            for (int row = 0; row < library.rowCount(); ++row)
                result.insert(
                    library.index(row, 0).data(UserPlaylistModel::CloudListIdRole).toULongLong());
            return result;
        };
        const auto baseline = ids();
        const auto playlist = read(api.playlistDetails(
            {ApiClient::PublicCollectionId{QStringLiteral("collection_3_2132029040_287_0")}}));
        QVERIFY(playlist.isArray());
        QVERIFY(!playlist.array().isEmpty());
        const auto album = read(api.albumDetail(960399));
        QVERIFY(album.isObject());
        for (const auto &target : QList<QPair<bool, QJsonObject>>{
                 {false, playlist.array().first().toObject()}, {true, album.object()}}) {
            if (collections.collected(target.first, target.second))
                continue;
            bool restored = false;
            const auto cleanup = qScopeGuard([&] {
                if (restored)
                    return;
                if (mutations.state() == PlaylistMutationService::State::Ready &&
                    collections.collected(target.first, target.second)) {
                    collections.toggle(target.first, target.second);
                    QElapsedTimer t;
                    t.start();
                    while (collections.busy() && t.elapsed() < 60000)
                        QTest::qWait(20);
                }
            });
            collections.toggle(target.first, target.second);
            QTRY_VERIFY_WITH_TIMEOUT(!collections.busy(), 60000);
            QCOMPARE(mutations.state(), PlaylistMutationService::State::Ready);
            QVERIFY(collections.collected(target.first, target.second));
            collections.toggle(target.first, target.second);
            QTRY_VERIFY_WITH_TIMEOUT(!collections.busy(), 60000);
            QCOMPARE(mutations.state(), PlaylistMutationService::State::Ready);
            QVERIFY(!collections.collected(target.first, target.second));
            restored = true;
        }
        QCOMPARE(ids(), baseline);
        profile.ensureArtistFollows();
        QTRY_VERIFY_WITH_TIMEOUT(profile.artistFollowsLoaded(), 30000);
        quint64 chosen = 0;
        for (quint64 id : {3060ULL, 1573ULL, 1524ULL})
            if (!profile.artistFollowed(id)) {
                chosen = id;
                break;
            }
        QVERIFY(chosen);
        bool restored = false;
        const auto restore = qScopeGuard([&] {
            if (restored)
                return;
            if (profile.artistFollowed(chosen) && !profile.artistPending(chosen)) {
                profile.toggleArtistFollow(chosen);
                QElapsedTimer t;
                t.start();
                while (profile.artistPending(chosen) && t.elapsed() < 30000)
                    QTest::qWait(20);
            }
        });
        profile.toggleArtistFollow(chosen);
        QTRY_VERIFY_WITH_TIMEOUT(!profile.artistPending(chosen), 30000);
        QVERIFY(profile.artistFollowed(chosen));
        QTRY_COMPARE_WITH_TIMEOUT(ProfileServiceTestAccess::followRead(profile), quint64(0), 30000);
        QVERIFY(ProfileServiceTestAccess::observedArtist(profile, chosen));
        profile.toggleArtistFollow(chosen);
        QTRY_VERIFY_WITH_TIMEOUT(!profile.artistPending(chosen), 30000);
        QVERIFY(!profile.artistFollowed(chosen));
        QTRY_COMPARE_WITH_TIMEOUT(ProfileServiceTestAccess::followRead(profile), quint64(0), 30000);
        QVERIFY(!ProfileServiceTestAccess::observedArtist(profile, chosen));
        restored = true;
        qInfo("Native collection add/remove and artist follow/unfollow restored their baselines");
    }
    void canonicalCollectionIdentityAndRejectedWrite()
    {
        ApiClient api;
        QVERIFY(api.setNetworkTransport(sink.serverPort()));
        QVERIFY(api.setAccount(900001, "isolated-test-token"));
        UserPlaylistModel library(api);
        PlaylistMutationService mutations(api);
        LibraryCollectionService collections(api, library, mutations);
        const QJsonObject d{{"title", "Test"},
                            {"creator_id", 42},
                            {"creator_list_id", 287},
                            {"global_id", "public-playlist"},
                            {"source_code", 1}};
        UserPlaylistModelTestAccess::seed(library, api);
        QVERIFY(collections.available(false, d));
        QVERIFY(collections.collected(false, d));
        QVERIFY(!collections.collected(true, QJsonObject{{"id", 960399}}));
        QSignalSpy notices(&collections, &LibraryCollectionService::notice);
        collections.toggle(false, d);
        const auto id = PlaylistMutationServiceTestAccess::request(mutations);
        QVERIFY(id);
        QVERIFY(collections.busy());
        auto req = ApiClientTestAccess::request(api, id);
        QCOMPARE(req["operation"].toString(), QString("playlist_del"));
        QCOMPARE(req["query"].toObject()["cloud_list_id"].toInteger(), 122);
        QVERIFY(!ApiClientTestAccess::verifies(api, id));
        collections.toggle(false, d);
        QCOMPARE(PlaylistMutationServiceTestAccess::request(mutations), id);
        PlaylistMutationServiceTestAccess::reject(mutations);
        QVERIFY(!collections.busy());
        QVERIFY(collections.collected(false, d));
        QVERIFY(notices.count() > 0);
        // Optional ACK counts do not prevent the canonical readback.
        collections.toggle(false, d);
        PlaylistMutationServiceTestAccess::accept(
            mutations, R"({"user_id":900001,"cloud_list_id":122,"total_revision":2})");
        QCOMPARE(mutations.state(), PlaylistMutationService::State::Ready);
        PlaylistMutationServiceTestAccess::accept(
            mutations, R"({"user_id":900001,"total_revision":2,"page":{"items":[],"total":0}})");
        QCOMPARE(mutations.state(), PlaylistMutationService::State::Ready);

        UserPlaylistModelTestAccess::seed(library, api, 2);
        QVERIFY(collections.collected(true, QJsonObject{{"id", 960399}}));
        QVERIFY(!collections.collected(false, d));
        api.setAccount(0, {});
        QVERIFY(!collections.available(true, QJsonObject{{"id", 960399}}));
        QVERIFY(!collections.collected(true, QJsonObject{{"id", 960399}}));
    }
    void playlistMembershipUsesOnlyGlobalIds_data()
    {
        QTest::addColumn<QJsonObject>("detail");
        QTest::addColumn<bool>("expected");
        QTest::newRow("library-gid") << QJsonObject{{"global_id", "local-122"}} << true;
        QTest::newRow("original-gid") << QJsonObject{{"global_id", "public-playlist"}} << true;
        QTest::newRow("creator-to-library-gid")
            << QJsonObject{{"creator_global_id", "local-122"}} << true;
        QTest::newRow("creator-to-original-gid")
            << QJsonObject{{"global_id", "rewritten-gid"}, {"creator_global_id", "public-playlist"}}
            << true;
        QTest::newRow("cloud-id-collision")
            << QJsonObject{{"id", 122}, {"global_id", "another-playlist"}} << false;
        QTest::newRow("creator-list-id-collision")
            << QJsonObject{{"creator_list_id", 287}, {"global_id", "another-playlist"}} << false;
        QTest::newRow("global-id-collides-with-local-id")
            << QJsonObject{{"global_id", "122"}} << false;
        QTest::newRow("local-id-collides-with-global-id")
            << QJsonObject{{"id", "public-playlist"}} << false;
        QTest::newRow("missing-gids") << QJsonObject{{"creator_list_id", 287}} << false;
        QTest::newRow("empty-gids")
            << QJsonObject{{"global_id", ""}, {"creator_global_id", ""}} << false;
    }
    void playlistMembershipUsesOnlyGlobalIds()
    {
        QFETCH(QJsonObject, detail);
        QFETCH(bool, expected);
        ApiClient api;
        QVERIFY(api.setNetworkTransport(sink.serverPort()));
        QVERIFY(api.setAccount(900001, "isolated-test-token"));
        UserPlaylistModel library(api);
        PlaylistMutationService mutations(api);
        LibraryCollectionService collections(api, library, mutations);
        UserPlaylistModelTestAccess::seed(library, api);
        QCOMPARE(collections.collected(false, detail), expected);
        UserPlaylistModelTestAccess::seed(library, api, 2);
        QVERIFY(!collections.collected(false, detail));
    }
    void playlistToggleDoesNotDeleteCollidingCollection()
    {
        ApiClient api;
        QVERIFY(api.setNetworkTransport(sink.serverPort()));
        QVERIFY(api.setAccount(900001, "isolated-test-token"));
        UserPlaylistModel library(api);
        PlaylistMutationService mutations(api);
        LibraryCollectionService collections(api, library, mutations);
        const QJsonObject detail{{"title", "Target"},
                                 {"creator_id", 77},
                                 {"creator_list_id", 287},
                                 {"global_id", "target-gid"},
                                 {"source_code", 1}};
        UserPlaylistModelTestAccess::seed(library, api);
        collections.toggle(false, detail);
        auto id = PlaylistMutationServiceTestAccess::request(mutations);
        QVERIFY(id);
        QCOMPARE(ApiClientTestAccess::request(api, id)["operation"].toString(),
                 QString("playlist_add"));
        PlaylistMutationServiceTestAccess::reject(mutations);

        // The first entry shares only its creator-local number with the target.
        // A later GID match must select that row's account-local deletion ID.
        UserPlaylistModelTestAccess::seed(
            library, api,
            QJsonArray{QJsonObject{{"cloud_list_id", 122},
                                   {"creator_list_id", 287},
                                   {"creator_id", 42},
                                   {"global_id", "unrelated-library-gid"},
                                   {"creator_global_id", "unrelated-public-gid"},
                                   {"source_code", 1},
                                   {"kind", "collected"}},
                       QJsonObject{{"cloud_list_id", 456},
                                   {"creator_list_id", 287},
                                   {"creator_id", 77},
                                   {"global_id", "target-library-gid"},
                                   {"creator_global_id", "target-gid"},
                                   {"source_code", 1},
                                   {"kind", "collected"}}});
        QVERIFY(collections.collected(false, detail));
        collections.toggle(false, detail);
        id = PlaylistMutationServiceTestAccess::request(mutations);
        QVERIFY(id);
        const auto request = ApiClientTestAccess::request(api, id);
        QCOMPARE(request["operation"].toString(), QString("playlist_del"));
        QCOMPARE(request["query"].toObject()["cloud_list_id"].toInteger(), 456);
        PlaylistMutationServiceTestAccess::reject(mutations);

        // Even a matching GID must not turn the current user's playlist into a collection.
        auto own = detail;
        own["global_id"] = "public-playlist";
        own["creator_id"] = 900001;
        UserPlaylistModelTestAccess::seed(library, api,
                                          QJsonArray{QJsonObject{{"cloud_list_id", 122},
                                                                 {"creator_id", 900001},
                                                                 {"global_id", "public-playlist"},
                                                                 {"source_code", 1},
                                                                 {"kind", "owned"}}});
        QVERIFY(!collections.collected(false, own));
        QVERIFY(!collections.available(false, own));
    }
    void playlistMissingGidsRequireExactCreatorIdentity_data()
    {
        QTest::addColumn<QJsonObject>("detail");
        QTest::addColumn<QJsonObject>("entry");
        QTest::addColumn<bool>("expected");
        const QJsonObject detail{{"creator_id", 42}, {"creator_list_id", 287}};
        const QJsonObject entry{{"cloud_list_id", 122},
                                {"creator_id", 42},
                                {"creator_list_id", 287},
                                {"source_code", 1},
                                {"kind", "collected"}};
        QTest::newRow("exact-pair-without-gids") << detail << entry << true;
        for (const auto *field : {"creator_id", "creator_list_id"}) {
            auto different = entry;
            different[field] = 999;
            QTest::newRow(qPrintable(QString("different-%1").arg(field)))
                << detail << different << false;
            auto missing = detail;
            missing.remove(field);
            QTest::newRow(qPrintable(QString("missing-%1").arg(field)))
                << missing << entry << false;
        }
        for (const auto *field : {"global_id", "creator_global_id"}) {
            auto detailWithGid = detail;
            detailWithGid[field] = "public-gid";
            auto entryWithGid = entry;
            entryWithGid[field] = "different-gid";
            QTest::newRow(qPrintable(QString("detail-has-%1").arg(field)))
                << detailWithGid << entry << false;
            QTest::newRow(qPrintable(QString("entry-has-%1").arg(field)))
                << detail << entryWithGid << false;
            QTest::newRow(qPrintable(QString("conflicting-%1").arg(field)))
                << detailWithGid << entryWithGid << false;
        }
    }
    void playlistMissingGidsRequireExactCreatorIdentity()
    {
        QFETCH(QJsonObject, detail);
        QFETCH(QJsonObject, entry);
        QFETCH(bool, expected);
        ApiClient api;
        QVERIFY(api.setNetworkTransport(sink.serverPort()));
        QVERIFY(api.setAccount(900001, "isolated-test-token"));
        UserPlaylistModel library(api);
        PlaylistMutationService mutations(api);
        LibraryCollectionService collections(api, library, mutations);
        UserPlaylistModelTestAccess::seed(library, api, QJsonArray{entry});
        QCOMPARE(collections.collected(false, detail), expected);
        if (expected) {
            QVERIFY(collections.available(false, detail));
            collections.toggle(false, detail);
            const auto id = PlaylistMutationServiceTestAccess::request(mutations);
            QVERIFY(id);
            const auto request = ApiClientTestAccess::request(api, id);
            QCOMPARE(request["operation"].toString(), QString("playlist_del"));
            QCOMPARE(request["query"].toObject()["cloud_list_id"].toInteger(), 122);
            PlaylistMutationServiceTestAccess::reject(mutations);
        }
    }
    void playlistUnfavoriteIgnoresLateSuccessAfterAccountChange()
    {
        ApiClient api;
        QVERIFY(api.setNetworkTransport(sink.serverPort()));
        QVERIFY(api.setAccount(900001, "isolated-test-token"));
        UserPlaylistModel library(api);
        PlaylistMutationService mutations(api);
        LibraryCollectionService collections(api, library, mutations);
        UserPlaylistModelTestAccess::seed(library, api);
        const QJsonObject detail{
            {"creator_id", 42}, {"creator_list_id", 287}, {"global_id", "public-playlist"}};
        collections.toggle(false, detail);
        const auto id = PlaylistMutationServiceTestAccess::request(mutations);
        QVERIFY(id);
        const auto request = ApiClientTestAccess::request(api, id);
        QCOMPARE(request["operation"].toString(), QString("playlist_del"));
        QCOMPARE(request["query"].toObject()["cloud_list_id"].toInteger(), 122);

        QVERIFY(api.setAccount(900002, "another-isolated-test-token"));
        UserPlaylistModelTestAccess::seed(library, api);
        QSignalSpy notices(&collections, &LibraryCollectionService::notice);
        QSignalSpy completed(&mutations, &PlaylistMutationService::completed);
        QSignalSpy reconciled(&mutations, &PlaylistMutationService::libraryReconciled);
        PlaylistMutationServiceTestAccess::accept(
            mutations, id, R"({"user_id":900001,"cloud_list_id":122,"total_revision":2})");
        QVERIFY(notices.isEmpty());
        QVERIFY(completed.isEmpty());
        QVERIFY(reconciled.isEmpty());
        QVERIFY(!PlaylistMutationServiceTestAccess::hasRefresh(mutations));
        QVERIFY(!collections.writingCollection());
        QVERIFY(collections.collected(false, detail));
        QCOMPARE(library.rowCount(), 1);
    }
    void sharedSocialStateAndAccountIsolation()
    {
        ApiClient api;
        QVERIFY(api.setNetworkTransport(sink.serverPort()));
        AccountService account(api);
        ProfileService profile(api, account);
        QVERIFY(AccountServiceTestAccess::adopt(account));
        const QJsonObject user{{"key", "user:77"},
                               {"user_id", 77},
                               {"nickname", "Friend"},
                               {"following", true},
                               {"mutual", true}};
        ProfileServiceTestAccess::seed(profile, 0, {user});
        ProfileServiceTestAccess::seed(
            profile, 1,
            {user, QJsonObject{{"key", "artist:3060"}, {"user_id", 0}, {"artist_id", 3060}}});
        ProfileServiceTestAccess::seed(profile, 2, {user});
        QVERIFY(profile.artistFollowed(3060));
        profile.toggleSocialFollow("user:77");
        const auto id = ProfileServiceTestAccess::write(profile);
        QVERIFY(id);
        QVERIFY(profile.socialPending(77));
        profile.toggleSocialFollow("user:77");
        QCOMPARE(ProfileServiceTestAccess::write(profile), id);
        const auto req = ApiClientTestAccess::request(api, id);
        QCOMPARE(req["operation"].toString(), QString("user_follow_del"));
        QCOMPARE(req["id"].toInteger(), 77);
        QVERIFY(!ApiClientTestAccess::verifies(api, id));
        ProfileServiceTestAccess::accept(profile, id,
                                         R"({"following":false,"mutual":null,"already":false})");
        QVERIFY(!profile.socialPending(77));
        QCOMPARE(profile.socialUsers()->count(), 0);
        // Artist-only rows never become user write or chat targets.
        profile.openSocial(ProfileService::SocialTab::Follow);
        profile.openChat("artist:3060");
        QVERIFY(!profile.chatActive());
        account.signOut();
        ProfileServiceTestAccess::accept(profile, id, R"({"following":true})");
        QCOMPARE(profile.socialUsers()->count(), 0);
        QVERIFY(!profile.artistFollowed(3060));
    }
    void singerMembershipWaitsForEveryPageAndUsesSharedModel()
    {
        ApiClient api;
        QVERIFY(api.setNetworkTransport(sink.serverPort()));
        AccountService account(api);
        ProfileService profile(api, account);
        QVERIFY(AccountServiceTestAccess::adopt(account));
        auto &blacklist = *profile.blacklist();
        QCOMPARE(blacklist.singerStatus(3060), ContentBlacklistService::SongStatus::Unknown);
        blacklist.toggleSinger(3060, "Artist");
        QVERIFY(blacklist.singerBusy());
        auto read = ContentBlacklistTestAccess::read(blacklist);
        QVERIFY(read);
        ContentBlacklistTestAccess::accept(
            blacklist, read,
            R"({"items":[{"key":"3060","name":"Artist"}],"total":2,"page":1,"page_size":1})");
        QVERIFY(!ContentBlacklistTestAccess::write(blacklist));
        QCOMPARE(blacklist.singerStatus(3060), ContentBlacklistService::SongStatus::Unknown);
        read = ContentBlacklistTestAccess::read(blacklist);
        QVERIFY(read);
        ContentBlacklistTestAccess::accept(
            blacklist, read,
            R"({"items":[{"key":"77","name":"Other"}],"total":2,"page":2,"page_size":1})");
        const auto write = ContentBlacklistTestAccess::write(blacklist);
        QVERIFY(write);
        const auto q = ApiClientTestAccess::request(api, write)["query"].toObject();
        QCOMPARE(q["label"].toString(), QString("singer"));
        QCOMPARE(q["key"].toString(), QString("3060"));
        QVERIFY(q["remove"].toBool());
        ContentBlacklistTestAccess::accept(blacklist, write, "{}");
        QVERIFY(!blacklist.singerBusy());
        QCOMPARE(blacklist.singerStatus(3060), ContentBlacklistService::SongStatus::Absent);
        blacklist.selectCategory(1);
        QCOMPARE(blacklist.entries()->count(), 1);
        QCOMPARE(blacklist.entries()->index(0, 0).data(ContentBlacklistModel::Key).toString(),
                 QString("77"));
    }
    void messageCooldownCoversEveryWriteAndSurvivesFailure()
    {
        ApiClient api;
        QVERIFY(api.setNetworkTransport(sink.serverPort()));
        QVERIFY(api.setAccount(900001, "isolated-cooldown"));
        ApiClient::CommentWrite comment;
        comment.id = "42";
        comment.content = "text";
        comment.commentId = "1";
        ListenChatRequest room;
        room.action = ListenChatAction::Send;
        room.roomId = "test-room";
        room.text = "text";
        const QList<std::function<quint64()>> writes{
            [&] { return api.sendSocialMessage(77, {}, "text", "Test"); },
            [&] {
                comment.resource = ApiClient::CommentResource::Music;
                return api.sendComment(comment);
            },
            [&] {
                comment.resource = ApiClient::CommentResource::Album;
                return api.sendComment(comment);
            },
            [&] {
                comment.resource = ApiClient::CommentResource::Playlist;
                return api.sendComment(comment);
            },
            [&] { return api.replyComment(comment); },
            [&] { return api.sendSongBarrage("105A2705BEEE36A8BED029A8E1A6311D", "test", "text"); },
            [&] {
                return api.sendVideoBarrage("105A2705BEEE36A8BED029A8E1A6311D", "test", "text");
            },
            [&] { return api.listenTogetherChat(room); }};
        QSignalSpy changed(&api, &ApiClient::messageSendCooldownChanged);
        QSignalSpy failed(&api, &ApiClient::failed);
        const auto first = writes[0]();
        QVERIFY(first);
        QCOMPARE(api.messageSendCooldownSeconds(), 3);
        for (const auto &write : writes)
            QCOMPARE(write(), quint64(0));
        QTRY_VERIFY_WITH_TIMEOUT(!failed.isEmpty(), 2000);
        QVERIFY(api.messageSendCooldownSeconds() > 0);
        QTRY_COMPARE_WITH_TIMEOUT(api.messageSendCooldownSeconds(), 0, 4000);
        QVERIFY(changed.size() >= 3);
        for (const auto &write : writes) {
            ApiClientTestAccess::expireMessageCooldown(api);
            const auto id = write();
            QVERIFY(id);
            QCOMPARE(api.messageSendCooldownSeconds(), 3);
            for (const auto &blocked : writes)
                QCOMPARE(blocked(), quint64(0));
            const auto read = api.socialMessages(77, {});
            QVERIFY(read);
            api.cancel(read);
            api.cancel(id);
            QVERIFY(api.messageSendCooldownSeconds() > 0);
        }
        QVERIFY(api.setAccount(900001, "refreshed-token"));
        QVERIFY(api.messageSendCooldownSeconds() > 0);
        QVERIFY(api.setAccount(900002, "other-account"));
        QCOMPARE(api.messageSendCooldownSeconds(), 0);
        QVERIFY(writes[0]());
        quint64 reentrant = 0;
        connect(&api, &ApiClient::sessionChanged, &api, [&] {
            if (api.currentAccountUserId() == 900003)
                reentrant = writes[0]();
        });
        QVERIFY(api.setAccount(900003, "reentrant-account"));
        QVERIFY(reentrant);
        QCOMPARE(api.messageSendCooldownSeconds(), 3);
        api.shutdown();
        QCOMPARE(api.messageSendCooldownSeconds(), 0);
    }
    void chatHistoryReopensMergesAcksAndRestoresTag()
    {
        ApiClient api;
        QVERIFY(api.setNetworkTransport(sink.serverPort()));
        AccountService account(api);
        ProfileService profile(api, account);
        QVERIFY(AccountServiceTestAccess::adopt(account));
        ProfileServiceTestAccess::seed(profile, 0,
                                       {QJsonObject{{"key", "user:77"},
                                                    {"user_id", 77},
                                                    {"nickname", "Friend"},
                                                    {"avatar_url", "peer-avatar"},
                                                    {"following", true}}});
        const auto history =
            R"({"records":[{"id":"past-self","sender_id":900001,"sender_nickname":"Self","text":"saved self","sent_at_seconds":1790607092},{"id":"past-peer","sender_id":77,"sender_nickname":"Peer","text":"saved peer","sent_at_seconds":1790607093}],"tag":"chat:77_900001","unmapped_records":[],"ended":true})";
        profile.openChat("user:77");
        const auto read = ProfileServiceTestAccess::chatRead(profile);
        ProfileServiceTestAccess::accept(profile, read, history);
        QCOMPARE(profile.chatMessages().size(), 2);
        QVERIFY(profile.chatMessages()[0].toMap()["isSelf"].toBool());
        QVERIFY(!profile.chatMessages()[1].toMap()["isSelf"].toBool());
        QCOMPARE(profile.chatMessages()[1].toMap()["avatar"].toString(), QString("peer-avatar"));
        QVERIFY(!profile.chatMessages()[0].toMap()["time"].toString().isEmpty());
        profile.closeChat();
        QVERIFY(profile.chatMessages().isEmpty());
        profile.openChat("user:77");
        const auto reopen = ProfileServiceTestAccess::chatRead(profile);
        ProfileServiceTestAccess::accept(profile, read, history);
        QVERIFY(profile.chatMessages().isEmpty());
        ProfileServiceTestAccess::accept(profile, reopen, history);
        QCOMPARE(profile.chatMessages().size(), 2);
        profile.sendChat("new text");
        const auto write = ProfileServiceTestAccess::chatWrite(profile);
        QCOMPARE(ApiClientTestAccess::request(api, write)["query"].toObject()["tag"].toString(),
                 QString("chat:77_900001"));
        ProfileServiceTestAccess::accept(
            profile, write,
            R"({"msgid":"new-message","tag":"chat:77_900001","addtime":1790607100})");
        QCOMPARE(profile.chatMessages().size(), 3);
        profile.refreshChat();
        ProfileServiceTestAccess::accept(profile, ProfileServiceTestAccess::chatRead(profile),
                                         history);
        QCOMPARE(profile.chatMessages().size(), 3);
        const auto confirmed =
            R"({"records":[{"id":"new-message","sender_id":900001,"sender_nickname":"Self","text":"new text","sent_at_seconds":1790607100}],"tag":"chat:77_900001","unmapped_records":[],"ended":true})";
        profile.refreshChat();
        ProfileServiceTestAccess::accept(profile, ProfileServiceTestAccess::chatRead(profile),
                                         confirmed);
        QCOMPARE(profile.chatMessages().size(), 3);
        QCOMPARE(profile.chatMessages().last().toMap()["id"].toString(), QString("new-message"));
        profile.closeChat();
        ProfileServiceTestAccess::accept(profile, reopen, history);
        QVERIFY(profile.chatMessages().isEmpty());
    }
    void chatAckFailureCloseAndNoAutomaticSendReplay()
    {
        ApiClient api;
        QVERIFY(api.setNetworkTransport(sink.serverPort()));
        AccountService account(api);
        ProfileService profile(api, account);
        QVERIFY(AccountServiceTestAccess::adopt(account));
        ProfileServiceTestAccess::seed(
            profile, 0,
            {QJsonObject{
                {"key", "user:77"}, {"user_id", 77}, {"nickname", "Friend"}, {"following", true}}});
        profile.openChat("user:77");
        QVERIFY(profile.chatActive());
        auto read = ProfileServiceTestAccess::chatRead(profile);
        QVERIFY(read);
        ProfileServiceTestAccess::accept(profile, read, R"({"records":[],"ended":true})");
        QVERIFY(!profile.chatLoading());
        QVERIFY(profile.chatMessages().isEmpty());
        profile.sendChat(QString(201, 'a'));
        QVERIFY(!profile.chatSending());
        profile.sendChat("  Hello / 你好  ");
        auto write = ProfileServiceTestAccess::chatWrite(profile);
        QVERIFY(write);
        QVERIFY(!ApiClientTestAccess::verifies(api, write));
        QCOMPARE(ApiClientTestAccess::request(api, write)["query"].toObject()["text"].toString(),
                 QString("Hello / 你好"));
        profile.sendChat("duplicate");
        QCOMPARE(ProfileServiceTestAccess::chatWrite(profile), write);
        ProfileServiceTestAccess::fail(profile, write);
        QVERIFY(profile.chatMessages().isEmpty());
        profile.sendChat("Hello / 你好");
        QVERIFY(!ProfileServiceTestAccess::chatWrite(profile));
        profile.closeChat();
        profile.openChat("user:77");
        profile.sendChat("Hello / 你好");
        QVERIFY(!ProfileServiceTestAccess::chatWrite(profile));
        ApiClientTestAccess::expireMessageCooldown(api);
        profile.sendChat("Hello / 你好");
        write = ProfileServiceTestAccess::chatWrite(profile);
        QSignalSpy sent(&profile, &ProfileService::chatSent);
        ProfileServiceTestAccess::accept(
            profile, write, R"({"msgid":"sample-ack","tag":"sample-tag","addtime":1792242093})");
        QCOMPARE(sent.count(), 1);
        QCOMPARE(profile.chatMessages().size(), 1);
        QCOMPARE(profile.chatMessages().first().toMap()["text"].toString(),
                 QString("Hello / 你好"));
        profile.closeChat();
        QVERIFY(!profile.chatActive());
        QVERIFY(profile.chatMessages().isEmpty());
        QTest::qWait(400);
        QVERIFY(!profile.chatLoading());
        ProfileServiceTestAccess::accept(profile, write, R"({"msgid":"stale"})");
        QVERIFY(profile.chatMessages().isEmpty());
    }
};
QTEST_GUILESS_MAIN(CollectionSocialTest)
#include "CollectionSocialTest.moc"
