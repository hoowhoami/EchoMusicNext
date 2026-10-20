#include "ShareResolveService.h"
#include "AccountService.h"
#include "PlaybackService.h"
#include "ShellController.h"
#include "WindowController.h"
#include <QJsonArray>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
using namespace EchoMusic;
namespace EchoMusic
{
class ShareResolveServiceTestAccess
{
public:
    static quint64 request(const ShareResolveService &service)
    {
        return service.m_pending.isEmpty() ? 0 : service.m_pending.keys().first();
    }
    static quint64 roomState(const ShareResolveService &service)
    {
        for (auto i = service.m_pending.cbegin(); i != service.m_pending.cend(); ++i)
            if (i->kind == ShareResolveService::Kind::RoomState)
                return i.key();
        return 0;
    }
    static quint64 roomDetail(const ShareResolveService &service)
    {
        for (auto i = service.m_pending.cbegin(); i != service.m_pending.cend(); ++i)
            if (i->kind == ShareResolveService::Kind::RoomDetail)
                return i.key();
        return 0;
    }
    static int pending(const ShareResolveService &service)
    {
        return service.m_pending.size();
    }
    static void accept(ShareResolveService &service, quint64 id, const QJsonObject &value)
    {
        service.accept(id, QJsonDocument(value).toJson());
    }
    static void acceptList(ShareResolveService &service, quint64 id, const QJsonArray &value)
    {
        service.accept(id, QJsonDocument(value).toJson());
    }
    static void absent(ShareResolveService &service, quint64 id)
    {
        service.accept(id, "null");
    }
    static void reject(ShareResolveService &service, quint64 id, const QJsonObject &error)
    {
        service.reject(id, QJsonDocument(error).toJson());
    }
};
} // namespace EchoMusic
namespace
{
struct Fixture
{
    QTemporaryDir dir;
    QSettings settings{dir.filePath("prefs.ini"), QSettings::IniFormat};
    ApiClient api{DeviceIdentity::temporary()};
    AccountService account{api};
    PlaybackService playback{api, settings};
    SharingCommands commands{account};
    RouteController router;
    ShellController shell{settings, router};
    WindowController window{settings, false};
    ShareResolveService shares{api, commands, router, shell, window};
    Fixture()
    {
        shares.registerRoutes(router);
        api.setNetworkTransport(1); // No network adapter: controlled native-domain callbacks only.
        api.setAccount(900001, "offline-test-fixture");
        playback.songCommands()->attachSharing(commands);
        shell.attachPlayback(playback);
        for (const auto &name : {"home", "song-detail", "playlist-detail", "artist-detail",
                                 "album-detail", "listen-together"})
            router.registerPage({name,
                                 QStringLiteral("/test/") + name,
                                 QUrl("qrc:/native-domain-test.qml"),
                                 {},
                                 true,
                                 true,
                                 false,
                                 {}});
        router.navigate("home");
    }
    ~Fixture()
    {
        shares.shutdown();
        router.shutdown();
        shell.shutdown();
        window.shutdown();
        playback.shutdown();
        api.shutdown();
    }
    bool open(ShareResource type, const QString &id, const QMap<QString, QString> &query = {})
    {
        return shares.openUrl(shareWebUrl({type, id, query}));
    }
};
} // namespace
class ShareResolveTest final : public QObject
{
    Q_OBJECT
private slots:
    void acceptedResourcesReplaceOnlyTheirTransientResolver()
    {
        for (auto type : {ShareResource::Album, ShareResource::Artist, ShareResource::Playlist}) {
            Fixture f;
            QVERIFY(f.open(type, "12345"));
            QCOMPARE(f.router.current()->name(), QString("share-resolve"));
            QVERIFY(f.shares.loading());
            const auto id = ShareResolveServiceTestAccess::request(f.shares);
            QVERIFY(id);
            if (type == ShareResource::Playlist)
                ShareResolveServiceTestAccess::acceptList(
                    f.shares, id, {QJsonObject{{"global_id", "12345"}, {"title", "测试歌单"}}});
            else
                ShareResolveServiceTestAccess::accept(f.shares, id, {{"id", 12345}});
            QCOMPARE(f.router.current()->name(), shareResourceName(type) + "-detail");
            QCOMPARE(f.router.current()->resourceId(), QString("12345"));
            QCOMPARE(f.playback.queue()->rowCount(), 0);
            f.router.back();
            QCOMPARE(f.router.current()->name(), QString("home"));
        }
    }
    void songResolutionHandsAllNativeCatalogFactsToTheExistingDetailEntry()
    {
        Fixture f;
        const QString hash(32, 'a');
        QSignalSpy handoff(&f.shell, &ShellController::songDetailOpening);
        QVERIFY(f.open(ShareResource::Song, hash, {{"albumId", "42"}}));
        const auto id = ShareResolveServiceTestAccess::request(f.shares);
        const QJsonObject primary{{"hash", hash},
                                  {"cover_url", "https://image.example.invalid/cover.jpg"},
                                  {"duration_ms", 234000},
                                  {"privilege", 8},
                                  {"payment_type", QJsonValue::Null}};
        const QJsonArray variants{
            QJsonObject{{"quality", "flac"}, {"hash", QString(32, 'b')}, {"size_bytes", 98765432}}};
        const QJsonObject catalog{
            {"hash", hash},
            {"song_id", 32042818},
            {"album_id", "960399"},
            {"display_name", "晴天"},
            {"artist_text", "周杰伦"},
            {"album_title", "叶惠美"},
            {"language", "国语"},
            {"primary", primary},
            {"variants", variants},
            {"hash_offset", QJsonObject{{"start_ms", 1000}, {"end_ms", 31000}}}};
        ShareResolveServiceTestAccess::acceptList(f.shares, id, {catalog});
        QCOMPARE(handoff.size(), 1);
        const auto track = qvariant_cast<PlaybackTrack>(handoff[0][0]);
        QCOMPARE(track.hash, hash);
        QCOMPARE(track.songId, 32042818ULL);
        QCOMPARE(track.albumId, 960399ULL);
        QCOMPARE(track.catalogMetadata.value("resource_privileges").toObject(), catalog);
        QCOMPARE(track.toCatalog().value("privilege").toInt(), 8);
        QCOMPARE(track.toCatalog().value("duration_ms").toInteger(), 234000);
        QCOMPARE(f.router.current()->name(), QString("song-detail"));
        QCOMPARE(f.router.current()->resourceId(), QString("32042818"));
        QCOMPARE(f.router.current()->query().value("hash").toString(), hash);
        QCOMPARE(f.router.current()->query().value("mainTab").toString(), QString("detail"));
        QCOMPARE(f.playback.queue()->rowCount(), 0);
        f.router.back();
        QCOMPARE(f.router.current()->name(), QString("home"));
    }
    void incomingRoomIsValidatedThenOnlyOpensAnInvitation()
    {
        Fixture f;
        QVERIFY(f.open(ShareResource::ListenTogether, "1565598054776708260", {{"roomType", "0"}}));
        const auto state = ShareResolveServiceTestAccess::roomState(f.shares);
        const auto detail = ShareResolveServiceTestAccess::roomDetail(f.shares);
        QVERIFY(state && detail && state != detail);
        ShareResolveServiceTestAccess::accept(f.shares, detail,
                                              {{"id", "1565598054776708260"}, {"name", "晚风"}});
        QCOMPARE(f.router.current()->name(), QString("share-resolve"));
        ShareResolveServiceTestAccess::accept(f.shares, state, {{"active_code", 1}});
        QCOMPARE(f.router.current()->name(), QString("listen-together"));
        QCOMPARE(f.router.current()->query().value("roomId").toString(),
                 QString("1565598054776708260"));
        QCOMPARE(f.router.current()->query().value("roomType").toString(), QString("0"));
        QVERIFY(!f.playback.listenTogether()->joined());
        QCOMPARE(f.playback.queue()->rowCount(), 0);
    }
    void closedRoomsCancelTheirOtherResponseAndDoNotOpenThePage()
    {
        Fixture f;
        QVERIFY(f.open(ShareResource::ListenTogether, "12345"));
        const auto state = ShareResolveServiceTestAccess::roomState(f.shares);
        const auto detail = ShareResolveServiceTestAccess::roomDetail(f.shares);
        ShareResolveServiceTestAccess::accept(f.shares, state, {{"active_code", 0}});
        QVERIFY(!f.shares.loading());
        QCOMPARE(ShareResolveServiceTestAccess::pending(f.shares), 0);
        ShareResolveServiceTestAccess::accept(f.shares, detail, {{"name", "旧响应"}});
        QCOMPARE(f.router.current()->name(), QString("share-resolve"));
        QVERIFY(f.shares.explanation().contains(QStringLiteral("删除")));
        QVERIFY(f.open(ShareResource::ListenTogether, "12345", {{"roomType", "1"}}));
        QCOMPARE(ShareResolveServiceTestAccess::pending(f.shares), 0);
        QVERIFY(f.shares.explanation().contains(QStringLiteral("格式")));
    }
    void oldUrlAccountAndOffPageCallbacksCannotNavigate()
    {
        Fixture f;
        QVERIFY(f.open(ShareResource::Album, "111"));
        const auto old = ShareResolveServiceTestAccess::request(f.shares);
        QVERIFY(f.open(ShareResource::Artist, "222"));
        const auto current = ShareResolveServiceTestAccess::request(f.shares);
        QVERIFY(current != old);
        ShareResolveServiceTestAccess::accept(f.shares, old, {{"id", 111}});
        QCOMPARE(f.router.current()->name(), QString("share-resolve"));
        ShareResolveServiceTestAccess::accept(f.shares, current, {{"id", 222}});
        QCOMPARE(f.router.current()->resourceId(), QString("222"));
        QVERIFY(f.open(ShareResource::Album, "333"));
        const auto beforeAccount = ShareResolveServiceTestAccess::request(f.shares);
        QVERIFY(f.api.setAccount(900002, "another-offline-fixture"));
        const auto afterAccount = ShareResolveServiceTestAccess::request(f.shares);
        QVERIFY(afterAccount && afterAccount != beforeAccount);
        ShareResolveServiceTestAccess::accept(f.shares, beforeAccount, {{"id", 333}});
        QCOMPARE(f.router.current()->name(), QString("share-resolve"));
        f.router.navigate("home");
        ShareResolveServiceTestAccess::accept(f.shares, afterAccount, {{"id", 333}});
        QCOMPARE(f.router.current()->name(), QString("home"));
    }
    void errorsRetryTheActualRequestAndKeepTheFailurePage()
    {
        Fixture f;
        QVERIFY(f.open(ShareResource::Album, "12345"));
        const auto first = ShareResolveServiceTestAccess::request(f.shares);
        ShareResolveServiceTestAccess::reject(f.shares, first,
                                              {{"code", 500}, {"message", "fixture failure"}});
        QVERIFY(!f.shares.loading());
        QVERIFY(f.shares.explanation().contains(QStringLiteral("网络")));
        f.shares.retry();
        const auto second = ShareResolveServiceTestAccess::request(f.shares);
        QVERIFY(second && second != first);
        ShareResolveServiceTestAccess::absent(f.shares, second);
        QVERIFY(!f.shares.loading());
        QVERIFY(f.shares.explanation().contains(QStringLiteral("删除")));
        QCOMPARE(f.router.current()->name(), QString("share-resolve"));
        f.shares.goBack();
        QCOMPARE(f.router.current()->name(), QString("home"));
    }
    void optionalFactsRemainUnknownRatherThanBecomingAResponseShapeGate()
    {
        Fixture f;
        QVERIFY(f.open(ShareResource::Artist, "12345"));
        ShareResolveServiceTestAccess::accept(f.shares,
                                              ShareResolveServiceTestAccess::request(f.shares),
                                              {{"id", QJsonValue::Null}, {"name", ""}});
        QCOMPARE(f.router.current()->name(), QString("artist-detail"));
        QCOMPARE(f.router.current()->resourceId(), QString("12345"));
    }
    void pluginSourceAcceptanceRemainsWithItsExistingOwner()
    {
        Fixture f;
        bool delegated = false;
        f.shares.setPluginOpener([&](const ShareTarget &target) {
            delegated = target.type == ShareResource::Plugin && target.id == "echo.spectrum" &&
                        target.query.value("source") == "https://example.org/index.json";
            f.router.navigate("home");
            return true;
        });
        QVERIFY(f.open(ShareResource::Plugin, "echo.spectrum",
                       {{"source", "https://example.org/index.json"},
                        {"path", "spectrum"},
                        {"version", "1.0.0"}}));
        QTRY_VERIFY(delegated);
        QTRY_COMPARE(f.router.current()->name(), QString("home"));
        QCOMPARE(ShareResolveServiceTestAccess::pending(f.shares), 0);
    }
    void playlistShareIdentityFollowsTheSourcePublicPriority()
    {
        QVariantMap facts{{"global_id", "public-number"},
                          {"creator_global_id", "collection_source"},
                          {"creator_id", 900001},
                          {"creator_list_id", 12},
                          {"public_id", 12345}};
        QCOMPARE(playlistShareId(facts, "fallback"), QString("collection_source"));
        facts["global_id"] = "collection_public";
        QCOMPARE(playlistShareId(facts, "fallback"), QString("collection_public"));
        facts["global_id"] = "public-number";
        facts["creator_global_id"] = "";
        QCOMPARE(playlistShareId(facts), QString("collection_3_900001_12_0"));
        facts.remove("creator_id");
        QCOMPARE(playlistShareId(facts), QString("public-number"));
        facts.remove("global_id");
        QCOMPARE(playlistShareId(facts), QString("12345"));
        facts.remove("public_id");
        QCOMPARE(playlistShareId(facts, "fallback"), QString("fallback"));
    }
};
QTEST_MAIN(ShareResolveTest)
#include "ShareResolveTest.moc"
