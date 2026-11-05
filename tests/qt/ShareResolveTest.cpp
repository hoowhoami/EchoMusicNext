#include "ShareResolveService.h"
#include "AccountService.h"
#include "DetailsService.h"
#include "NetworkPreferences.h"
#include "PlaybackService.h"
#include "ShellController.h"
#include "WindowController.h"
#include <QJsonArray>
#include <QNetworkProxy>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <iostream>
#include <iterator>
using namespace EchoMusic;
namespace EchoMusic
{
class PlaybackServiceTestAccess
{
public:
    static void hold(PlaybackService &service)
    {
        service.m_ready = false;
    }
};
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
    EchoMusic::SettingsStore settings{dir.filePath("prefs.ini")};
    ApiClient api{DeviceIdentity::temporary()};
    AccountService account{api};
    PlaybackService playback{api, settings};
    SharingCommands commands{account};
    RouteController router;
    DetailsService details{api, playback, router, false};
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
        details.attachShell(shell);
        details.registerRoutes(router);
        for (const auto &name :
             {"home", "playlist-detail", "artist-detail", "album-detail", "listen-together"})
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
        details.shutdown();
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
    void liveLegacySharePlayback_data()
    {
        QTest::addColumn<QString>("quality");
        QTest::addColumn<QString>("expected");
        QTest::newRow("hq") << "higher" << "higher";
        QTest::newRow("sq") << "flac" << "flac";
        QTest::newRow("hires-descends-to-sq") << "hi_res" << "flac";
    }
    void liveLegacySharePlayback()
    {
        if (qEnvironmentVariable("ECHO_LIVE_SHARE_PLAYBACK") != "1")
            QSKIP("Opt-in selected account; stdin credentials only");
        QFETCH(QString, quality);
        QFETCH(QString, expected);
        const std::string input((std::istreambuf_iterator<char>(std::cin)),
                                std::istreambuf_iterator<char>());
        const auto identity = QJsonDocument::fromJson(QByteArray::fromStdString(input)).object();
        DeviceIdentity device{identity.value("mid").toString(),
                              identity.value("dfid").toString(),
                              identity.value("guid").toString(),
                              identity.value("mac").toString(),
                              "EchoMusicNext",
                              identity.value("uuid").toString(),
                              "-"};
        QTemporaryDir dir;
        SettingsStore settings(dir.filePath("settings.ini"));
        settings.setValue("playback/defaultQuality", quality);
        settings.setValue("playback/compatibilityMode", true);
        settings.setValue("playback/volume", 0);
        settings.setValue("playerEngine/audioSamplerate", "48000");
        settings.setValue("playback/transitionMode", "none");
        settings.setValue("playback/mvAudioFallbackEnabled", false);
        ApiClient api(device);
        NetworkPreferences network(settings, api);
        const auto previous = QNetworkProxy::applicationProxy();
        const auto restore = qScopeGuard([&] { QNetworkProxy::setApplicationProxy(previous); });
        network.installApplicationTransport();
        QVERIFY(api.setAccount(quint64(identity.value("userid").toInteger()),
                               identity.value("token").toString(),
                               identity.value("t1").toString()));
        AccountService account(api);
        PlaybackService playback(api, settings, nullptr, dir.filePath("playback.sqlite"), &network);
        SharingCommands commands(account);
        RouteController router;
        ShellController shell(settings, router);
        WindowController window(settings, false);
        DetailsService details(api, playback, router, true);
        ShareResolveService shares(api, commands, router, shell, window);
        shell.attachPlayback(playback);
        details.attachShell(shell);
        details.registerRoutes(router);
        shares.registerRoutes(router);
        QSignalSpy sourceFailures(&playback, &PlaybackService::sourceOperationFailed);
        QSignalSpy playbackClosed(&playback, &PlaybackService::closed);
        QSignalSpy apiClosed(&api, &ApiClient::closed);
        const auto cleanup = qScopeGuard([&] {
            shares.shutdown();
            details.shutdown();
            router.shutdown();
            shell.shutdown();
            window.shutdown();
            playback.shutdown();
            api.shutdown();
            QVERIFY2(QTest::qWaitFor(
                         [&] { return !playbackClosed.isEmpty() && !apiClosed.isEmpty(); }, 15000),
                     "Live share probe did not drain playback and API before teardown");
        });
        QVERIFY(shares.openUrl(
            shareWebUrl({ShareResource::Song,
                         "8c4e910d5dd7250de34228945e8091cc",
                         {{"albumAudioId", "410300671"}, {"albumId", "57386687"}}})));
        QTRY_VERIFY_WITH_TIMEOUT(router.current() && router.current()->name() == "song-detail",
                                 30000);
        auto *detail = details.songFor(router.current());
        QVERIFY(detail);
        QTRY_VERIFY_WITH_TIMEOUT(!detail->loading(), 30000);
        QCOMPARE(detail->songs()->count(), 1);
        const auto track = detail->songs()->track("410300671");
        QVERIFY(track);
        QCOMPARE(detail->qualities().count("SD"), 1);
        QCOMPARE(track->catalogPrivilege, 5U);
        QVERIFY(!track->catalogMetadata.contains("old_copyright_code"));
        QVERIFY(detail->songs()->catalogPlayable("410300671"));
        detail->actions()->play();
        QCOMPARE(playback.trackId(), QString("410300671"));
        QTRY_VERIFY_WITH_TIMEOUT(playback.playing() || !playback.errorMessage().isEmpty(), 30000);
        QVERIFY2(playback.errorMessage().isEmpty(), qPrintable(playback.errorMessage()));
        QTRY_VERIFY_WITH_TIMEOUT(playback.clockPosition() > 2000, 10000);
        QCOMPARE(playback.resolvedQuality(), expected);
        QCOMPARE(sourceFailures.size(), 0);
        qInfo() << "Live share/detail command native playback" << quality << "->"
                << playback.resolvedQuality() << "position" << playback.clockPosition();
    }
    void sharedLegacySongReachesPlaybackOnlyWithMatchingFreeFacts_data()
    {
        QTest::addColumn<QString>("action");
        QTest::addColumn<QString>("facts");
        QTest::addColumn<bool>("playable");
        QTest::newRow("header-play") << "play" << "free" << true;
        QTest::newRow("row-play") << "row" << "free" << true;
        QTest::newRow("menu-play-now") << "now" << "free" << true;
        for (const auto &facts : {"missing-payment", "missing-price", "null-price", "paid",
                                  "priced", "another-resource", "no-source"})
            QTest::newRow(facts) << "play" << facts << false;
    }
    void sharedLegacySongReachesPlaybackOnlyWithMatchingFreeFacts()
    {
        QFETCH(QString, action);
        QFETCH(QString, facts);
        QFETCH(bool, playable);
        Fixture f;
        PlaybackServiceTestAccess::hold(f.playback);
        PlaybackTrack existing;
        existing.id = "existing";
        existing.hash = QString(32, 'a');
        existing.title = "Existing queue song";
        f.playback.playTracks({existing}, 0, "Existing queue", "queue:existing");
        const auto queueKey = f.playback.queueCatalog()->activeKey();
        // Normalized legacy-copyright facts from privilege_lite supply no
        // old_copyright_code for this track.
        const QString hash = "8C4E910D5DD7250DE34228945E8091CC";
        QJsonObject primary{{"hash", hash},     {"quality", "128"},     {"level", 2},
                            {"privilege", 5},   {"service_status", 0},  {"payment_type", 0},
                            {"price_units", 0}, {"duration_ms", 383085}};
        if (facts == "missing-payment")
            primary.remove("payment_type");
        if (facts == "missing-price")
            primary.remove("price_units");
        if (facts == "null-price")
            primary["price_units"] = QJsonValue::Null;
        if (facts == "paid")
            primary["payment_type"] = 3;
        if (facts == "priced")
            primary["price_units"] = 200;
        if (facts == "no-source")
            primary["privilege"] = 40;
        QJsonObject catalog{{"hash", facts == "another-resource" ? QString(32, 'b') : hash},
                            {"song_id", 410300671},
                            {"album_id", "57386687"},
                            {"display_name", "万能诊听器"},
                            {"artist_text", "洛天依、Z新豪"},
                            {"primary", primary},
                            {"variants", QJsonArray{primary}}};
        QVERIFY(f.open(ShareResource::Song, hash.toLower(),
                       {{"albumId", "57386687"}, {"albumAudioId", "410300671"}}));
        ShareResolveServiceTestAccess::acceptList(
            f.shares, ShareResolveServiceTestAccess::request(f.shares), {catalog});
        auto *detail = f.details.songFor(f.router.current());
        QVERIFY(detail);
        // Stale rights for another hash must not unlock this track. The real
        // share path normally keeps these equal; exercise the shared model too.
        if (facts == "another-resource") {
            auto row = detail->songs()->track("410300671")->toDomain();
            row["hash"] = hash;
            detail->songs()->replaceSongs({row});
        }
        QCOMPARE(detail->songs()->count(), 1);
        QCOMPARE(detail->songs()->catalogPlayable("410300671"), playable);
        const auto track = detail->songs()->track("410300671");
        QVERIFY(track);
        QVERIFY(!track->catalogMetadata.contains("old_copyright_code"));
        QCOMPARE(track->catalogPrivilege, facts == "no-source" ? 40U : 5U);
        QCOMPARE(
            detail->songs()->index(0, 0).data(SongCatalogModel::PrivilegeTags).toList().isEmpty(),
            playable);
        if (action == "now")
            detail->actions()->playNow("410300671");
        else if (action == "row")
            detail->actions()->playOrToggle("410300671");
        else
            detail->actions()->play();
        QCOMPARE(f.playback.queueCatalog()->activeKey(), queueKey);
        QCOMPARE(f.playback.queue()->rowCount(), playable ? 2 : 1);
        QCOMPARE(f.playback.trackId(), playable ? QString("410300671") : existing.id);
    }
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
        QVERIFY(!f.router.current()->query().contains("mainTab"));
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
        QCOMPARE(f.shares.explanation(), QStringLiteral("fixture failure（错误码 500）"));
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
