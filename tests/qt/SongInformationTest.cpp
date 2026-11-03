#include "DetailsService.h"
#include "NativeWindow.h"
#include "SettingsStore.h"
#include "ShellSettings.h"
#include "SongMetadataService.h"
#include "ThemeStore.h"
#include "UiTypography.h"
#include <QDir>
#include <QJsonDocument>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
using namespace EchoMusic;
namespace EchoMusic
{
class PlaybackServiceTestAccess
{
public:
    static void current(PlaybackService &playback, PlaybackTrack track)
    {
        ++playback.m_sequence;
        playback.m_track = std::move(track);
        emit playback.changed();
    }
};
class DetailsTestAccess
{
public:
    static void pending(SongDetailContext &c, quint64 id)
    {
        c.m_pending.insert(id, {SongDetailContext::Metadata, c.m_api.sessionRevision(), c.m_epoch});
    }
    static void deliver(SongDetailContext &c, quint64 id, const QJsonObject &value)
    {
        c.accept(id, QJsonDocument(QJsonArray{value}).toJson());
    }
    static void ready(SongDetailContext &c, const QJsonObject &value)
    {
        c.m_loaded = c.m_rankingsStarted = c.m_commentsStarted = true;
        c.m_track["duration_ms"] = 240000;
        pending(c, 991001);
        deliver(c, 991001, value);
    }
};
class SongMetadataServiceTestAccess
{
public:
    static void accept(SongMetadataService &service, const QJsonObject &value)
    {
        service.m_flights.insert(991002, {value["id"].toString()});
        service.accept(991002, QJsonDocument(QJsonArray{value}).toJson());
    }
};
} // namespace EchoMusic
static QJsonObject presentation()
{
    // Domain presentation, separate from the locally captured upstream wire.
    return {{"id", "32042818"},
            {"title", "歌曲资料示例 Song information"},
            {"album_id", "960399"},
            {"album_title", "专辑 Album"},
            {"artists", QJsonArray{QJsonObject{{"id", 3520}, {"name", "周杰伦"}}}},
            {"information", QJsonObject{{"lyricist", "方文山"},
                                        {"composer", "周杰伦"},
                                        {"published_at", "2008-10-15"},
                                        {"album_published_at", "2008-10-15"},
                                        {"language", "国语"},
                                        {"bpm", 115},
                                        {"tempo_description", "快(BPM 115)"}}}};
}
static QQuickItem *visual(QQuickItem *root, const QString &name)
{
    if (!root || !root->isVisible())
        return nullptr;
    if (root->objectName() == name)
        return root;
    for (auto *child : root->childItems())
        if (auto *item = visual(child, name))
            return item;
    return nullptr;
}
class SongInformationTest : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase()
    {
        QQuickStyle::setStyle("Basic");
        configureUiTypography();
    }
    void playbackDetailsKeepOpeningSnapshotWithoutNavigation()
    {
        QTemporaryDir dir;
        SettingsStore prefs(dir.filePath("settings.ini"));
        ApiClient api(DeviceIdentity::temporary());
        PlaybackService playback(api, prefs, nullptr, dir.filePath("playback.sqlite"));
        RouteController router;
        DetailsService service(api, playback, router, false);
        service.registerRoutes(router);
        QVERIFY(router.navigate("song-detail", {}, "77"));
        auto *route = router.current();
        PlaybackTrack first;
        first.id = "32042818";
        first.songId = 32042818;
        first.hash = "105A2705BEEE36A8BED029A8E1A6311D";
        first.title = "Opening song";
        first.artist = "Opening artist";
        first.durationMs = 240000;
        PlaybackServiceTestAccess::current(playback, first);
        QVERIFY(playback.hasTrack());
        QPointer<SongDetailContext> detail = service.openPlaybackDetails();
        QVERIFY(detail);
        QCOMPARE(router.current(), route);
        DetailsTestAccess::ready(*detail, presentation());
        const auto original = detail->detail();
        PlaybackTrack next = first;
        next.id = "20";
        next.songId = 20;
        next.title = "Next song";
        PlaybackServiceTestAccess::current(playback, next);
        QCOMPARE(detail->detail(), original);
        QCOMPARE(detail->songId(), QString("32042818"));
        DetailsTestAccess::pending(*detail, 991003);
        detail->setPresented(false);
        DetailsTestAccess::deliver(*detail, 991003, {{"title", "Late response"}});
        QCOMPARE(detail->detail(), original);
        api.advanceSession();
        QCOMPARE(detail->detail().value("title").toString(), QString("Opening song"));
        QVERIFY(!detail->information().contains("composer"));
        PlaybackServiceTestAccess::current(playback, next);
        QPointer<SongDetailContext> replacement = service.openPlaybackDetails();
        QVERIFY(replacement && replacement != detail);
        service.closePlaybackDetails(detail);
        QTRY_VERIFY(detail.isNull());
        detail = replacement;
        QVERIFY(detail);
        QCOMPARE(detail->songId(), QString("20"));
        QCOMPARE(detail->detail().value("title").toString(), QString("Next song"));
        QCOMPARE(router.current(), route);
        service.closePlaybackDetails(detail);
        QTRY_VERIFY(detail.isNull());
        service.shutdown();
        router.shutdown();
        QSignalSpy closed(&playback, &PlaybackService::closed);
        playback.shutdown();
        QTRY_COMPARE_WITH_TIMEOUT(closed.size(), 1, 15000);
        api.shutdown();
    }
    void sharedCacheProvidesInformationWithoutAHash()
    {
        QTemporaryDir dir;
        SettingsStore prefs(dir.filePath("settings.ini"));
        ApiClient api(DeviceIdentity::temporary());
        PlaybackService playback(api, prefs, nullptr, dir.filePath("playback.sqlite"));
        SongMetadataService metadata(api);
        playback.attachMetadata(metadata);
        QSignalSpy completed(&metadata, &SongMetadataService::completed);
        QVERIFY(metadata.request({"32042818"}));
        SongMetadataServiceTestAccess::accept(metadata, presentation());
        QTRY_COMPARE(completed.size(), 1);
        RouteController router;
        DetailsService service(api, playback, router, true);
        service.registerRoutes(router);
        QVERIFY(router.navigate("song-detail", {{"title", "Loading title"}}, "32042818"));
        auto *context = service.songFor(router.current());
        QVERIFY(context);
        QTRY_COMPARE(context->information().value("lyricist").toString(), QString("方文山"));
        QCOMPARE(context->information().value("bpm").toDouble(), 115.);
        QCOMPARE(context->information().value("published_at").toString(), QString("2008-10-15"));
        QCOMPARE(context->albumNavigationId(), QString("960399"));
        QCOMPARE(playback.queue()->rowCount(), 0);
        service.shutdown();
        router.shutdown();
        metadata.shutdown();
        QSignalSpy closed(&playback, &PlaybackService::closed);
        playback.shutdown();
        QTRY_COMPARE_WITH_TIMEOUT(closed.size(), 1, 15000);
        api.shutdown();
    }
    void staleAndMissingFactsStayIsolated()
    {
        QTemporaryDir dir;
        SettingsStore prefs(dir.filePath("settings.ini"));
        ApiClient api(DeviceIdentity::temporary());
        PlaybackService playback(api, prefs, nullptr, dir.filePath("playback.sqlite"));
        RouteController router;
        DetailsService service(api, playback, router, false);
        service.registerRoutes(router);
        QVERIFY(router.navigate("song-detail", {}, "32042818"));
        auto *context = service.songFor(router.current());
        DetailsTestAccess::ready(*context, presentation());
        QCOMPARE(context->information().value("composer").toString(), QString("周杰伦"));
        auto partial = presentation();
        partial["information"] = QJsonObject{{"composer", QJsonValue::Null}, {"lyricist", ""}};
        DetailsTestAccess::ready(*context, partial);
        QCOMPARE(context->information().value("composer").toString(), QString("周杰伦"));
        QCOMPARE(context->information().value("lyricist").toString(), QString("方文山"));
        DetailsTestAccess::pending(*context, 991003);
        context->reload();
        auto late = presentation();
        late["information"] = QJsonObject{{"composer", "Late author"}};
        DetailsTestAccess::deliver(*context, 991003, late);
        QCOMPARE(context->information().value("composer").toString(), QString("周杰伦"));
        QVERIFY(QMetaObject::invokeMethod(&api, "sessionChanged",
                                          Q_ARG(quint64, api.sessionRevision() + 1)));
        QVERIFY(context->information().value("composer").toString().isEmpty());
        QVERIFY(!context->information().value("bpm").isValid() ||
                context->information().value("bpm").isNull());
        QCOMPARE(playback.queue()->rowCount(), 0);
        service.shutdown();
        router.shutdown();
        QSignalSpy closed(&playback, &PlaybackService::closed);
        playback.shutdown();
        QTRY_COMPARE_WITH_TIMEOUT(closed.size(), 1, 15000);
        api.shutdown();
    }
    void productionPageInformationAndNavigation()
    {
        QTemporaryDir dir;
        SettingsStore prefs(dir.filePath("settings.ini"));
        ThemeStore theme(prefs);
        theme.setMotionEnabled(false);
        ShellSettings settings(prefs);
        ApiClient api(DeviceIdentity::temporary());
        PlaybackService playback(api, prefs, nullptr, dir.filePath("playback.sqlite"));
        RouteController router;
        DetailsService service(api, playback, router, false);
        service.registerRoutes(router);
        const QUrl destination("qrc:/qt/qml/EchoMusic/Shell/RoutePage.qml");
        router.registerPage({"album-detail", "/album/:id", destination});
        router.registerPage({"artist-detail", "/artist/:id", destination});
        QVERIFY(router.navigate("song-detail", {{"hash", "105A2705BEEE36A8BED029A8E1A6311D"}},
                                "32042818"));
        auto *context = service.songFor(router.current());
        auto song = presentation();
        auto info = song["information"].toObject();
        info["composer"] = QString("很长的作曲者姓名 Long contributor name ").repeated(5);
        song["information"] = info;
        DetailsTestAccess::ready(*context, song);
        QQmlEngine engine;
        theme.attachToEngine(engine);
        QSignalSpy warnings(&engine, &QQmlEngine::warnings);
        QQmlComponent component(&engine);
        component.setData(R"(
import QtQuick
import EchoMusic.Shell
import EchoMusic.Theme
Window {
 id: window
 required property RouteController router
 required property ShellSettings settings
 visible: true; width: 1000; height: 760; color: Theme.main
 Item {
  width: parent.width / window.settings.uiScale; height: parent.height / window.settings.uiScale
  scale: window.settings.uiScale; transformOrigin: Item.TopLeft
  RouteHost { anchors.fill: parent; router: window.router; settings: window.settings }
 }
})",
                          QUrl("qrc:/song-information-test.qml"));
        QScopedPointer<QObject> root(
            component.createWithInitialProperties({{"router", QVariant::fromValue(&router)},
                                                   {"settings", QVariant::fromValue(&settings)}}));
        QVERIFY2(root, qPrintable(component.errorString()));
        auto *window = qobject_cast<QQuickWindow *>(root.data());
        QVERIFY(window);
        QVERIFY(Test::prepareNativeInput(window));
        QTRY_VERIFY(visual(window->contentItem(), "songInformationSection"));
        auto *section = visual(window->contentItem(), "songInformationSection");
        auto *lyrics = visual(section, "songInformationValue-lyricist");
        auto *tempo = visual(section, "songInformationValue-tempo");
        auto *composer = visual(section, "songInformation-composer");
        auto *next = visual(section, "songInformation-tempo");
        QVERIFY(lyrics && tempo && composer && next);
        QCOMPARE(lyrics->property("text").toString(), QString("方文山"));
        QCOMPARE(tempo->property("text").toString(), QString("快(BPM 115)"));
        auto *language = visual(section, "songInformationValue-language");
        auto *summary = visual(window->contentItem(), "songHeaderMetadata");
        auto *headerArtist = visual(window->contentItem(), "songArtistLink");
        auto *headerAlbum = visual(window->contentItem(), "songAlbumLink");
        QVERIFY(language && summary && headerArtist && headerAlbum);
        QVERIFY(!visual(summary, "songAlbumLink"));
        QCOMPARE(language->property("text").toString(), QString("国语"));
        for (auto *entry : summary->findChildren<QObject *>())
            QVERIFY(entry->property("text").toString() != QString("国语"));
        const auto captures = qEnvironmentVariable("ECHO_SONG_INFORMATION_CAPTURES");
        for (const auto mode : {ThemeStore::Light, ThemeStore::Dark})
            for (const double scale : {1., 1.25})
                for (const int width : {1000, 560}) {
                    theme.setMode(mode);
                    settings.resetZoom();
                    if (scale > 1.)
                        for (int i = 0; i < 5; ++i)
                            settings.zoomIn();
                    window->resize(width, 760);
                    QTest::qWait(100);
                    const auto frame = window->grabWindow();
                    QVERIFY(!frame.isNull());
                    const auto artistBounds =
                        headerArtist->mapRectToScene(headerArtist->boundingRect());
                    const auto albumBounds =
                        headerAlbum->mapRectToScene(headerAlbum->boundingRect());
                    QVERIFY(qAbs(artistBounds.center().y() - albumBounds.center().y()) < 1);
                    QVERIFY(albumBounds.left() > artistBounds.right());
                    QVERIFY(albumBounds.right() <= window->width());
                    QVERIFY(!composer->mapRectToScene(composer->boundingRect())
                                 .intersects(next->mapRectToScene(next->boundingRect())));
                    QVERIFY(section->width() <= window->width() / scale);
                    if (!captures.isEmpty()) {
                        QVERIFY(QDir().mkpath(captures));
                        QVERIFY(frame.save(captures + QString("/information-%1-%2-%3.png")
                                                          .arg(int(mode))
                                                          .arg(scale)
                                                          .arg(width)));
                    }
                }
        auto *list = visual(window->contentItem(), "commentsList");
        QVERIFY(list);
        QVERIFY(QMetaObject::invokeMethod(list, "positionViewAtEnd"));
        auto *duration = visual(window->contentItem(), "songInformationValue-duration");
        QVERIFY(duration);
        QTRY_VERIFY(duration->mapToScene({0, duration->height()}).y() <= window->height() + 1);
        DetailsTestAccess::ready(*context, presentation());
        settings.resetZoom();
        window->resize(1000, 760);
        for (const auto mode : {ThemeStore::Light, ThemeStore::Dark}) {
            theme.setMode(mode);
            QTest::qWait(60);
            QVERIFY(QMetaObject::invokeMethod(list, "positionViewAtBeginning"));
            const auto frame = window->grabWindow();
            QVERIFY(!frame.isNull());
            if (!captures.isEmpty()) {
                const auto bounds = section->mapRectToScene(section->boundingRect());
                const auto ratio = frame.width() / qreal(window->width());
                QVERIFY(
                    frame
                        .copy(
                            QRectF(bounds.topLeft() * ratio, bounds.size() * ratio).toAlignedRect())
                        .save(captures + QString("/information-normal-%1.png").arg(int(mode))));
            }
        }
        auto *album = visual(window->contentItem(), "songInformationAlbum");
        QVERIFY(album);
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                          album->mapToScene(album->boundingRect().center()).toPoint());
        QTRY_COMPARE(router.current()->name(), QString("album-detail"));
        QCOMPARE(router.current()->resourceId(), QString("960399"));
        router.back();
        QTRY_COMPARE(router.current()->name(), QString("song-detail"));
        // Song detail is not cached; this is a fresh visit in the offline fixture.
        context = service.songFor(router.current());
        DetailsTestAccess::ready(*context, song);
        QTRY_VERIFY(visual(window->contentItem(), "songInformationArtists"));
        auto *artist =
            visual(visual(window->contentItem(), "songInformationArtists"), "artistLink-0");
        QVERIFY(artist && artist->isEnabled());
        QVERIFY(!window->grabWindow().isNull());
        const auto artistBounds = artist->mapRectToScene(artist->boundingRect());
        QVERIFY(window->contentItem()->boundingRect().contains(artistBounds.center()));
        Test::moveNativePointer(window, artistBounds.center().toPoint());
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, artistBounds.center().toPoint());
        QTRY_COMPARE(router.current()->name(), QString("artist-detail"));
        QCOMPARE(router.current()->resourceId(), QString("3520"));
        QCOMPARE(playback.queue()->rowCount(), 0);
        router.back();
        QTRY_COMPARE(router.current()->name(), QString("song-detail"));
        // A fresh offline visit has no inferred credit names or catalog links.
        QQuickItem *unknown = nullptr;
        QTRY_VERIFY((unknown = visual(window->contentItem(), "songInformationValue-lyricist")));
        QTRY_COMPARE(unknown->property("text").toString(), QString("—"));
        QCOMPARE(warnings.size(), 0);
        root.reset();
        service.shutdown();
        router.shutdown();
        QSignalSpy closed(&playback, &PlaybackService::closed);
        playback.shutdown();
        QTRY_COMPARE_WITH_TIMEOUT(closed.size(), 1, 15000);
        api.shutdown();
    }
};
QTEST_MAIN(SongInformationTest)
#include "SongInformationTest.moc"
