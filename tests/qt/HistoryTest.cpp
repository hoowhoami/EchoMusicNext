#include "HistoryService.h"
#include "NativeWindow.h"
#include "NetworkPreferences.h"
#include "PlaybackRepository.h"
#include "PlaybackService.h"
#include "ShellSettings.h"
#include "ThemeStore.h"
#include "RenderedInk.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include "SettingsStore.h"
#include <QTemporaryDir>
#include <QtTest>
using namespace EchoMusic;
namespace EchoMusic
{
class PlaybackServiceTestAccess
{
public:
    static PlaybackRepository *repository(PlaybackService &service)
    {
        return service.m_repository.get();
    }
};
class HistoryServiceTestAccess
{
public:
    static void enableMetadata(HistoryService &s, bool enabled = true)
    {
        s.m_network = enabled;
        if (enabled)
            s.completeSongs();
    }
    static quint64 metadataTicket(const HistoryService &s)
    {
        return s.m_metadataRequests.isEmpty() ? 0 : s.m_metadataRequests.cbegin().key();
    }
    static void metadata(HistoryService &s, quint64 ticket, const QJsonArray &values)
    {
        s.acceptMetadata(ticket, values);
    }
    static void accept(HistoryService &s, const QJsonArray &entries)
    {
        s.accept(entries);
    }
    static QJsonArray entries(const HistoryService &s)
    {
        return s.m_entries;
    }
};
class SongMetadataServiceTestAccess
{
public:
    static void complete(SongMetadataService &s, const QStringList &ids, const QJsonArray &values)
    {
        constexpr quint64 request = 990001;
        s.m_flights.insert(request, ids);
        s.accept(request, QJsonDocument(values).toJson());
    }
    static QSet<QString> waiting(const SongMetadataService &s, quint64 ticket)
    {
        return s.m_jobs.value(ticket).waiting;
    }
};
} // namespace EchoMusic
namespace
{
PlaybackTrack historyTrack(const QString &id, const QString &title, int duration = 180000)
{
    PlaybackTrack track;
    track.id = id;
    track.hash = QString("%1").arg(qHash(id), 32, 16, QChar('0'));
    track.title = title;
    track.artist = "周杰伦";
    track.album = "魔杰座";
    track.cover = "";
    track.durationMs = quint64(duration);
    track.catalogPrivilege = 10;
    track.catalogMetadata =
        QJsonObject{{"catalog_duration_ms", duration}, {"catalog_privilege", 10}};
    return track;
}
QQuickItem *historyVisual(QQuickItem *item, const QString &name)
{
    if (item->objectName() == name)
        return item;
    for (auto *child : item->childItems())
        if (auto *found = historyVisual(child, name))
            return found;
    return nullptr;
}
int historyVisualCount(QQuickItem *item, const QString &name)
{
    int count = item->objectName() == name;
    for (auto *child : item->childItems())
        count += historyVisualCount(child, name);
    return count;
}
QObject *historyObject(QObject *object, const QString &name)
{
    if (object->objectName() == name)
        return object;
    for (auto *child : object->children())
        if (auto *found = historyObject(child, name))
            return found;
    return nullptr;
}
} // namespace
class HistoryTest : public QObject
{
    Q_OBJECT
private slots:
    void metadataCompletesPresentationAndStatsWithoutWritingHistory()
    {
        QTemporaryDir dir;
        EchoMusic::SettingsStore prefs(dir.filePath("prefs.ini"));
        ApiClient api(DeviceIdentity::temporary());
        QVERIFY(api.setNetworkTransport(1));
        QVERIFY(api.setAccount(100, "offline-domain-fixture"));
        PlaybackService playback(api, prefs, nullptr, dir.filePath("playback.sqlite"));
        SongMetadataService metadata(api);
        RouteController router;
        HistoryService service(api, playback, metadata, router, false);
        service.registerRoutes(router);
        QVERIFY(router.navigate("history"));
        auto *repository = PlaybackServiceTestAccess::repository(playback);
        auto original = historyTrack("1", "原歌曲");
        original.artist = "临时歌手";
        original.album.clear();
        original.catalogMetadata["filename_artist_name"] = "临时歌手";
        original.catalogMetadata["source_file_name"] = "原始.mp3";
        original.catalogMetadata["opaque"] = QJsonObject{{"fact", 42}};
        repository->record(100, original, 12345);
        repository->record(100, original, 23456);
        auto local = historyTrack("local:track", "本地歌曲");
        local.localPath = dir.filePath("local.mp3");
        repository->record(100, local, 1000);
        service.refresh();
        QTRY_COMPARE_WITH_TIMEOUT(service.songs()->count(), 2, 5000);
        QTRY_COMPARE(service.stats()["totalPlays"].toInt(), 3);
        QVERIFY(!service.loading());
        const auto before = HistoryServiceTestAccess::entries(service);
        const auto statsBefore = service.stats();
        const auto trackBefore = service.songs()->track("1").value();
        service.songs()->toggleSelected("1");
        service.songs()->setQuery("正式歌手");
        QCOMPARE(service.songs()->count(), 0);
        HistoryServiceTestAccess::enableMetadata(service);
        const auto ticket = HistoryServiceTestAccess::metadataTicket(service);
        QVERIFY(ticket);
        // The local ID is filtered before any remote lookup is scheduled.
        QCOMPARE(SongMetadataServiceTestAccess::waiting(metadata, ticket), QSet<QString>{"1"});
        const QJsonArray facts{
            QJsonObject{{"id", "1"},
                        {"title", "正式歌曲"},
                        {"artists", QJsonArray{QJsonObject{{"id", 51}, {"name", "正式歌手"}}}},
                        {"album_id", "81"},
                        {"album_title", "正式专辑"},
                        {"cover_url", "https://example.test/1.jpg"},
                        {"hash", "NOT-A-SOURCE"},
                        {"duration_ms", 999000},
                        {"privilege", 0}}};
        SongMetadataServiceTestAccess::complete(metadata, {"1"}, facts);
        QTRY_COMPARE(service.songs()->count(), 1);
        QCOMPARE(HistoryServiceTestAccess::metadataTicket(service), quint64(0));
        QVERIFY(!service.loading());
        QCOMPARE(service.songs()->selectedCount(), 1);
        const auto completed = service.songs()->track("1").value();
        QCOMPARE(completed.title, QString("正式歌曲"));
        QCOMPARE(completed.album, QString("正式专辑"));
        QCOMPARE(completed.hash, trackBefore.hash);
        QCOMPARE(completed.durationMs, trackBefore.durationMs);
        QCOMPARE(completed.catalogPrivilege, trackBefore.catalogPrivilege);
        QCOMPARE(completed.catalogMetadata["history_key"],
                 trackBefore.catalogMetadata["history_key"]);
        QCOMPARE(completed.catalogMetadata["source_file_name"],
                 trackBefore.catalogMetadata["source_file_name"]);
        QCOMPARE(completed.catalogMetadata["opaque"], trackBefore.catalogMetadata["opaque"]);
        const auto after = HistoryServiceTestAccess::entries(service);
        QCOMPARE(after.size(), before.size());
        for (qsizetype i = 0; i < before.size(); ++i)
            for (const auto &key : {"key", "played_at", "play_count", "listened_ms"})
                QCOMPARE(after[i].toObject()[key], before[i].toObject()[key]);
        QCOMPARE(service.stats()["totalPlays"], statsBefore["totalPlays"]);
        QCOMPARE(service.stats()["statCards"], statsBefore["statCards"]);
        QCOMPARE(service.stats()["topSongs"].toList().first().toMap()["title"].toString(),
                 QString("正式歌曲"));
        QCOMPARE(service.stats()["topAlbum"].toMap()["name"].toString(), QString("正式专辑"));
        service.actions()->appendSong("1", false);
        QCOMPARE(playback.queue()->tracks().last().album, completed.album);
        QCOMPARE(playback.queue()->tracks().last().hash, original.hash);
        // A real SQL re-read proves completion did not record another play or
        // persist its presentation into the history row.
        HistoryServiceTestAccess::enableMetadata(service, false);
        QSignalSpy reads(&playback, &PlaybackService::historyLoaded);
        service.refresh();
        QTRY_VERIFY_WITH_TIMEOUT(!reads.isEmpty(), 5000);
        QCOMPARE(HistoryServiceTestAccess::entries(service), before);
        service.songs()->setQuery({});
        service.remove("1");
        QTRY_COMPARE_WITH_TIMEOUT(service.songs()->count(), 1, 5000);
        QCOMPARE(service.songs()->item(0)["resourceId"].toString(), QString("local:track"));
        service.shutdown();
        metadata.shutdown();
        router.shutdown();
        QSignalSpy drain(&playback, &PlaybackService::closed);
        playback.shutdown();
        QTRY_COMPARE_WITH_TIMEOUT(drain.size(), 1, 15000);
        api.shutdown();
    }
    void metadataRefreshRouteAccountAndDeletionScopes()
    {
        QTemporaryDir dir;
        EchoMusic::SettingsStore prefs(dir.filePath("prefs.ini"));
        ApiClient api(DeviceIdentity::temporary());
        QVERIFY(api.setNetworkTransport(1));
        QVERIFY(api.setAccount(100, "offline-domain-fixture"));
        PlaybackService playback(api, prefs);
        SongMetadataService metadata(api);
        RouteController router;
        HistoryService service(api, playback, metadata, router, true);
        service.registerRoutes(router);
        QVERIFY(router.registerPage({"other", "/main/other", QUrl("qrc:/not-instantiated.qml")}));
        QVERIFY(router.navigate("history"));
        const QJsonArray entries{QJsonObject{{"key", "1:history"},
                                             {"play_count", 2},
                                             {"played_at", qint64(1000)},
                                             {"listened_ms", 1234},
                                             {"track", historyTrack("1", "歌曲").toDomain()}}};
        const QJsonArray facts{QJsonObject{{"id", "1"},
                                           {"album_id", "8"},
                                           {"album_title", "新专辑"},
                                           {"cover_url", "https://example.test/1.jpg"}}};
        HistoryServiceTestAccess::accept(service, entries);
        const auto retired = HistoryServiceTestAccess::metadataTicket(service);
        QVERIFY(retired);
        service.refresh();
        HistoryServiceTestAccess::accept(service, entries);
        HistoryServiceTestAccess::metadata(service, retired, facts);
        QVERIFY(service.songs()->track("1")->cover.isEmpty());
        QVERIFY(router.navigate("other"));
        QCOMPARE(HistoryServiceTestAccess::metadataTicket(service), quint64(0));
        SongMetadataServiceTestAccess::complete(metadata, {"1"}, facts);
        QTRY_COMPARE(service.songs()->track("1")->cover, QString("https://example.test/1.jpg"));
        router.back();
        QTRY_COMPARE(service.songs()->track("1")->cover, QString("https://example.test/1.jpg"));
        HistoryServiceTestAccess::accept(service, entries);
        const auto removing = HistoryServiceTestAccess::metadataTicket(service);
        service.remove("1");
        HistoryServiceTestAccess::metadata(service, removing, facts);
        QCOMPARE(service.songs()->track("1")->cover, QString("https://example.test/1.jpg"));
        HistoryServiceTestAccess::accept(service, entries);
        const auto clearing = HistoryServiceTestAccess::metadataTicket(service);
        service.clear();
        HistoryServiceTestAccess::metadata(service, clearing, facts);
        QCOMPARE(service.songs()->track("1")->cover, QString("https://example.test/1.jpg"));
        HistoryServiceTestAccess::accept(service, entries);
        const auto oldSession = HistoryServiceTestAccess::metadataTicket(service);
        QVERIFY(api.setAccount(200, "other-offline-domain-fixture"));
        HistoryServiceTestAccess::metadata(service, oldSession, facts);
        QCOMPARE(service.songs()->count(), 0);
        QCOMPARE(HistoryServiceTestAccess::metadataTicket(service), quint64(0));
        service.shutdown();
        metadata.shutdown();
        router.shutdown();
        QSignalSpy drain(&playback, &PlaybackService::closed);
        playback.shutdown();
        QTRY_COMPARE_WITH_TIMEOUT(drain.size(), 1, 15000);
        api.shutdown();
    }
    void readsRemovesAndClearsAccountHistory()
    {
        QTemporaryDir dir;
        EchoMusic::SettingsStore prefs(dir.filePath("prefs.ini"));
        prefs.setValue("playback/volume", 0.);
        ApiClient api(DeviceIdentity::temporary());
        QVERIFY(api.setAccount(100, "history-token"));
        NetworkPreferences network(prefs, api);
        network.installApplicationTransport();
        PlaybackService playback(api, prefs, nullptr, dir.filePath("playback.sqlite"), &network);
        RouteController router;
        SongMetadataService songMetadata(api);
        HistoryService service(api, playback, songMetadata, router, false);
        service.registerRoutes(router);
        QVERIFY(router.available("history"));
        QSignalSpy notices(&service, &HistoryService::notice);
        auto *repository = PlaybackServiceTestAccess::repository(playback);
        QVERIFY(repository);
        repository->record(100, historyTrack("song-1", "第一首"), 12000);
        QTest::qWait(10);
        repository->record(100, historyTrack("song-2", "第二首"), 5000);
        service.refresh();
        QTRY_COMPARE_WITH_TIMEOUT(service.songs()->count(), 2, 5000);
        QTRY_VERIFY(!service.loading());
        const auto stats = service.stats();
        QCOMPARE(stats.value("totalSongs").toInt(), 2);
        QCOMPARE(stats.value("totalPlays").toLongLong(), 2);
        QCOMPARE(stats.value("statCards").toList().size(), 4);
        QCOMPARE(stats.value("recentDays").toList().size(), 7);
        QCOMPARE(stats.value("timeBuckets").toList().size(), 4);
        QCOMPARE(stats.value("topArtists").toList().size(), 1);
        QCOMPARE(stats.value("topArtists").toList().first().toMap().value("name").toString(),
                 QString("周杰伦"));
        QCOMPARE(stats.value("topSongs").toList().size(), 2);
        QCOMPARE(stats.value("topAlbum").toMap().value("name").toString(), QString("魔杰座"));
        QCOMPARE(service.songs()->item(0)["title"].toString(), QString("第二首"));
        const auto item = service.songs()->item(1);
        QCOMPARE(item["title"].toString(), QString("第一首"));
        QCOMPARE(item["artistText"].toString(), QString("周杰伦"));
        QVERIFY(service.songs()
                    ->track(service.songs()->item(1)["resourceId"].toString())
                    ->catalogMetadata.contains("history_key"));
        QCOMPARE(item["durationText"].toString(), QString("3:00"));
        // Removing uses the view id's history key, not a provider identity.
        service.remove(service.songs()->item(1)["resourceId"].toString());
        QTRY_COMPARE_WITH_TIMEOUT(service.songs()->count(), 1, 5000);
        QTRY_COMPARE(notices.size(), 1);
        service.clear();
        QTRY_COMPARE_WITH_TIMEOUT(service.songs()->count(), 0, 5000);
        QCOMPARE(notices.size(), 2);
        service.shutdown();
        router.shutdown();
        QSignalSpy drain(&playback, &PlaybackService::closed);
        playback.shutdown();
        QTRY_COMPARE_WITH_TIMEOUT(drain.size(), 1, 15000);
        api.shutdown();
    }
    void nativeHistoryPageAndClearDialog()
    {
        QTemporaryDir dir;
        EchoMusic::SettingsStore prefs(dir.filePath("prefs.ini"));
        prefs.setValue("playback/volume", 0.);
        ApiClient api(DeviceIdentity::temporary());
        QVERIFY(api.setAccount(100, "history-token"));
        NetworkPreferences network(prefs, api);
        network.installApplicationTransport();
        PlaybackService playback(api, prefs, nullptr, dir.filePath("playback.sqlite"), &network);
        RouteController router;
        SongMetadataService songMetadata(api);
        HistoryService service(api, playback, songMetadata, router, false);
        service.registerRoutes(router);
        auto *repository = PlaybackServiceTestAccess::repository(playback);
        repository->record(100, historyTrack("song-1", "历史歌曲一"), 12000);
        repository->record(100, historyTrack("song-2", "历史歌曲二"), 5000);
        ThemeStore theme(prefs);
        ShellSettings shell(prefs);
        QQmlEngine engine;
        theme.attachToEngine(engine);
        QStringList errors;
        connect(&engine, &QQmlEngine::warnings, this, [&](const auto &warnings) {
            for (const auto &warning : warnings)
                errors.append(warning.toString());
        });
        QQmlComponent component(&engine);
        component.setData(R"(
import QtQuick
import QtQuick.Window
import EchoMusic.Shell
import EchoMusic.Theme
import EchoMusic.History
Window {
 id: window
 required property RouteController router
 required property ShellSettings settings
 property real sidebarWidth: 0
 visible: true; width: 1150; height: 800
 Rectangle { anchors.fill: parent; color: Theme.main }
 Item {
  width: parent.width / window.settings.uiScale
  height: parent.height / window.settings.uiScale
  scale: window.settings.uiScale
  transformOrigin: Item.TopLeft
  RouteHost { anchors.fill: parent; anchors.leftMargin: window.sidebarWidth; router: window.router; settings: window.settings }
 }
})",
                          QUrl("qrc:/history-runtime.qml"));
        QScopedPointer<QObject> root(component.createWithInitialProperties(
            {{"router", QVariant::fromValue(&router)}, {"settings", QVariant::fromValue(&shell)}}));
        QVERIFY2(root, qPrintable(component.errorString()));
        auto *window = qobject_cast<QQuickWindow *>(root.data());
        QVERIFY(window);
        QVERIFY(Test::prepareNativeInput(window));
        QVERIFY(router.navigate("history"));
        QTRY_VERIFY(historyVisual(window->contentItem(), "historyPage"));
        QTRY_COMPARE_WITH_TIMEOUT(service.songs()->count(), 2, 5000);
        QTRY_VERIFY(historyVisual(window->contentItem(), "songDelegate"));
        auto *count = historyVisual(window->contentItem(), "historySongCount");
        QVERIFY(count);
        QVERIFY(count->property("text").toString().contains("2 首歌曲"));
        auto *pageItem = historyVisual(window->contentItem(), "historyPage");
        auto *detailHeader = historyVisual(pageItem, "songDetailHeader");
        QVERIFY(detailHeader);
        QCOMPARE(detailHeader->property("heading").toString(), QString("播放历史"));
        QTRY_VERIFY(historyObject(pageItem, "historyClearDialogOwner"));
        auto *clearOwner = historyObject(pageItem, "historyClearDialogOwner");
        QVERIFY(!clearOwner->property("item").value<QObject *>());
        auto *initialTab = historyVisual(window->contentItem(), "historyTabSongs");
        auto *initialIndicator = historyVisual(window->contentItem(), "detailTabIndicator");
        auto *tabRow = historyVisual(window->contentItem(), "historyTabToolbarRow");
        auto *divider = historyVisual(window->contentItem(), "historyTabDivider");
        QVERIFY(initialTab && initialIndicator && tabRow && divider);
        QTRY_VERIFY(initialIndicator->width() > 20);
        QTRY_COMPARE(initialIndicator->width(), initialTab->width());
        QTRY_COMPARE(tabRow->height(), 56.);
        QCOMPARE(divider->mapToScene(QPointF()).y(), tabRow->mapToScene(QPointF(0, 55)).y());
        const auto artifacts = qEnvironmentVariable("ECHO_HISTORY_ARTIFACTS");
        if (!artifacts.isEmpty()) {
            QVERIFY(QDir().mkpath(artifacts));
            QTest::qWait(200);
            QVERIFY(window->grabWindow().save(artifacts + "/history-loaded.png"));
        }
        // Stats tab: bounded native panel, no placeholder charts.
        auto *statsTab = historyVisual(window->contentItem(), "historyTabStats");
        QVERIFY(statsTab);
        QTest::mouseClick(
            window, Qt::LeftButton, Qt::NoModifier,
            statsTab->mapToScene(QPointF(statsTab->width() / 2, statsTab->height() / 2)).toPoint());
        QTRY_COMPARE(router.current()->query().value("tab").toString(), QString("stats"));
        QTRY_VERIFY(historyVisual(window->contentItem(), "historyStatsView"));
        auto *statsView = historyVisual(window->contentItem(), "historyStatsView");
        QVERIFY(statsView->isVisible());
        QTRY_COMPARE_WITH_TIMEOUT(statsView->property("opacity").toDouble(), 1., 2000);
        QTRY_COMPARE_WITH_TIMEOUT(historyVisualCount(statsView, "historyStatCard"), 4, 2000);
        QCOMPARE(historyVisualCount(statsView, "historyRecentDay"), 7);
        QCOMPARE(historyVisualCount(statsView, "historyTimeBucket"), 4);
        QCOMPARE(historyVisualCount(statsView, "historyTopArtistRow"), 1);
        QCOMPARE(historyVisualCount(statsView, "historyTopSongRow"), 2);
        {
            QList<QQuickItem *> stack{statsView};
            QQuickItem *icon = nullptr;
            while (!stack.isEmpty() && !icon) {
                auto *item = stack.takeFirst();
                stack.append(item->childItems());
                if (item->objectName() == "iconImage")
                    icon = item;
            }
            QVERIFY(icon);
            QTRY_COMPARE(icon->property("status").toInt(), 1);
        }
        QVERIFY(!historyVisual(window->contentItem(), "historyClearAction")->isVisible());
        auto *indicator = historyVisual(window->contentItem(), "detailTabIndicator");
        QVERIFY(indicator);
        QTRY_COMPARE_WITH_TIMEOUT(indicator->property("x").toDouble(), statsTab->x(), 2000);
        QTRY_COMPARE(tabRow->height(), 56.);
        QCOMPARE(divider->mapToScene(QPointF()).y(), tabRow->mapToScene(QPointF(0, 55)).y());
        QVERIFY(indicator->mapToScene(QPointF(0, indicator->height())).y() <
                divider->mapToScene(QPointF()).y());
        for (const auto mode : {ThemeStore::Light, ThemeStore::Dark}) {
            theme.setMode(mode);
            for (const auto zoom : {1., 1.25}) {
                shell.resetZoom();
                if (zoom > 1.)
                    for (int step = 0; step < 5; ++step)
                        shell.zoomIn();
                // CSS media queries depend on the whole zoom-adjusted window.
                // A sidebar changes available panel space, never the breakpoint.
                auto *statGrid = historyVisual(statsView, "historyStatGrid");
                auto *activityGrid = historyVisual(statsView, "historyActivityGrid");
                auto *rankingGrid = historyVisual(statsView, "historyRankingGrid");
                auto *week = historyVisual(statsView, "historyWeekCard");
                auto *time = historyVisual(statsView, "historyTimeCard");
                auto *artists = historyVisual(statsView, "historyArtistCard");
                auto *topSongs = historyVisual(statsView, "historyTopSongsCard");
                auto *latest = historyVisual(pageItem, "historyLatestPlayed");
                auto *chart = historyVisual(statsView, "historyWeekChart");
                QVERIFY(statGrid && activityGrid && rankingGrid && week && time && artists &&
                        topSongs && latest && chart);
                for (const int viewportWidth : {639, 640, 641, 979, 980, 981, 1150}) {
                    window->resize(qRound(viewportWidth * zoom), 1000);
                    for (const int sidebarWidth : {0, 216}) {
                        window->setProperty("sidebarWidth", sidebarWidth);
                        const auto logicalWidth = window->width() / shell.uiScale();
                        const int columns = logicalWidth <= 640 ? 1 : logicalWidth <= 980 ? 2 : 4;
                        QTRY_COMPARE(statGrid->property("columns").toInt(), columns);
                        QTRY_COMPARE(activityGrid->property("columns").toInt(),
                                     logicalWidth <= 980 ? 1 : 2);
                        QTRY_COMPARE(rankingGrid->property("columns").toInt(),
                                     logicalWidth <= 980 ? 1 : 2);
                        QTRY_COMPARE(latest->isVisible(), logicalWidth >= 640);
                        QTRY_COMPARE(chart->property("spacing").toReal(),
                                     logicalWidth <= 640 ? 6. : 10.);
                        if (logicalWidth > 980) {
                            // Both widths change during the resize polish. Do
                            // not freeze the previous grid width before waiting.
                            QTRY_VERIFY2(
                                qAbs(time->width() -
                                     qMax(260., (activityGrid->width() - 14) * .9 / 2.35)) <= 1.,
                                qPrintable(QString("time=%1 grid=%2 viewport=%3 sidebar=%4")
                                               .arg(time->width())
                                               .arg(activityGrid->width())
                                               .arg(logicalWidth)
                                               .arg(sidebarWidth)));
                            QTRY_COMPARE(week->y(), time->y());
                            QTRY_COMPARE(week->height(), time->height());
                            QTRY_VERIFY(qAbs(week->width() + time->width() + 14 -
                                             activityGrid->width()) <= 1.);
                            QTRY_VERIFY(qAbs(artists->width() -
                                             (rankingGrid->width() - 14) * .92 / 2) <= 1.);
                            QTRY_VERIFY(qAbs(topSongs->x() - artists->width() - 14) <= 1.);
                            QTRY_COMPARE(artists->y(), topSongs->y());
                            QTRY_COMPARE(artists->height(), topSongs->height());
                        } else {
                            QTRY_VERIFY(time->y() > week->y());
                            QTRY_VERIFY(topSongs->y() > artists->y());
                        }
                    }
                    if (!artifacts.isEmpty()) {
                        QTest::qWait(40);
                        QVERIFY(window->grabWindow().save(
                            artifacts + QString("/history-breakpoint-%1-%2-%3.png")
                                            .arg(int(mode))
                                            .arg(zoom)
                                            .arg(viewportWidth)));
                    }
                }
                window->setProperty("sidebarWidth", 0);
                window->resize(1150, 800);
                QTest::qWait(100);
                QCOMPARE(tabRow->height(), 56.);
                if (!artifacts.isEmpty())
                    QVERIFY(window->grabWindow().save(
                        artifacts + QString("/history-stats-%1-%2.png")
                                        .arg(mode == ThemeStore::Light ? "light" : "dark")
                                        .arg(zoom)));
                QVERIFY(QMetaObject::invokeMethod(statsView, "positionViewAtEnd"));
                QTRY_COMPARE(detailHeader->property("progress").toDouble(), 1.);
                auto *cover = historyVisual(detailHeader, "themedDetailCover");
                QVERIFY(cover);
                QTRY_COMPARE(cover->property("status").toInt(), 1);
                QTest::qWait(80);
                const auto image = window->grabWindow();
                const auto scene = cover->mapRectToScene(cover->boundingRect());
                const double ratio = double(image.width()) / window->width();
                const QRectF pixels(scene.topLeft() * ratio, scene.size() * ratio);
                const auto ink = Test::renderedInkBounds(
                    image,
                    pixels
                        .adjusted(pixels.width() * .2, pixels.height() * .2, -pixels.width() * .2,
                                  -pixels.height() * .2)
                        .toAlignedRect(),
                    Qt::white);
                QVERIFY(ink);
                if (!artifacts.isEmpty())
                    QVERIFY(image.save(artifacts +
                                       QString("/history-sticky-%1-%2.png")
                                           .arg(mode == ThemeStore::Light ? "light" : "dark")
                                           .arg(zoom)));
                const double glyphFraction = ink->width() / pixels.width();
                QVERIFY2(glyphFraction > .30 && glyphFraction < .40,
                         qPrintable(QString("collapsed clock fraction %1").arg(glyphFraction)));
                QCOMPARE(detailHeader->property("heading").toString(), QString("播放历史"));
                statsView->setProperty("contentY", -statsView->property("topMargin").toReal());
                QTRY_COMPARE(detailHeader->property("progress").toDouble(), 0.);
            }
        }
        theme.setMode(ThemeStore::Light);
        shell.resetZoom();
        if (!artifacts.isEmpty())
            QVERIFY(window->grabWindow().save(artifacts + "/history-stats.png"));
        window->resize(1778, 1000);
        QTest::qWait(250);
        if (!artifacts.isEmpty())
            QVERIFY(window->grabWindow().save(artifacts + "/history-stats-wide.png"));
        window->resize(1150, 800);
        QTest::qWait(150);
        QMetaObject::invokeMethod(statsView, "positionViewAtEnd");
        QTest::qWait(200);
        if (!artifacts.isEmpty())
            QVERIFY(window->grabWindow().save(artifacts + "/history-stats-bottom.png"));
        statsView->setProperty("contentY", -statsView->property("topMargin").toReal());
        // Reduced motion and hidden windows show content immediately, never at
        // a stuck transparent frame.
        window->hide();
        router.replaceQuery({{"tab", QString("songs")}});
        QTRY_VERIFY(historyVisual(window->contentItem(), "discoverySongs")->isVisible());
        auto *songsView = historyVisual(window->contentItem(), "catalogSongList");
        QVERIFY(songsView);
        QTRY_COMPARE_WITH_TIMEOUT(songsView->property("opacity").toDouble(), 1., 2000);
        window->show();
        theme.setMotionEnabled(false);
        router.replaceQuery({{"tab", QString("stats")}});
        QTRY_COMPARE_WITH_TIMEOUT(statsView->property("opacity").toDouble(), 1., 2000);
        QTRY_COMPARE_WITH_TIMEOUT(indicator->property("x").toDouble(), statsTab->x(), 2000);
        theme.setMotionEnabled(true);
        router.replaceQuery({{"tab", QString("songs")}});
        QTRY_COMPARE_WITH_TIMEOUT(songsView->property("opacity").toDouble(), 1., 2000);
        QTRY_COMPARE_WITH_TIMEOUT(detailHeader->property("progress").toReal(), 0., 2000);
        auto *songsTab = historyVisual(window->contentItem(), "historyTabSongs");
        QVERIFY(songsTab);
        QTest::mouseClick(
            window, Qt::LeftButton, Qt::NoModifier,
            songsTab->mapToScene(QPointF(songsTab->width() / 2, songsTab->height() / 2)).toPoint());
        QTRY_COMPARE(router.current()->query().value("tab").toString(), QString("songs"));
        auto *songBody = historyVisual(window->contentItem(), "catalogSongList");
        QVERIFY(songBody);
        auto *viewport = songBody->property("pageContainer").value<QObject *>();
        QVERIFY(viewport);
        for (int count : {0, 3, 80}) {
            QJsonArray rows;
            for (int i = 1; i <= count; ++i)
                rows.append(QJsonObject{{"id", QString::number(i)},
                                        {"title", "History " + QString::number(i)}});
            service.songs()->replaceSongs(rows);
            for (int height : {500, 800}) {
                window->resize(1150, height);
                QVERIFY(QMetaObject::invokeMethod(viewport, "scrollTo", Q_ARG(double, 0.)));
                for (int cycle = 0; cycle < 3; ++cycle) {
                    router.replaceQuery({{"tab", "stats"}});
                    QTest::qWait(40);
                    QTRY_COMPARE_WITH_TIMEOUT(viewport->property("scrollTop").toReal(), 0., 1000);
                    router.replaceQuery({{"tab", "songs"}});
                    QTest::qWait(40);
                    QTRY_COMPARE_WITH_TIMEOUT(viewport->property("scrollTop").toReal(), 0., 1000);
                }
                if (count == 80) {
                    QVERIFY(QMetaObject::invokeMethod(viewport, "scrollTo", Q_ARG(double, 60.)));
                    router.replaceQuery({{"tab", "stats"}});
                    QTest::qWait(40);
                    QTRY_COMPARE_WITH_TIMEOUT(viewport->property("scrollTop").toReal(), 60., 1000);
                    router.replaceQuery({{"tab", "songs"}});
                    QTest::qWait(40);
                    QTRY_COMPARE_WITH_TIMEOUT(viewport->property("scrollTop").toReal(), 60., 1000);
                }
            }
        }
        QVERIFY(QMetaObject::invokeMethod(viewport, "scrollTo", Q_ARG(double, 0.)));
        window->resize(1150, 800);
        auto *clearAction = historyVisual(window->contentItem(), "historyClearAction");
        QVERIFY(clearAction);
        QTest::mouseClick(
            window, Qt::LeftButton, Qt::NoModifier,
            clearAction->mapToScene(QPointF(clearAction->width() / 2, clearAction->height() / 2))
                .toPoint());
        QTRY_VERIFY(clearOwner->property("item").value<QObject *>());
        auto *confirm =
            historyObject(clearOwner->property("item").value<QObject *>(), "historyClearConfirm");
        QVERIFY(confirm);
        QMetaObject::invokeMethod(confirm, "clicked");
        QTRY_COMPARE_WITH_TIMEOUT(service.songs()->count(), 0, 5000);
        QTRY_VERIFY(!clearOwner->property("item").value<QObject *>());
        QVERIFY2(errors.isEmpty(), qPrintable(errors.join('\n')));
        if (!artifacts.isEmpty())
            QVERIFY(window->grabWindow().save(artifacts + "/history-empty.png"));
        service.shutdown();
        router.shutdown();
        QSignalSpy drain(&playback, &PlaybackService::closed);
        playback.shutdown();
        QTRY_COMPARE_WITH_TIMEOUT(drain.size(), 1, 15000);
        api.shutdown();
    }
};
QTEST_MAIN(HistoryTest)
#include "HistoryTest.moc"
