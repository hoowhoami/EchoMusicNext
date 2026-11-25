#include "CatalogRead.h"
#include "PlaybackRepository_p.h"
#include "SongCatalogModel.h"
#include "ArtistDirectorySliceModel.h"
#include "DiscoveryService.h"
#include "ExploreService.h"
#include "DiscoveryRouteCoordinator.h"
#include "PlaybackService.h"
#include "PortraitController.h"
#include "PlaybackRepository.h"
#include "AccountService.h"
#include "RouteController.h"
#include "ShellSettings.h"
#include "RenderedInk.h"
#include "ThemeStore.h"
#include "NetworkPreferences.h"
#include "SessionDatabase.h"
#include "SongMetadataService.h"
#include "NativeWindow.h"
#include <iostream>
#include <memory>
#include <algorithm>
#include <cmath>
#include <QNetworkProxy>
#include <QScopeGuard>
#include <QAbstractItemModelTester>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJSValue>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQmlContext>
#include <QQmlProperty>
#include <QQuickWindow>
#include <QWheelEvent>
#include <QQuickItem>
#include <QQuickImageProvider>
#include <QMutex>
#include <QMutexLocker>
#include "SettingsStore.h"
#include <QStandardItemModel>
#include <QSignalSpy>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest>
using namespace EchoMusic;
namespace EchoMusic
{
class ApiClientTestAccess
{
public:
    static QJsonObject query(const ApiClient &api, quint64 id)
    {
        return api.m_pending.value(id).request["query"].toObject();
    }
};
class PlaybackServiceTestAccess
{
public:
    static void hold(PlaybackService &playback)
    {
        playback.m_ready = false;
    }
    static void currentTrack(PlaybackService &playback, const PlaybackTrack &track)
    {
        playback.m_track = track;
    }
};
class SongCatalogStorageTestAccess
{
public:
    static bool hasStore(const SongCatalogModel &model)
    {
        return bool(model.m_store);
    }
    static auto statistics(const SongCatalogModel &model)
    {
        return model.m_hotViews.statistics();
    }
    static bool metadataActive(const SongCatalogModel &model)
    {
        return model.m_metadataActive;
    }
    static void raceUnchangedMetadataWithViewRead(SongCatalogModel &model)
    {
        model.m_pendingMetadata.insert(0);
        model.flushMetadata();
        model.data(model.index(0), SongCatalogModel::Title);
        model.flushViewReads();
    }
};
class ExploreServiceTestAccess
{
public:
    static void complete(ExploreService &s, ExploreService::Part part, const QJsonValue &data,
                         quint64 id = 70001)
    {
        s.m_pending.insert(id, {part, s.m_api.sessionRevision()});
        s.accept(id, data.isArray() ? QJsonDocument(data.toArray()).toJson()
                                    : QJsonDocument(data.toObject()).toJson());
    }
    static void pending(ExploreService &s, ExploreService::Part part, quint64 id)
    {
        s.m_states[part] = ExploreService::Loading;
        s.m_pending.insert(id, {part, s.m_api.sessionRevision()});
    }
    static void retired(ExploreService &s, quint64 id, const QJsonObject &data)
    {
        s.accept(id, QJsonDocument(data).toJson());
    }
    static int state(const ExploreService &s, ExploreService::Part part)
    {
        return s.m_states[part];
    }
};
class PortraitControllerTestAccess
{
public:
    static void present(PortraitController &gallery, const QUrl &source)
    {
        gallery.setActive(true);
        gallery.m_urls = {source.toString()};
        gallery.m_index = 0;
        gallery.publish();
    }
};
class DiscoveryServiceTestAccess
{
public:
    static RecommendationFeed *stream(DiscoveryService &s, const QString &key)
    {
        return s.m_feeds.value(key);
    }
    static quint64 cardRequest(const DiscoveryService &s, const QString &key)
    {
        for (auto it = s.m_cardPending.cbegin(); it != s.m_cardPending.cend(); ++it)
            if (it->first && it->first->key() == key)
                return it.key();
        return 0;
    }
    static void completeCard(DiscoveryService &s, quint64 id, const QJsonObject &data)
    {
        QVERIFY(s.acceptCard(id, QJsonDocument(data).toJson()));
    }
    static void occupyCardRequests(DiscoveryService &s)
    {
        s.m_networkEnabled = true;
        s.m_activePage = "home";
        s.m_styleGroups = QJsonArray{QJsonObject{{"child", QJsonArray{QJsonObject{{"id", "tag"}}}}}}
                              .toVariantList();
        for (int i = 0; i < 3; ++i)
            s.m_cardPending.insert(90000 + i,
                                   {s.m_recommendationCards.find(QString::number(3004 + i)),
                                    s.m_api.sessionRevision()});
    }
    static auto itemStatistics(const RecommendationItemsModel &model)
    {
        return model.m_hot.statistics();
    }
    static bool itemCached(const RecommendationItemsModel &model, int row)
    {
        return bool(model.m_hot.peek(row));
    }
    static bool itemPersisted(const RecommendationItemsModel &model)
    {
        return model.m_store && model.m_store->persisted(model.m_source);
    }

    static void complete(DiscoveryService &s, DiscoveryService::Part part, const QJsonValue &data,
                         quint64 id = 12345)
    {
        s.m_pending.insert(id, {part, s.m_api.sessionRevision()});
        s.succeeded(id, QJsonDocument(data.isArray() ? QJsonDocument(data.toArray())
                                                     : QJsonDocument(data.toObject()))
                            .toJson());
    }
    static void pending(DiscoveryService &s, DiscoveryService::Part part, quint64 id,
                        bool notify = false)
    {
        s.m_states[part] = DiscoveryService::Loading;
        s.m_pending.insert(id, {part, s.m_api.sessionRevision()});
        if (notify)
            emit s.changed();
    }
    static void fail(DiscoveryService &s, DiscoveryService::Part part, quint64 id)
    {
        pending(s, part, id);
        s.failed(id, QStringLiteral("内容加载失败"));
    }
    // DiscoverFlow keeps replace vs. append distinct across the async boundary;
    // these mirror the page's bottom-preload request and its failure path.
    static void completeAppend(DiscoveryService &s, DiscoveryService::Part part,
                               const QJsonValue &data, quint64 id = 12345)
    {
        s.m_pending.insert(id, {part, s.m_api.sessionRevision(), true});
        s.succeeded(id, QJsonDocument(data.isArray() ? QJsonDocument(data.toArray())
                                                     : QJsonDocument(data.toObject()))
                            .toJson());
    }
    static void failAppend(DiscoveryService &s, DiscoveryService::Part part, quint64 id)
    {
        s.m_states[part] = DiscoveryService::Loading;
        s.m_pending.insert(id, {part, s.m_api.sessionRevision(), true});
        s.failed(id, QStringLiteral("内容加载失败"));
    }
    static void retired(DiscoveryService &s, quint64 id, const QJsonObject &data)
    {
        s.succeeded(id, QJsonDocument(data).toJson());
    }
    static bool hasPending(DiscoveryService &s)
    {
        return !s.m_pending.isEmpty();
    }
    static void seedDetailPresentation(DiscoveryService &s, bool description, bool optional)
    {
        s.m_dailySubtitle = QStringLiteral("为你量身定制的每日歌单 Daily");
        s.m_dailyDate = optional ? QStringLiteral("2026-10-16") : QString{};
        s.m_selectedRank = 1;
        s.m_rankTotal = optional ? 200 : 1;
        s.m_ranks = {QVariantMap{
            {"id", 1},
            {"title", "名品堂 Ranking"},
            {"description",
             description ? QStringLiteral("酷狗音乐平台评选出最具影响力的歌曲排行榜。 ").repeated(8)
                         : QString{}},
            {"update_frequency", optional ? "每年年底" : ""},
            {"published_at", optional ? "2025-12-30 18:00:33" : ""}}};
        for (auto part : {DiscoveryService::Daily, DiscoveryService::RankTop,
                          DiscoveryService::RankCatalog, DiscoveryService::RankSongs})
            s.m_states[part] = DiscoveryService::Ready;
        s.m_dailySongs.replaceSongs({domainSongForPresentation()});
        s.m_rankSongs.replaceSongs({domainSongForPresentation()});
        emit s.changed();
    }

private:
    static QJsonObject domainSongForPresentation()
    {
        return {{"id", "1"},
                {"title", "歌曲 Song"},
                {"hash", QString(32, '1')},
                {"duration_ms", 180000},
                {"artists", QJsonArray{}}};
    }
};
} // namespace EchoMusic
// Canonical-domain inputs exercise Qt presentation only, never an invented HTTP
// success. The optional native/live test below exercises actual wire conversion.
QJsonObject domainSong(int id, const QString &title = "中文 Song")
{
    return {{"id", QString::number(id)},
            {"hash", QString("%1").arg(id, 32, 16, QChar('0'))},
            {"title", title},
            {"album_id", "960399"},
            {"album_title", "Album 专辑"},
            {"cover_url", ""},
            {"duration_ms", 180000 + id},
            {"privilege", 10},
            {"artists", QJsonArray{QJsonObject{{"id", 1}, {"name", "Artist 歌手"}}}},
            {"metadata",
             QJsonObject{{"payment_type", 3},
                         {"originality_code", 1},
                         {"audio_id", 120},
                         {"source_file_name", "Artist 歌手 - 中文 Song (Live).mp3"},
                         {"videos", QJsonArray{}},
                         {"resources",
                          QJsonArray{QJsonObject{{"quality", "flac"},
                                                 {"hash", "0123456789ABCDEF0123456789ABCDEF"}}}},
                         {"rights", QJsonObject{{"quality_map", QJsonObject{{"attr0", 32}}}}}}}};
}
QQuickItem *findVisual(QQuickItem *root, const QString &name)
{
    if (root->objectName() == name)
        return root;
    for (auto *child : root->childItems())
        if (auto *found = findVisual(child, name))
            return found;
    return nullptr;
}
QString previewPresentationError(QQuickItem *root)
{
    QList<QQuickItem *> pending{root};
    while (!pending.isEmpty()) {
        auto *item = pending.takeLast();
        pending.append(item->childItems());
        if (item->objectName() != "homeSongCard" || !item->property("resourceActive").toBool())
            continue;
        auto *model = item->property("collection").value<SongCatalogModel *>();
        const auto row = item->property("index").toInt();
        const auto expected = model->data(model->index(row), SongCatalogModel::Title).toString();
        const auto roleTitle = item->property("title").toString();
        const auto value = item->property("entry");
        const auto entry = value.metaType() == QMetaType::fromType<QJSValue>()
                               ? value.value<QJSValue>().toVariant().toMap()
                               : value.toMap();
        if (expected.isEmpty() || roleTitle != expected ||
            entry.value("title").toString() != expected)
            return QString("row %1: sourceReady=%2 roleMatches=%3 entryMatches=%4 title=%5 "
                           "expected=%6 idMatches=%7")
                .arg(row)
                .arg(!expected.isEmpty())
                .arg(roleTitle == expected)
                .arg(entry.value("title").toString() == expected)
                .arg(roleTitle, expected)
                .arg(item->property("resourceId").toString() ==
                     model->data(model->index(row), SongCatalogModel::ResourceId).toString());
    }
    return {};
}
QObject *findVisualOwnerObject(QQuickItem *root, const QString &name)
{
    if (auto *found = root->findChild<QObject *>(name))
        return found;
    for (auto *child : root->childItems())
        if (auto *found = findVisualOwnerObject(child, name))
            return found;
    return nullptr;
}
int countVisual(QQuickItem *root, const QString &name)
{
    int count = root->objectName() == name ? 1 : 0;
    for (auto *child : root->childItems())
        count += countVisual(child, name);
    return count;
}
class ArtistImageRequests final : public QQuickImageProvider
{
public:
    ArtistImageRequests() : QQuickImageProvider(QQuickImageProvider::Image) {}
    QImage requestImage(const QString &id, QSize *size, const QSize &) override
    {
        const QMutexLocker lock(&m_mutex);
        m_requested.insert(id);
        *size = QSize(32, 32);
        QImage image(*size, QImage::Format_ARGB32_Premultiplied);
        image.fill(QColor("#75b9a5"));
        return image;
    }
    QSet<QString> requested()
    {
        const QMutexLocker lock(&m_mutex);
        return m_requested;
    }

private:
    QMutex m_mutex;
    QSet<QString> m_requested;
};
class DiscoveryTest : public QObject
{
    Q_OBJECT
private slots:
    void discoverFlowSourceLayout_data()
    {
        QTest::addColumn<QSize>("size");
        QTest::addColumn<qreal>("zoom");
        QTest::newRow("wide") << QSize(1200, 700) << 1.;
        QTest::newRow("normal") << QSize(850, 600) << 1.;
        QTest::newRow("narrow") << QSize(590, 600) << 1.;
        QTest::newRow("short") << QSize(850, 400) << 1.;
        QTest::newRow("narrow-short") << QSize(590, 400) << 1.;
        QTest::newRow("scaled-container") << QSize(740, 520) << 1.25;
    }
    void discoverFlowSourceLayout()
    {
        QFETCH(QSize, size);
        QFETCH(qreal, zoom);
        QTemporaryDir dir;
        EchoMusic::SettingsStore prefs(dir.filePath("prefs.ini"));
        prefs.setValue("playback/volume", 0.);
        ThemeStore theme(prefs);
        theme.setMotionEnabled(false);
        ShellSettings shell(prefs);
        if (zoom > 1.)
            for (int i = 0; i < 5; ++i)
                shell.zoomIn();
        ApiClient api(DeviceIdentity::temporary());
        AccountService account(api);
        PlaybackService playback(api, prefs);
        RouteController router;
        DiscoveryService service(api, playback, account);
        DiscoveryRouteCoordinator routes(service, router, false);
        QVERIFY(router.navigate("discover-flow"));
        auto song = domainSong(101);
        QImage artwork(800, 800, QImage::Format_RGB32);
        artwork.fill(QColor("#75b9a5"));
        const auto cover = dir.filePath("cover.png");
        QVERIFY(artwork.save(cover));
        song["cover_url"] = QUrl::fromLocalFile(cover).toString();
        song["title"] = QStringLiteral("刷歌视觉对照 Discover Flow");
        song["album_title"] = QStringLiteral("专辑 Album");
        service.discoverFlow()->replaceSongs({song, domainSong(102)});
        DiscoveryServiceTestAccess::completeAppend(service, DiscoveryService::DiscoverFlow,
                                                   QJsonObject{{"items", QJsonArray{}}});
        QQmlEngine engine;
        theme.attachToEngine(engine);
        QSignalSpy warnings(&engine, &QQmlEngine::warnings);
        QQmlComponent component(&engine);
        component.setData(R"(
import QtQuick
import QtQuick.Window
import EchoMusic.Shell
import EchoMusic.Theme
Window {
 id: window
 required property RouteController router
 required property ShellSettings settings
 property color desktopColor: "#e4e0d4"
 property bool desktopVisible: true
 color: "transparent"
 visible: true; width: 850; height: 600
 Rectangle { anchors.fill: parent; color: window.desktopColor; visible: window.desktopVisible }
 ThemeBackground { anchors.fill: parent; baseColor: Theme.main; opacity: 1 - Theme.store.transparency / 100 }
 Item {
  width: parent.width / window.settings.uiScale
  height: parent.height / window.settings.uiScale
  scale: window.settings.uiScale; transformOrigin: Item.TopLeft
  RouteHost { anchors.fill: parent; router: window.router; settings: window.settings }
 }
})",
                          QUrl("qrc:/discover-flow-source-layout.qml"));
        QScopedPointer<QObject> root(component.createWithInitialProperties(
            {{"router", QVariant::fromValue(&router)}, {"settings", QVariant::fromValue(&shell)}}));
        QVERIFY2(root, qPrintable(component.errorString()));
        auto *window = qobject_cast<QQuickWindow *>(root.data());
        QVERIFY(window);
        window->resize(size);
        if (QGuiApplication::platformName() != "offscreen")
            QVERIFY(QTest::qWaitForWindowExposed(window));
        QTRY_VERIFY(findVisual(window->contentItem(), "discoverFlowPage"));
        auto *page = findVisual(window->contentItem(), "discoverFlowPage");
        auto *stage = findVisual(page, "discoverFlowStage");
        auto *actions = findVisual(page, "discoverFlowActions");
        auto *title = findVisual(page, "discoverFlowTitle");
        auto *artist = findVisual(page, "discoverFlowArtist");
        QVERIFY(stage && actions && title && artist);
        QTRY_VERIFY(findVisual(page, "discoverFlowArtwork"));
        QTRY_COMPARE(findVisual(page, "discoverFlowArtwork")->property("status").toInt(), 1);
        // A different track's playback gallery must never supply discovery
        // artwork or acquire a discovery-owned activation lease.
        QImage portrait(800, 800, QImage::Format_RGB32);
        portrait.fill(QColor("#db517e"));
        const auto portraitPath = dir.filePath("other-track-portrait.png");
        QVERIFY(portrait.save(portraitPath));
        const auto portraitUrl = QUrl::fromLocalFile(portraitPath);
        auto *gallery = playback.portraits();
        PortraitControllerTestAccess::present(*gallery, portraitUrl);
        gallery->setActive(false);
        QCOMPARE(gallery->source(), QUrl());
        PortraitControllerTestAccess::present(*gallery, portraitUrl);
        auto *background = findVisual(page, "discoverFlowBackdrop");
        QVERIFY(background);
        QCOMPARE(background->property("source").toUrl(), QUrl::fromLocalFile(cover));
        auto *quality = findVisual(page, "discoverFlowQuality");
        QVERIFY(quality);
        QCOMPARE(quality->property("text").toString(), "SQ");
        const auto artifacts = qEnvironmentVariable("ECHO_FLOW_ARTIFACTS");
        for (const auto mode : {ThemeStore::Light, ThemeStore::Dark}) {
            theme.setMode(mode);
            QTest::qWait(100);
            const auto image = window->grabWindow();
            if (!artifacts.isEmpty()) {
                QVERIFY(QDir().mkpath(artifacts));
                QVERIFY(
                    image.save(artifacts + QString("/%1-%2.png")
                                               .arg(QTest::currentDataTag())
                                               .arg(mode == ThemeStore::Light ? "light" : "dark")));
            }
            const bool narrow = page->width() <= 600, shortPage = page->height() <= 420;
            const qreal bottom = shortPage ? 28 : narrow ? 32 : 40;
            const qreal top = shortPage ? 68 : narrow ? 76 : 88;
            const auto actionBottom = actions->mapToItem(page, {0, actions->height()}).y();
            qInfo() << "FLOW" << page->size() << "action bottom" << actionBottom << "expected"
                    << page->height() - bottom;
            QVERIFY(qAbs(actionBottom - (page->height() - bottom)) < .01);
            QCOMPARE(stage->y(), top);
            QCOMPARE(stage->x(), narrow ? 18. : 32.);
            QCOMPARE(actions->property("spacing").toReal(), shortPage ? 6. : 12.);
            QCOMPARE(title->property("font").value<QFont>().pixelSize(),
                     narrow || shortPage ? 24 : 32);
            QCOMPARE(artist->property("font").value<QFont>().pixelSize(), narrow ? 14 : 16);
            auto *info = findVisual(page, "discoverFlowInfo");
            QVERIFY(info);
            QVERIFY(info->width() <= 680);
            auto *content = findVisual(info, "discoverFlowInfoContent");
            QVERIFY(content);
            // mapToItem passes through Qt's float scene transform. Keep the
            // allowance below one ten-thousandth of a logical pixel.
            QVERIFY(qAbs(content->mapToItem(page, {0, content->height()}).y() -
                         (page->height() - bottom)) < .0001);
            auto *next = findVisual(page, "discoverFlowNext");
            auto *play = findVisual(page, "discoverFlowPlay");
            QVERIFY(next && play);
            QCOMPARE(next->size(), QSizeF(36, 36));
            QCOMPARE(play->size(), QSizeF(52, 52));
            // A uniform source isolates the top entrance and adjusted scrim from cover
            // content. Probe the stage gap, away from all foreground glyphs.
            if (QGuiApplication::platformName() != "offscreen") {
                const auto ratio = image.width() / qreal(window->width());
                const auto x = (page->width() - (narrow ? 80 : 100)) * zoom;
                const auto mask = [](qreal t) {
                    const QList<QPointF> stops{{0, 0},   {.05, .10}, {.12, .45}, {.20, .82},
                                               {.28, 1}, {.58, 1},   {1, 1}};
                    for (int i = 1; i < stops.size(); ++i)
                        if (t <= stops[i].x()) {
                            const auto a = stops[i - 1], b = stops[i];
                            return a.y() + (b.y() - a.y()) * (t - a.x()) / (b.x() - a.x());
                        }
                    return 0.;
                };
                QList<qreal> ys;
                for (const auto t : {0., .025, .05, .085, .12, .16, .20, .24, .28, .58})
                    ys.append(t * page->height());
                for (const auto distance : {460., 350., 300., 240., 120., 20.})
                    if (page->height() > distance)
                        ys.append(page->height() - distance);
                const QColor coverColor("#75b9a5"), base = theme.main();
                for (const auto y : ys) {
                    const int py = qBound(0, qRound(y * zoom * ratio), image.height() - 1);
                    const auto logicalY = (py + .5) / (zoom * ratio);
                    const auto distance = page->height() - logicalY;
                    const auto scrim = distance >= 300   ? 0.
                                       : distance >= 120 ? .45 * (300 - distance) / 180
                                                         : .45 + .33 * (120 - distance) / 120;
                    const auto alpha = mask(logicalY / page->height()) * (1 - scrim);
                    const auto pixel = image.pixelColor(qRound(x * ratio), py);
                    for (int channel = 0; channel < 3; ++channel) {
                        const auto c = [channel](const QColor &color) {
                            return channel == 0   ? color.red()
                                   : channel == 1 ? color.green()
                                                  : color.blue();
                        };
                        const auto expected = qRound(c(base) * (1 - alpha) + c(coverColor) * alpha);
                        QVERIFY2(
                            qAbs(c(pixel) - expected) <= 3,
                            qPrintable(QString("source alpha y=%1 channel=%2 actual=%3 expected=%4")
                                           .arg(logicalY)
                                           .arg(channel)
                                           .arg(c(pixel))
                                           .arg(expected)));
                    }
                }
                // Both media and scrim must leave the shell's rounded bottom
                // corners clear. Check actual pixels at each scale and theme,
                // including the inside of the arc, rather than just a radius property.
                for (const bool right : {false, true}) {
                    const int edgeX = right ? image.width() - 2 : 1;
                    const auto outside = image.pixelColor(edgeX, image.height() - 2);
                    QVERIFY(qAbs(outside.red() - base.red()) <= 3);
                    QVERIFY(qAbs(outside.green() - base.green()) <= 3);
                    QVERIFY(qAbs(outside.blue() - base.blue()) <= 3);
                    const int insideX = qRound((right ? page->width() - 10 : 10) * zoom * ratio);
                    const auto inside = image.pixelColor(insideX, image.height() - 2);
                    QVERIFY(qAbs(inside.green() - base.green()) > 8);
                }
            }
        }
        if (QString(QTest::currentDataTag()) == "normal" || zoom > 1.) {
            for (const auto mode : {ThemeStore::Light, ThemeStore::Dark}) {
                theme.setMode(mode);
                for (const int transparency : {25, 65, 100}) {
                    theme.setTransparency(transparency);
                    const qreal strength = 1 - transparency / 100.;
                    if (strength > 0) {
                        QTRY_COMPARE(background->property("imageCount").toInt(), 1);
                        QTRY_COMPARE(
                            findVisual(page, "discoverFlowArtwork")->property("status").toInt(), 1);
                    } else
                        QTRY_COMPARE(background->property("imageCount").toInt(), 0);
                    for (const auto desktop : {QColor("#e4e0d4"), QColor("#242738")}) {
                        root->setProperty("desktopVisible", true);
                        root->setProperty("desktopColor", desktop);
                        const auto frame = window->grabWindow();
                        QVERIFY(!frame.isNull());
                        if (!artifacts.isEmpty())
                            QVERIFY(frame.save(artifacts + QString("/transparent-%1-%2-%3-%4.png")
                                                               .arg(QTest::currentDataTag())
                                                               .arg(int(mode))
                                                               .arg(transparency)
                                                               .arg(desktop.lightness() > 128
                                                                        ? "light-backdrop"
                                                                        : "dark-backdrop")));
                        if (QGuiApplication::platformName() == "offscreen")
                            continue;
                        const auto ratio = frame.width() / qreal(window->width());
                        const auto channel = [](QColor color, int c) {
                            return c == 0 ? color.redF() : c == 1 ? color.greenF() : color.blueF();
                        };
                        for (const qreal fraction : {.085, .58, .96}) {
                            const int py = qRound(page->height() * fraction * zoom * ratio);
                            const auto y = (py + .5) / (zoom * ratio);
                            const auto top =
                                fraction < .12 ? .10 + .35 * (y / page->height() - .05) / .07 : 1.;
                            const auto p = qBound(
                                0., 1 - (page->height() - y) / qMin(300., page->height()), 1.);
                            const auto shade = p < .6 ? .45 * p / .6 : .45 + .33 * (p - .6) / .4;
                            const auto coverage = strength * (top * (1 - shade) + shade);
                            const auto pixel =
                                frame.pixelColor(qRound((page->width() - 100) * zoom * ratio), py);
                            for (int c = 0; c < 3; ++c) {
                                const auto base = strength * channel(theme.main(), c) +
                                                  (1 - strength) * channel(desktop, c);
                                const auto media =
                                    strength * (top * (1 - shade) * channel(QColor("#75b9a5"), c) +
                                                shade * channel(theme.main(), c));
                                const auto expected = media + (1 - coverage) * base;
                                QVERIFY2(
                                    qAbs(channel(pixel, c) - expected) <= 4. / 255,
                                    qPrintable(
                                        QString(
                                            "transparent=%1 y=%2 channel=%3 actual=%4 expected=%5")
                                            .arg(transparency)
                                            .arg(y)
                                            .arg(c)
                                            .arg(channel(pixel, c))
                                            .arg(expected)));
                            }
                        }
                    }
                    // Read the native RGBA target itself, independently of any
                    // wallpaper. Business text and controls must stay opaque.
                    root->setProperty("desktopVisible", false);
                    if (QGuiApplication::platformName() != "offscreen") {
                        const auto frame = window->grabWindow();
                        const auto ratio = frame.width() / qreal(window->width());
                        const auto pixel =
                            frame.pixelColor(qRound((page->width() - 100) * zoom * ratio),
                                             qRound(page->height() * .4 * zoom * ratio));
                        QVERIFY(qAbs(pixel.alphaF() - (2 * strength - strength * strength)) <=
                                3. / 255);
                        for (const auto &name : {"discoverFlowTitle", "discoverFlowPlay"}) {
                            auto *item = findVisual(page, name);
                            QVERIFY(item);
                            const auto rect = item->mapRectToScene(item->boundingRect());
                            const auto region =
                                frame.copy(QRectF(rect.topLeft() * ratio, rect.size() * ratio)
                                               .toAlignedRect());
                            int peakAlpha = 0;
                            for (int y = 0; y < region.height(); ++y)
                                for (int x = 0; x < region.width(); ++x)
                                    peakAlpha = qMax(peakAlpha, region.pixelColor(x, y).alpha());
                            QVERIFY(peakAlpha >= 250);
                        }
                    }
                }
            }
            root->setProperty("desktopVisible", true);
            theme.setTransparency(0);
            QTRY_COMPARE(background->property("imageCount").toInt(), 1);
            QTRY_COMPARE(findVisual(page, "discoverFlowArtwork")->property("status").toInt(), 1);
        }
        if (QString(QTest::currentDataTag()) == "normal") {
            auto *backdrop = findVisual(page, "discoverFlowBackdrop");
            auto *info = findVisual(page, "discoverFlowInfo");
            QVERIFY(backdrop && info);
            QCOMPARE(backdrop->property("imageCount").toInt(), 1);
            // A patterned cover verifies the UV crop against object-position
            // 35%, independently of the preceding uniform alpha probes.
            QImage bands(800, 800, QImage::Format_RGB32);
            for (int y = 0; y < bands.height(); ++y)
                for (int x = 0; x < bands.width(); ++x)
                    bands.setPixelColor(x, y, QColor(y * 255 / 799, 80, 200));
            const auto bandPath = dir.filePath("bands.png");
            QVERIFY(bands.save(bandPath));
            auto second = domainSong(102, QStringLiteral("第二首 Another Song"));
            second["cover_url"] = QUrl::fromLocalFile(bandPath).toString();
            service.discoverFlow()->replaceSongs({song, second});
            theme.setMotionEnabled(true);
            page->setProperty("m_activeIndex", 1);
            // Keep the old text visible during the exit transition.
            QCOMPARE(title->property("text").toString(), song["title"].toString());
            QTRY_VERIFY_WITH_TIMEOUT(backdrop->property("running").toBool(), 2000);
            QCOMPARE(backdrop->property("imageCount").toInt(), 2);
            QTRY_COMPARE_WITH_TIMEOUT(title->property("text").toString(),
                                      second["title"].toString(), 2000);
            QTRY_COMPARE_WITH_TIMEOUT(backdrop->property("imageCount").toInt(), 1, 2000);
            QCOMPARE(backdrop->property("source").toUrl(), QUrl::fromLocalFile(bandPath));
            QCOMPARE(gallery->source(), portraitUrl);
            QTRY_VERIFY_WITH_TIMEOUT(!info->property("running").toBool(), 2000);
            QVERIFY(!backdrop->property("running").toBool());
            QCOMPARE(playback.queue()->rowCount(), 0); // Presentation is not playback intent.
            if (QGuiApplication::platformName() != "offscreen") {
                const auto frame = window->grabWindow();
                const auto dpr = frame.width() / qreal(window->width());
                const qreal x = page->width() - 100, y = page->height() * .3;
                // At y=.3 the top mask is fully opaque. Undo only the known
                // bottom scrim before comparing the 35% vertical source crop.
                const auto distance = page->height() - y;
                const auto scrim = distance >= 300 ? 0. : .45 * (300 - distance) / 180;
                const auto crop = page->height() / page->width();
                const auto uv = (1 - crop) * .35 + (y / page->height()) * crop;
                const auto expected = qRound(uv * 255 * (1 - scrim) + theme.main().red() * scrim);
                const auto pixel = frame.pixelColor(qRound(x * dpr), qRound(y * dpr));
                QVERIFY2(qAbs(pixel.red() - expected) <= 3,
                         qPrintable(QString("35 percent crop actual=%1 expected=%2")
                                        .arg(pixel.red())
                                        .arg(expected)));
            }
            page->setProperty("m_activeIndex", 0);
            page->setProperty("m_activeIndex", 1);
            page->setProperty("m_activeIndex", 0);
            QVERIFY(backdrop->property("imageCount").toInt() <= 2);
            theme.setMotionEnabled(false);
            QTRY_VERIFY(!backdrop->property("running").toBool());
            QTRY_VERIFY(!info->property("running").toBool());
            QTRY_COMPARE(backdrop->property("imageCount").toInt(), 1);
            window->hide();
            QTRY_COMPARE(backdrop->property("imageCount").toInt(), 0);
            window->show();
            QTRY_COMPARE(backdrop->property("imageCount").toInt(), 1);
            QPointer<QQuickItem> retired(findVisual(page, "discoverFlowArtwork"));
            QVERIFY(retired);
            page->setProperty("m_activeIndex", 1);
            QPointer<QQuickItem> retiredPage(page);
            QVERIFY(router.navigate("free-listen"));
            QTRY_VERIFY(retired.isNull());
            QTRY_VERIFY(retiredPage.isNull());
            QCOMPARE(gallery->source(), portraitUrl);
            router.back();
            QTRY_VERIFY((page = findVisual(window->contentItem(), "discoverFlowPage")));
            QTRY_COMPARE(page->property("m_activeIndex").toInt(), 1);
            backdrop = findVisual(page, "discoverFlowBackdrop");
            info = findVisual(page, "discoverFlowInfo");
            QVERIFY(backdrop && info);
            QTRY_COMPARE(backdrop->property("imageCount").toInt(), 1);
            QTRY_VERIFY(findVisual(page, "discoverFlowArtwork"));
            QTRY_COMPARE(findVisual(page, "discoverFlowArtwork")->property("status").toInt(), 1);
            QTRY_VERIFY(!backdrop->property("running").toBool());
            QTRY_VERIFY(!info->property("running").toBool());
            if (QGuiApplication::platformName() != "offscreen") {
                Test::moveNativePointer(window, {5, 5});
                // Flush the frame requested by the asynchronous image upload
                // before observing an idle interval; resource construction alone
                // does not mean the returned page has been presented yet.
                QVERIFY(!window->grabWindow().isNull());
                QTest::qWait(100);
                Test::FrameSpy frames(window);
                // Native upload/material retirement can present a late frame.
                // Require two quiet intervals within a bounded drain, while a
                // persistent animation clock still fails this observation.
                int quietIntervals = 0, settlingFrames = 0;
                for (int interval = 0; interval < 8 && quietIntervals < 2; ++interval) {
                    frames.clear();
                    QTest::qWait(250);
                    settlingFrames += frames.size();
                    quietIntervals = frames.isEmpty() ? quietIntervals + 1 : 0;
                }
                qInfo() << "FLOW return settling_frames=" << settlingFrames
                        << "consecutive_idle_250ms_intervals=" << quietIntervals;
                QCOMPARE(quietIntervals, 2);
            }
        }
        QCOMPARE(warnings.size(), 0);
        root.reset();
        service.shutdown();
        router.shutdown();
        QSignalSpy drain(&playback, &PlaybackService::closed);
        playback.shutdown();
        QTRY_COMPARE_WITH_TIMEOUT(drain.size(), 1, 15000);
        api.shutdown();
    }
    void homeAgreementAndLegalParagraphAlignment()
    {
        QTemporaryDir dir;
        EchoMusic::SettingsStore prefs(dir.filePath("prefs.ini"));
        ThemeStore theme(prefs);
        theme.setMotionEnabled(false);
        theme.setFloatingFrosted(false);
        ShellSettings shell(prefs);
        QVERIFY(!shell.userAgreementAccepted());
        ApiClient api(DeviceIdentity::temporary());
        AccountService account(api);
        PlaybackService playback(api, prefs, nullptr, dir.filePath("playback.sqlite"));
        RouteController router;
        DiscoveryService service(api, playback, account);
        DiscoveryRouteCoordinator routes(service, router, false);
        QQmlEngine engine;
        theme.attachToEngine(engine);
        QSignalSpy warnings(&engine, &QQmlEngine::warnings);
        QSignalSpy quit(&engine, &QQmlEngine::quit);
        QQmlComponent component(&engine);
        component.setData(R"(
            import QtQuick
            import QtQuick.Controls.Basic
            import EchoMusic.Shell
            import EchoMusic.Theme
            ApplicationWindow {
                id: testWindow
                required property RouteController router
                required property ShellSettings settings
                visible: true; width: 1150; height: 800
                color: Theme.main
                Item {
                    width: parent.width / testWindow.settings.uiScale
                    height: parent.height / testWindow.settings.uiScale
                    scale: testWindow.settings.uiScale; transformOrigin: Item.TopLeft
                    RouteHost { anchors.fill: parent; router: testWindow.router; settings: testWindow.settings }
                }
            })",
                          QUrl("qrc:/home-agreement-test.qml"));
        QScopedPointer<QObject> root(component.createWithInitialProperties(
            {{"router", QVariant::fromValue(&router)}, {"settings", QVariant::fromValue(&shell)}}));
        QVERIFY2(root, qPrintable(component.errorString()));
        auto *window = qobject_cast<QQuickWindow *>(root.data());
        QVERIFY(window);
        QVERIFY(Test::prepareNativeInput(window));
        const auto nativePopups = [&] {
            QSet<QObject *> visited;
            int count = 0;
            const auto visit = [&](auto &&self, QObject *object) -> void {
                if (!object || visited.contains(object))
                    return;
                visited.insert(object);
                count += object->inherits("QQuickPopup");
                for (auto *child : object->children())
                    self(self, child);
                if (auto *item = qobject_cast<QQuickItem *>(object))
                    for (auto *child : item->childItems())
                        self(self, child);
            };
            visit(visit, root.data());
            return count;
        };
        QCOMPARE(nativePopups(), 0);
        QVERIFY(!root->findChild<QObject *>("userAgreementDialog"));
        QVERIFY(router.navigate("home"));
        QPointer<QObject> agreement;
        QTRY_VERIFY(
            (agreement = findVisualOwnerObject(window->contentItem(), "userAgreementDialog")));
        QTRY_VERIFY(agreement->property("opened").toBool());
        QCOMPARE(nativePopups(), 1);
        QVERIFY(!agreement->property("closable").toBool());
        QTest::keyClick(window, Qt::Key_Escape);
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, {4, 4});
        QTest::qWait(50);
        QVERIFY(agreement && agreement->property("visible").toBool());
        auto *reject = findVisual(window->contentItem(), "userAgreementReject");
        QVERIFY(reject);
        QVERIFY(QMetaObject::invokeMethod(reject, "clicked"));
        QCOMPARE(quit.size(), 1);
        QVERIFY(!shell.userAgreementAccepted());
        QVERIFY(!prefs.value("legal/userAgreementAccepted", false).toBool());
        const auto artifacts = qEnvironmentVariable("ECHO_LEGAL_ARTIFACTS");
        if (!artifacts.isEmpty())
            QVERIFY(QDir().mkpath(artifacts));
        auto verifyParagraphs = [&](QObject *popup, const QString &name) {
            auto *content = popup->property("contentItem").value<QQuickItem *>();
            QVERIFY(content);
            QList<QQuickItem *> pending{content}, sections;
            while (!pending.isEmpty()) {
                auto *item = pending.takeLast();
                pending.append(item->childItems());
                if (item->objectName() == "legalSection")
                    sections.append(item);
            }
            QCOMPARE(sections.size(), 7);
            Test::FrameSpy frames(window);
            window->update();
            QTRY_VERIFY(!frames.isEmpty());
            const auto image = window->grabWindow();
            QVERIFY(!image.isNull());
            int measured = 0;
            for (auto *section : sections) {
                auto *title = findVisual(section, "legalSectionTitle");
                auto *paragraph = findVisual(section, "legalSectionContent");
                QVERIFY(title && paragraph);
                const auto firstLine = QMarginsF(0, 0, 0, qMax(0., paragraph->height() - 24));
                const auto titleInk = Test::renderedInkCenter(
                    image, window, title, title->property("color").value<QColor>());
                const auto paragraphInk = Test::renderedInkCenter(
                    image, window, paragraph, paragraph->property("color").value<QColor>(),
                    firstLine);
                if (!titleInk || !paragraphInk)
                    continue; // Rows outside the clipped scrolling viewport are not painted.
                const auto error = qAbs(*titleInk - *paragraphInk);
                QVERIFY2(error <= 1.5 * shell.uiScale(),
                         qPrintable(QString("%1 / %2: first-line ink differs by %3px")
                                        .arg(name, title->property("text").toString())
                                        .arg(error)));
                ++measured;
            }
            QVERIFY(measured >= 3);
            if (!artifacts.isEmpty())
                QVERIFY(image.save(artifacts + "/" + name + ".png"));
        };
        for (const auto mode : {ThemeStore::Light, ThemeStore::Dark}) {
            theme.setMode(mode);
            for (const auto scale : {1., 1.25}) {
                shell.resetZoom();
                if (scale > 1)
                    for (int i = 0; i < 5; ++i)
                        shell.zoomIn();
                verifyParagraphs(agreement, QString("agreement-%1-%2").arg(int(mode)).arg(scale));
            }
        }
        window->hide();
        QTRY_VERIFY(agreement.isNull());
        QCOMPARE(nativePopups(), 0);
        QVERIFY(!shell.userAgreementAccepted());
        window->show();
        QVERIFY(Test::prepareNativeInput(window));
        QTRY_VERIFY(
            (agreement = findVisualOwnerObject(window->contentItem(), "userAgreementDialog")));
        QTRY_VERIFY(agreement->property("opened").toBool());
        auto *accept = findVisual(window->contentItem(), "userAgreementAccept");
        QVERIFY(accept);
        Test::FrameSpy acceptedFrame(window);
        window->update();
        QTRY_VERIFY(!acceptedFrame.isEmpty());
        QTest::mouseClick(
            window, Qt::LeftButton, Qt::NoModifier,
            accept->mapToScene({accept->width() / 2, accept->height() / 2}).toPoint());
        QTRY_VERIFY(shell.userAgreementAccepted());
        QTRY_VERIFY(agreement.isNull());
        QCOMPARE(nativePopups(), 0);
        EchoMusic::SettingsStore reread(dir.filePath("prefs.ini"));
        ShellSettings restored(reread);
        QVERIFY(restored.userAgreementAccepted());
        QVERIFY(router.navigate("ranking"));
        QVERIFY(router.navigate("home"));
        QTest::qWait(100);
        QVERIFY(!findVisualOwnerObject(window->contentItem(), "userAgreementDialog"));
        QCOMPARE(nativePopups(), 0);
        QQmlComponent disclaimerComponent(
            &engine, QUrl("qrc:/qt/qml/EchoMusic/Settings/SettingsDisclaimerDialog.qml"));
        QScopedPointer<QObject> disclaimer(disclaimerComponent.createWithInitialProperties(
            {{"parent", QVariant::fromValue(window->contentItem())}}));
        QVERIFY2(disclaimer, qPrintable(disclaimerComponent.errorString()));
        QVERIFY(QMetaObject::invokeMethod(disclaimer.data(), "open"));
        QTRY_VERIFY(disclaimer->property("opened").toBool());
        for (const auto mode : {ThemeStore::Light, ThemeStore::Dark}) {
            theme.setMode(mode);
            for (const auto scale : {1., 1.25}) {
                shell.resetZoom();
                if (scale > 1)
                    for (int i = 0; i < 5; ++i)
                        shell.zoomIn();
                disclaimer->setProperty("uiScale", scale);
                verifyParagraphs(disclaimer.data(),
                                 QString("disclaimer-%1-%2").arg(int(mode)).arg(scale));
            }
        }
        QVERIFY(QMetaObject::invokeMethod(disclaimer.data(), "close"));
        QTRY_VERIFY(!disclaimer->property("visible").toBool());
        QCOMPARE(warnings.size(), 0);
        api.shutdown();
    }
    void songSearchClearRestoresTheActualCollection()
    {
        QTemporaryDir dir;
        EchoMusic::SettingsStore prefs(dir.filePath("prefs.ini"));
        ThemeStore theme(prefs);
        ApiClient api(DeviceIdentity::temporary());
        PlaybackService playback(api, prefs, nullptr, dir.filePath("playback.sqlite"));
        SongCatalogModel collection;
        collection.replaceSongs(
            {domainSong(1, "Alpha"), domainSong(2, "Beta"), domainSong(3, "Delta")});
        SongCatalogActions actions(collection, playback);
        QQmlEngine engine;
        theme.attachToEngine(engine);
        QSignalSpy warnings(&engine, &QQmlEngine::warnings);
        QQmlComponent component(&engine);
        component.setData(R"(
            import QtQuick
            import QtQuick.Window
            import EchoMusic.Music
            Window {
                id: window
                width: 1000; height: 700; visible: true
                required property SongCatalogModel collection
                required property SongCatalogActions actions
                SongCatalogView {
                    anchors.fill: parent
                    collection: window.collection
                    actions: window.actions
                    expandedHeaderHeight: 0
                    headerComponent: Component { Item {} }
                }
            }
        )",
                          QUrl("qrc:/song-clear-test.qml"));
        QScopedPointer<QObject> root(
            component.createWithInitialProperties({{"collection", QVariant::fromValue(&collection)},
                                                   {"actions", QVariant::fromValue(&actions)}}));
        QVERIFY2(root, qPrintable(component.errorString()));
        auto *window = qobject_cast<QQuickWindow *>(root.data());
        QVERIFY(window);
        QVERIFY(Test::prepareNativeInput(window));
        auto *input = findVisual(window->contentItem(), "discoverySongFilter");
        QVERIFY(input);
        auto *clear = findVisual(input, "clearTextAction");
        QVERIFY(clear);
        collection.toggleSelected("2");
        input->forceActiveFocus(Qt::TabFocusReason);
        for (const auto key : QByteArray("Bet"))
            QTest::keyClick(window, key);
        QTRY_COMPARE(collection.query(), QString("Bet"));
        QCOMPARE(collection.count(), 1);
        QTest::keyClick(window, Qt::Key_Backspace);
        QCOMPARE(collection.query(), QString("Be"));
        QCOMPARE(collection.count(), 1);
        for (const auto query : {QString("Be"), QString("no-matching-song")}) {
            collection.setQuery(query);
            QTRY_COMPARE(input->property("text").toString(), query);
            QTRY_VERIFY(clear->isVisible());
            const auto point =
                clear->mapToScene({clear->width() / 2, clear->height() / 2}).toPoint();
            Test::moveNativePointer(window, point);
            QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, point);
            QTRY_COMPARE(collection.query(), QString());
            QCOMPARE(collection.count(), 3);
            QCOMPARE(collection.selectedCount(), 1);
            QCOMPARE(collection.item(1).value("resourceId").toString(), QString("2"));
            QVERIFY(input->hasActiveFocus());
            QVERIFY(!clear->isVisible());
        }
        QCOMPARE(warnings.size(), 0);
        root.reset();
        QSignalSpy closed(&playback, &PlaybackService::closed);
        playback.shutdown();
        QTRY_COMPARE_WITH_TIMEOUT(closed.size(), 1, 15000);
        api.shutdown();
    }

    void catalogInsertionCallbackRetiresWithItsView()
    {
        QTemporaryDir dir;
        SettingsStore prefs(dir.filePath("prefs.ini"));
        ApiClient api(DeviceIdentity::temporary());
        AccountService account(api);
        PlaybackService playback(api, prefs, nullptr, dir.filePath("playback.sqlite"));
        ThemeStore theme(prefs);
        DiscoveryService service(api, playback, account);
        service.dailySongs()->replaceSongs({domainSong(1)});
        QQmlEngine engine;
        theme.attachToEngine(engine);
        QStringList warnings;
        QObject observer;
        connect(&engine, &QQmlEngine::warnings, &observer, [&](const auto &messages) {
            for (const auto &m : messages)
                warnings.append(m.toString());
        });
        QQmlComponent component(&engine);
        component.setData(R"(import QtQuick
import EchoMusic.Controls
import EchoMusic.Music
Window {
 id:host;required property SongCatalogModel collection;required property SongCatalogActions commands;property bool loaded:false;property bool showBody:true;visible:true;width:600;height:400
 EchoPageScrollContainer {id:viewport;anchors.fill:parent;scrollView:null;pageActive:true}
 Loader {anchors.fill:parent;active:host.showBody;sourceComponent:CatalogSongList {collection:host.collection;actions:host.commands;pageContainer:viewport;loading:!host.loaded}}
})",
                          QUrl("qrc:/catalog-insertion-retirement.qml"));
        QScopedPointer<QObject> root(component.createWithInitialProperties(
            {{"collection", QVariant::fromValue(service.dailySongs())},
             {"commands", QVariant::fromValue(service.actions(service.dailySongs()))}}));
        QVERIFY2(root, qPrintable(component.errorString()));
        for (int i = 0; i < 20; ++i) {
            root->setProperty("showBody", true);
            root->setProperty("loaded", false);
            root->setProperty("loaded", true);
            root->setProperty("showBody", false);
            QCoreApplication::processEvents();
        }
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
        root.reset();
        service.shutdown();
        QSignalSpy drain(&playback, &PlaybackService::closed);
        playback.shutdown();
        QTRY_COMPARE_WITH_TIMEOUT(drain.size(), 1, 15000);
        api.shutdown();
    }
    void recommendationItemsUseSharedColdSourceAndRetireOldReads()
    {
        QTemporaryDir dir;
        SessionDatabase database(dir.filePath("session"));
        RecommendationItemsModel model;
        model.attach(&database);
        QJsonArray rows;
        for (int i = 0; i < 1000; ++i)
            rows.append(QJsonObject{
                {"title", QString::number(i)},
                {"url", "https://example.invalid/" + QString::number(i)},
                {"provider_extension", QJsonObject{{"full_fact", QString(4096, 'x')}}}});
        model.replace(rows);
        QCOMPARE(model.count(), 1000);
        QTRY_VERIFY_WITH_TIMEOUT(DiscoveryServiceTestAccess::itemPersisted(model), 15000);
        for (int i = 0; i < 1000; i += 7) {
            model.data(model.index(i), Qt::UserRole + 1);
            QTRY_COMPARE(
                model.data(model.index(i), Qt::UserRole + 1).toMap().value("title").toString(),
                QString::number(i));
        }
        const auto stats = DiscoveryServiceTestAccess::itemStatistics(model);
        QVERIFY(stats.entries <= 32);
        QVERIFY(stats.cost <= 1024 * 1024);
        bool complete = false;
        QObject consumer;
        model.requestItem(999, &consumer, [&](QJsonObject row) {
            QCOMPARE(row["provider_extension"].toObject()["full_fact"].toString().size(), 4096);
            complete = true;
        });
        QTRY_VERIFY(complete);
        bool stale = false;
        model.requestItem(500, &consumer, [&](QJsonObject) { stale = true; });
        model.replace({QJsonObject{{"title", "replacement"}}});
        QTest::qWait(50);
        QVERIFY(!stale);
        QCOMPARE(model.count(), 1);
        QSignalSpy closed(&database, &SessionDatabase::closed);
        database.shutdown();
        QTRY_COMPARE(closed.size(), 1);
    }
    void recommendationConfigurationPersistsAndKeepsOneCard()
    {
        QTemporaryDir dir;
        SettingsStore prefs(dir.filePath("prefs.ini"));
        ApiClient api(DeviceIdentity::temporary());
        AccountService account(api);
        PlaybackService playback(api, prefs, nullptr, dir.filePath("playback.sqlite"));
        DiscoveryService service(api, playback, account);
        service.setPreferences(&prefs);
        auto *layout = service.recommendationCards();
        QVERIFY(!layout->find("3104"));
        QVERIFY(!layout->find("3106"));
        QCOMPARE(layout->catalog().size(), 12);
        QVERIFY(!layout->find("3007"));
        QVERIFY(!layout->find("3032"));
        QAbstractItemModelTester check(layout,
                                       QAbstractItemModelTester::FailureReportingMode::QtTest);
        const int original = layout->count();
        QCOMPARE(original, 4);
        QCOMPARE(layout->at(3)->key(), "new");
        const QStringList previousDefaults{"daily", "style", "3001", "3004", "3005", "3006", "3014",
                                           "3101",  "3102",  "3103", "3105", "3106", "new"};
        prefs.setValue("discovery/cards", previousDefaults);
        prefs.setValue("discovery/cardsDefaultsVersion", 0);
        layout->setPreferences(&prefs);
        QCOMPARE(layout->count(), 4);
        QCOMPARE(prefs.value("discovery/cardsDefaultsVersion").toInt(), 1);
        // A later explicit full selection remains the user's choice.
        prefs.setValue("discovery/cards", previousDefaults);
        layout->setPreferences(&prefs);
        QCOMPARE(layout->count(), previousDefaults.size() - 2);
        QVERIFY(!prefs.value("discovery/cards").toStringList().contains("3106"));
        prefs.setValue("discovery/cards",
                       QStringList{"3104", "3007", "style", "3106", "3105", "3032", "new"});
        layout->setPreferences(&prefs);
        QCOMPARE(prefs.value("discovery/cards").toStringList(), QStringList({"style", "new"}));
        prefs.setValue("discovery/cards", QStringList{"3104", "3105", "3106", "3007", "3032"});
        layout->setPreferences(&prefs);
        QCOMPARE(layout->count(), 4);
        layout->toggle("3104");
        layout->toggle("3106");
        QCOMPARE(layout->count(), 4);
        layout->restoreDefaults();
        const auto second = layout->at(1)->key();
        layout->move(1, 0);
        QCOMPARE(layout->at(0)->key(), second);
        QCOMPARE(prefs.value("discovery/cards").toStringList().first(), second);
        while (layout->count() > 1)
            layout->toggle(layout->at(layout->count() - 1)->key());
        layout->toggle(layout->at(0)->key());
        QCOMPARE(layout->count(), 1);
        layout->toggle("new");
        QCOMPARE(layout->count(), 2);
        layout->setPreferences(&prefs);
        QCOMPARE(layout->count(), 2);
        layout->restoreDefaults();
        QCOMPARE(layout->count(), original);
        QCOMPARE(layout->at(0)->key(), "daily");
        // A style-filter click also joins the shared card scheduler.
        DiscoveryServiceTestAccess::occupyCardRequests(service);
        service.toggleTag("tag");
        QCOMPARE(service.selectedTags(), QStringList{"tag"});
        QCOMPARE(service.state(DiscoveryService::Style), int(DiscoveryService::Idle));
        QVERIFY(!DiscoveryServiceTestAccess::hasPending(service));
        service.shutdown();
        QSignalSpy drain(&playback, &PlaybackService::closed);
        playback.shutdown();
        QTRY_COMPARE_WITH_TIMEOUT(drain.size(), 1, 15000);
        api.shutdown();
    }
    void recommendationContextStaysWithEachColdSource()
    {
        QTemporaryDir dir;
        ApiClient api(DeviceIdentity::temporary());
        SessionDatabase database(dir.filePath("recommendations"));
        SongMetadataService metadata(api);
        metadata.enableSqlIndex(database);
        SongCatalogModel first, second;
        for (auto *model : {&first, &second}) {
            model->attachMetadata(&metadata);
            model->setMetadataActive(false);
            model->enableSessionStorage(database);
        }
        auto song = domainSong(1);
        auto facts = song["metadata"].toObject();
        facts["recommendation"] =
            QJsonObject{{"detail", "来源甲的推荐语"},
                        {"curator", QJsonObject{{"name", "甲"}, {"avatar_url", "image://home/1"}}}};
        song["metadata"] = facts;
        first.replaceSongs({song});
        facts["recommendation"] = QJsonObject{{"detail", "来源乙的推荐语"}};
        song["metadata"] = facts;
        second.replaceSongs({song});
        QTRY_VERIFY(first.storageReady() && second.storageReady());
        QTRY_COMPARE(first.data(first.index(0), SongCatalogModel::Recommendation)
                         .toMap()["detail"]
                         .toString(),
                     QString("来源甲的推荐语"));
        QTRY_COMPARE(second.data(second.index(0), SongCatalogModel::Recommendation)
                         .toMap()["detail"]
                         .toString(),
                     QString("来源乙的推荐语"));
        QVERIFY(!SongMetadataService::observationInput(song)["metadata"].toObject().contains(
            "recommendation"));
        QVERIFY(SongCatalogStorageTestAccess::statistics(first).entries <= 256);
        QSignalSpy closed(&database, &SessionDatabase::closed);
        database.shutdown();
        QTRY_COMPARE(closed.size(), 1);
        api.shutdown();
    }
    void rankingCatalogGroupsKeepServerOrderAndIntroduction_data()
    {
        QTest::addColumn<bool>("failFirst");
        QTest::addColumn<bool>("topFirst");
        QTest::addColumn<bool>("dark");
        QTest::newRow("catalog-first-light") << false << false << false;
        QTest::newRow("recommendations-first-dark") << false << true << true;
        QTest::newRow("retry-directory") << true << false << false;
    }
    void rankingCatalogGroupsKeepServerOrderAndIntroduction()
    {
        QFETCH(bool, failFirst);
        QFETCH(bool, topFirst);
        QFETCH(bool, dark);
        QTemporaryDir dir;
        SettingsStore prefs(dir.filePath("prefs.ini"));
        prefs.setValue("playback/volume", 0.);
        ApiClient api(DeviceIdentity::temporary());
        QList<QJsonObject> requests;
        QHash<QString, QList<ApiInterceptDone>> pending;
        const auto interceptor = api.addInterceptor(
            1, [&](const QJsonObject &request, ApiInterceptNext, ApiInterceptDone done) {
                const auto operation = request["operation"].toString();
                if (operation == "rank_list" || operation == "rank_top" ||
                    operation == "rank_audio") {
                    requests.append(request);
                    pending[operation].append(std::move(done));
                } else
                    QTimer::singleShot(0, &api, [done = std::move(done)] { done({1, "{}"}); });
                return std::function<void()>{};
            });
        PlaybackService playback(api, prefs, nullptr, dir.filePath("playback.sqlite"));
        AccountService account(api);
        DiscoveryService service(api, playback, account);
        RouteController router;
        DiscoveryRouteCoordinator routes(service, router, true);
        ThemeStore theme(prefs);
        theme.setMode(dark ? ThemeStore::Dark : ThemeStore::Light);
        theme.setMotionEnabled(false);
        ShellSettings settings(prefs);
        settings.acceptUserAgreement();
        QQmlEngine engine;
        theme.attachToEngine(engine);
        QSignalSpy warnings(&engine, &QQmlEngine::warnings);
        QQmlComponent component(&engine);
        component.setData(R"(import QtQuick
import QtQuick.Controls.Basic
import EchoMusic.Shell
ApplicationWindow {
 id: host
 required property RouteController router
 required property ShellSettings settings
 visible: true; width: 1100; height: 800
 RouteHost { anchors.fill: parent; router: host.router; settings: host.settings }
})",
                          QUrl("qrc:/ranking-complete-catalog.qml"));
        QScopedPointer<QObject> root(
            component.createWithInitialProperties({{"router", QVariant::fromValue(&router)},
                                                   {"settings", QVariant::fromValue(&settings)}}));
        QVERIFY2(root, qPrintable(component.errorString()));
        auto *window = qobject_cast<QQuickWindow *>(root.data());
        QVERIFY(window);
        QVERIFY(router.navigate("ranking"));
        QTRY_COMPARE(pending["rank_list"].size(), 1);
        QTRY_COMPARE(pending["rank_top"].size(), 1);
        QCOMPARE(requests.size(), 2);
        const auto deliver = [&](const QString &operation, const QJsonValue &data,
                                 bool failed = false) {
            auto done = pending[operation].takeFirst();
            done({failed ? 1 : 0,
                  (data.isArray() ? QJsonDocument(data.toArray()) : QJsonDocument(data.toObject()))
                      .toJson()});
        };
        if (failFirst) {
            deliver("rank_list", QJsonObject{{"message", "目录请求失败"}}, true);
            QTRY_COMPARE(service.state(DiscoveryService::RankCatalog), DiscoveryService::Error);
            QQuickItem *page = nullptr;
            QTRY_VERIFY((page = findVisual(window->contentItem(), "rankingPage")));
            QTRY_COMPARE(page->property("part").toInt(), int(DiscoveryService::RankCatalog));
            service.retry(DiscoveryService::RankCatalog);
            QTRY_COMPARE(pending["rank_list"].size(), 1);
            QCOMPARE(requests.last()["operation"].toString(), QString("rank_list"));
        }
        QJsonArray catalog;
        const QString description = "酷狗音乐人原创作品官方榜单，以推荐优秀原创作品为目的。";
        for (int i = 1; i <= 56; ++i)
            catalog.append(QJsonObject{{"id", i},
                                       {"title", QString("完整目录榜单 %1").arg(i)},
                                       {"description", description},
                                       {"category", i % 2 + 1},
                                       {"published_at", "2026-11-25 10:10:02"},
                                       {"update_frequency", "每天"}});
        const QJsonArray recommended{catalog[16], catalog[2], catalog[8]};
        deliver(topFirst ? "rank_top" : "rank_list", topFirst ? recommended : catalog);
        QTest::qWait(10);
        QVERIFY(pending["rank_audio"].isEmpty());
        // The first recommendation can be any rank. No city/name/id is pinned.
        deliver(topFirst ? "rank_list" : "rank_top", topFirst ? catalog : recommended);
        QTRY_COMPARE(service.recommendedRanks().size(), 3);
        QCOMPARE(service.recommendedRanks()[0].toMap()["id"].toInt(), 17);
        QCOMPARE(service.recommendedRanks()[1].toMap()["id"].toInt(), 3);
        QCOMPARE(service.recommendedRanks()[2].toMap()["id"].toInt(), 9);
        QTRY_COMPARE(service.ranks().size(), 56);
        QCOMPARE(service.ranks()[0].toMap()["id"].toInt(), 17);
        QCOMPARE(service.ranks()[3].toMap()["id"].toInt(), 1);
        QTRY_COMPARE(pending["rank_audio"].size(), 1);
        QCOMPARE(requests.last()["query"].toObject()["rank_id"].toInt(), 17);
        deliver("rank_audio", QJsonObject{{"songs", QJsonArray{domainSong(17)}}, {"total", 100}});
        QQuickItem *intro = nullptr, *header = nullptr;
        QTRY_VERIFY((intro = findVisual(window->contentItem(), "rankingIntroduction")));
        QTRY_VERIFY((header = findVisual(window->contentItem(), "songDetailHeader")));
        QCOMPARE(header->property("heading").toString(), QString("排行榜"));
        QCOMPARE(header->property("subtitle").toString(), QString());
        QCOMPARE(header->property("description").toString(), QString());
        QCOMPARE(intro->property("text").toString(), description);
        QCOMPARE(intro->property("font").value<QFont>().weight(), QFont::DemiBold);
        QVERIFY(!findVisual(header, "songDetailDescription")->isVisible());
        auto *metadata = findVisual(header, "songDetailSecondaryDetails");
        QVERIFY(metadata);
        QVERIFY(intro->mapToItem(header, QPointF()).y() <
                metadata->mapToItem(header, QPointF()).y());
        QCOMPARE(header->property("expandedHeight").toReal(), 176.);
        QVERIFY(QMetaObject::invokeMethod(intro, "clicked"));
        QPointer<QObject> introDialog;
        QTRY_VERIFY((introDialog = findVisualOwnerObject(window->contentItem(),
                                                         "rankingIntroductionDialog")));
        QTRY_VERIFY(introDialog->property("opened").toBool());
        QCOMPARE(introDialog->property("text").toString(), description);
        QVERIFY(QMetaObject::invokeMethod(introDialog, "close"));
        QTRY_VERIFY(introDialog.isNull());
        auto *choose = findVisual(window->contentItem(), "discoveryChooseRank");
        QVERIFY(choose);
        QVERIFY(QMetaObject::invokeMethod(choose, "clicked"));
        QPointer<QObject> selector;
        QTRY_VERIFY(
            (selector = findVisualOwnerObject(window->contentItem(), "discoveryRankSelector")));
        QTRY_VERIFY(selector->property("opened").toBool());
        auto *options = findVisual(window->contentItem(), "discoveryRankOptions");
        QVERIFY(options);
        QCOMPARE(selector->property("activeTab").toInt(), 0);
        const auto capture = [&](const QString &phase) {
            const auto output = qEnvironmentVariable("ECHO_RANK_GROUP_ARTIFACTS");
            if (output.isEmpty())
                return;
            QVERIFY(QDir().mkpath(output));
            QImage frame;
            QTRY_VERIFY(!(frame = window->grabWindow()).isNull());
            QVERIFY(frame.save(output + QString("/%1-%2-%3.png")
                                            .arg(dark ? "dark" : "light")
                                            .arg(failFirst ? "retry" : "ready")
                                            .arg(phase)));
        };
        capture("recommended");
        auto *groups = findVisual(window->contentItem(), "discoveryRankGroups");
        QVERIFY(groups);
        QVERIFY(QMetaObject::invokeMethod(groups, "picked", Q_ARG(int, 1)));
        QTRY_COMPARE(selector->property("activeTab").toInt(), 1);
        QTRY_VERIFY(options->property("count").toInt() > 1);
        capture("all");
        QVERIFY(options->inherits("QQuickListView"));
        const auto rowCount = options->property("count").toInt();
        QVERIFY(rowCount > 0 && rowCount < service.ranks().size());
        // Variable-width choices are packed into native virtual rows rather than
        // stretching every name into two fixed columns or constructing all 56.
        QVERIFY(countVisual(window->contentItem(), "discoveryRankOption") < 40);
        QVERIFY(QMetaObject::invokeMethod(options, "positionViewAtEnd"));
        QQuickItem *lastRow = nullptr;
        QTRY_VERIFY(QMetaObject::invokeMethod(options, "itemAtIndex",
                                              Q_RETURN_ARG(QQuickItem *, lastRow),
                                              Q_ARG(int, rowCount - 1)) &&
                    lastRow);
        QQuickItem *last = nullptr;
        QTRY_VERIFY(([&] {
            QList<QQuickItem *> pendingItems{lastRow};
            while (!pendingItems.isEmpty()) {
                auto *item = pendingItems.takeLast();
                if (item->objectName() == "discoveryRankOption" &&
                    item->property("rankId").toInt() == 56) {
                    last = item;
                    return true;
                }
                pendingItems.append(item->childItems());
            }
            return false;
        })());
        QVERIFY(QMetaObject::invokeMethod(last, "clicked"));
        QTRY_VERIFY(selector.isNull());
        QTRY_COMPARE(pending["rank_audio"].size(), 1);
        QCOMPARE(service.selectedRank()["id"].toInt(), 56);
        QCOMPARE(requests.last()["query"].toObject()["rank_id"].toInt(), 56);
        deliver("rank_audio", QJsonObject{{"songs", QJsonArray{domainSong(56)}}, {"total", 10}});
        int directories = 0, recommendations = 0;
        for (const auto &request : requests) {
            directories += request["operation"] == "rank_list";
            recommendations += request["operation"] == "rank_top";
        }
        QCOMPARE(directories, failFirst ? 2 : 1);
        QCOMPARE(recommendations, 1);
        QCOMPARE(warnings.size(), 0);
        root.reset();
        service.shutdown();
        api.removeInterceptor(interceptor);
        QSignalSpy closed(&playback, &PlaybackService::closed);
        playback.shutdown();
        QTRY_COMPARE(closed.size(), 1);
        api.shutdown();
    }
    void rankingDirectoryFallbackAndRecommendationRefreshKeepSelection()
    {
        QTemporaryDir dir;
        SettingsStore prefs(dir.filePath("prefs.ini"));
        ApiClient api(DeviceIdentity::temporary());
        PlaybackService playback(api, prefs, nullptr, dir.filePath("playback.sqlite"));
        AccountService account(api);
        DiscoveryService service(api, playback, account);
        service.setPage("ranking", "ranking-fallback", false);
        DiscoveryServiceTestAccess::pending(service, DiscoveryService::RankTop, 101);
        DiscoveryServiceTestAccess::pending(service, DiscoveryService::RankCatalog, 102);
        const QJsonArray catalog{
            QJsonObject{{"id", 1}, {"title", "榜一"}}, QJsonObject{{"id", 2}, {"title", "榜二"}},
            QJsonObject{{"id", 3}, {"title", "榜三"}, {"published_at", "2026-11-25"}}};
        DiscoveryServiceTestAccess::complete(service, DiscoveryService::RankCatalog, catalog, 102);
        QVERIFY(service.ranks().isEmpty());
        DiscoveryServiceTestAccess::fail(service, DiscoveryService::RankTop, 101);
        QCOMPARE(service.ranks().size(), 3);
        QVERIFY(service.recommendedRanks().isEmpty());
        QCOMPARE(service.selectedRank()["id"].toInt(), 1);
        const QJsonObject third{{"id", 3}, {"title", "榜三"}};
        const QJsonObject second{{"id", 2}, {"title", "榜二"}};
        DiscoveryServiceTestAccess::complete(service, DiscoveryService::RankTop,
                                             QJsonArray{third, second, third}, 103);
        QCOMPARE(service.ranks().size(), 3);
        QCOMPARE(service.recommendedRanks().size(), 2);
        QCOMPARE(service.recommendedRanks()[0].toMap()["id"].toInt(), 3);
        QCOMPARE(service.recommendedRanks()[0].toMap()["published_at"].toString(),
                 QString("2026-11-25"));
        QCOMPARE(service.ranks()[2].toMap()["id"].toInt(), 1);
        QCOMPARE(service.selectedRank()["id"].toInt(), 1);
        DiscoveryServiceTestAccess::complete(service, DiscoveryService::RankTop,
                                             QJsonArray{second, third}, 104);
        QCOMPARE(service.recommendedRanks()[0].toMap()["id"].toInt(), 2);
        QCOMPARE(service.selectedRank()["id"].toInt(), 1);
        service.shutdown();
        QSignalSpy drained(&playback, &PlaybackService::closed);
        playback.shutdown();
        QTRY_COMPARE(drained.size(), 1);
        api.shutdown();
    }

    void recommendationFeedsPageDeduplicateRetryAndCancel_data()
    {
        QTest::addColumn<int>("cardId");
        QTest::newRow("style") << 3103;
        QTest::newRow("ai") << 3107;
    }
    void recommendationFeedsPageDeduplicateRetryAndCancel()
    {
        QFETCH(int, cardId);
        QTemporaryDir dir;
        SettingsStore prefs(dir.filePath("prefs.ini"));
        ApiClient api(DeviceIdentity::temporary());
        QList<QJsonObject> requests;
        QList<ApiInterceptDone> replies;
        auto interceptor = api.addInterceptor(
            1, [&](const QJsonObject &request, ApiInterceptNext, ApiInterceptDone done) {
                if (request["operation"] != "discovery_card_single") {
                    QTimer::singleShot(0, &api, [done = std::move(done)] { done({1, "{}"}); });
                    return std::function<void()>{};
                }
                requests.append(request);
                replies.append(std::move(done));
                return std::function<void()>{};
            });
        PlaybackService playback(api, prefs, nullptr, dir.filePath("playback.sqlite"));
        AccountService account(api);
        SessionDatabase database(dir.filePath("session"));
        SongMetadataService metadata(api);
        metadata.enableSqlIndex(database);
        playback.attachMetadata(metadata);
        DiscoveryService service(api, playback, account);
        RouteController router;
        DiscoveryRouteCoordinator routes(service, router, true);
        ThemeStore theme(prefs);
        theme.setMotionEnabled(false);
        ShellSettings settings(prefs);
        settings.acceptUserAgreement();
        QQmlEngine engine;
        theme.attachToEngine(engine);
        QSignalSpy warnings(&engine, &QQmlEngine::warnings);
        QQmlComponent component(&engine);
        component.setData(R"(import QtQuick
import QtQuick.Controls.Basic
import EchoMusic.Shell
ApplicationWindow {
 id: feedWindow
 required property RouteController router
 required property ShellSettings settings
 visible: true; width: 1200; height: 700
 RouteHost { anchors.fill: parent; router: feedWindow.router; settings: feedWindow.settings }
})",
                          QUrl("qrc:/recommendation-feed-scroll.qml"));
        QScopedPointer<QObject> root(
            component.createWithInitialProperties({{"router", QVariant::fromValue(&router)},
                                                   {"settings", QVariant::fromValue(&settings)}}));
        QVERIFY2(root, qPrintable(component.errorString()));
        auto *window = qobject_cast<QQuickWindow *>(root.data());
        QVERIFY(window);
        QVariantMap query;
        if (cardId == 3103)
            query = {{"tag", "9"}, {"label", "摇滚"}, {"group", "0"}};
        QVERIFY(router.navigate("recommendation-detail", query, QString::number(cardId)));
        auto *feed = service.recommendationFeed(router.current());
        QVERIFY(feed);
        QTRY_COMPARE(replies.size(), 1);
        QCOMPARE(requests.last()["query"].toObject()["card_id"].toInt(), cardId);
        auto deliver = [&](QJsonArray songs, bool failure = false) {
            auto reply = replies.takeFirst();
            reply({failure ? 1 : 0,
                   QJsonDocument(failure ? QJsonObject{{"message", "server test failure"}}
                                         : QJsonObject{{"songs", songs}})
                       .toJson()});
        };
        QJsonArray first;
        for (int i = 1; i <= 30; ++i)
            first.append(domainSong(i));
        deliver(first);
        QTRY_COMPARE(feed->collection()->totalCount(), 30);
        QTRY_VERIFY(feed->collection()->storageReady());
        feed->collection()->toggleSelected("1");
        QQuickItem *list = nullptr;
        QTRY_VERIFY((list = findVisual(window->contentItem(), "discoverySongs")));
        QCOMPARE(list->property("model").value<SongCatalogModel *>(), feed->collection());
        QVERIFY(QMetaObject::invokeMethod(list, "positionViewAtEnd"));
        QTRY_COMPARE(replies.size(), 1); // The production near-tail hook, not a direct model call.
        feed->loadMore();                // An overlapping request is coalesced.
        QVERIFY(QMetaObject::invokeMethod(list, "positionViewAtBeginning"));
        if (cardId == 3107)
            QCOMPARE(requests.last()["query"].toObject()["page"].toInt(), 2);
        deliver({domainSong(30), domainSong(31), domainSong(31)});
        QTRY_COMPARE(feed->collection()->totalCount(), 31);
        QTRY_VERIFY(feed->collection()->storageReady());
        QCOMPARE(feed->collection()->selectedIds(), QStringList{"1"});
        feed->loadMore();
        QTRY_COMPARE(replies.size(), 1);
        deliver({}, true);
        QTRY_VERIFY(!feed->errorMessage().isEmpty());
        QCOMPARE(feed->collection()->totalCount(), 31);
        const auto before = requests.size();
        feed->loadMore();
        QTest::qWait(30);
        QCOMPARE(requests.size(), before);
        feed->retry();
        QTRY_COMPARE(replies.size(), 1);
        deliver({domainSong(32)});
        QTRY_COMPARE(feed->collection()->totalCount(), 32);
        QTRY_VERIFY(feed->collection()->storageReady());
        feed->loadMore();
        QTRY_COMPARE(replies.size(), 1);
        router.setPresentationActive(false);
        auto stale = replies.takeFirst();
        stale({0, QJsonDocument(QJsonObject{{"songs", QJsonArray{domainSong(33)}}}).toJson()});
        QTest::qWait(30);
        QCOMPARE(feed->collection()->totalCount(), 32);
        router.setPresentationActive(true);
        QCOMPARE(service.recommendationFeed(router.current()), feed);
        feed->collection()->setQuery("not in the loaded recommendation batch");
        QTRY_COMPARE(feed->collection()->count(), 0);
        const auto filteredRequests = requests.size();
        QTest::qWait(50);
        QCOMPARE(requests.size(), filteredRequests);
        feed->collection()->setQuery("");
        QTRY_COMPARE(feed->collection()->count(), 32);
        feed->loadMore();
        QTRY_COMPARE(replies.size(), 1);
        deliver({domainSong(32)});
        QTRY_VERIFY(!feed->hasMore());
        QCOMPARE(feed->collection()->totalCount(), 32);
        QCOMPARE(warnings.size(), 0);
        root.reset();
        service.shutdown();
        api.removeInterceptor(interceptor);
        QSignalSpy drained(&playback, &PlaybackService::closed);
        playback.shutdown();
        QTRY_COMPARE(drained.size(), 1);
        QSignalSpy closed(&database, &SessionDatabase::closed);
        database.shutdown();
        QTRY_COMPARE(closed.size(), 1);
        api.shutdown();
    }
    void recommendationHomepagePlaybackContinuesWithoutDetail_data()
    {
        QTest::addColumn<int>("cardId");
        QTest::newRow("style") << 3103;
        QTest::newRow("ai") << 3107;
    }
    void recommendationHomepagePlaybackContinuesWithoutDetail()
    {
        QFETCH(int, cardId);
        QTemporaryDir dir;
        SettingsStore prefs(dir.filePath("prefs.ini"));
        prefs.setValue("playback/volume", 0.);
        prefs.setValue("playback/autoNext", false);
        ApiClient api(DeviceIdentity::temporary());
        QList<ApiInterceptDone> replies;
        auto interceptor = api.addInterceptor(
            1, [&](const QJsonObject &request, ApiInterceptNext, ApiInterceptDone done) {
                if (request["operation"] == "discovery_card_single")
                    replies.append(std::move(done));
                else
                    QTimer::singleShot(0, &api, [done = std::move(done)] { done({1, "{}"}); });
                return std::function<void()>{};
            });
        PlaybackService playback(api, prefs, nullptr, dir.filePath("playback.sqlite"));
        QTRY_VERIFY_WITH_TIMEOUT(playback.engineReady(), 15000);
        PlaybackServiceTestAccess::hold(playback); // Domain queue transitions, no audio source.
        AccountService account(api);
        SessionDatabase database(dir.filePath("session"));
        SongMetadataService metadata(api);
        metadata.enableSqlIndex(database);
        playback.attachMetadata(metadata);
        DiscoveryService service(api, playback, account);
        service.setPage("home", "stream-play", true);
        auto *preview = service.recommendationCard(QString::number(cardId));
        QJsonArray six;
        for (int i = 1; i <= 6; ++i)
            six.append(domainSong(i));
        preview->accept({{"songs", six}});
        QTRY_VERIFY(preview->collection()->storageReady());
        const auto tag = cardId == 3103 ? QString("9") : QString();
        const auto key = service.feedQueueKey(cardId, tag);
        service.playFeed(cardId, tag, "Test recommendation", "", 0, 6, 2);
        QTRY_COMPARE(playback.sourceKey(), key);
        QTRY_COMPARE(playback.trackId(), QString("3"));
        QCOMPARE(playback.queue()->rowCount(), 6);
        auto *feed = DiscoveryServiceTestAccess::stream(service, key);
        QVERIFY(feed);
        playback.playIndex(3); // Two tracks remain: prefetch without opening a detail page.
        QTRY_COMPARE(replies.size(), 1);
        QJsonArray next;
        for (int i = 7; i <= 36; ++i)
            next.append(domainSong(i));
        replies.takeFirst()({0, QJsonDocument(QJsonObject{{"songs", next}}).toJson()});
        QTRY_COMPARE(playback.queue()->rowCount(), 36);
        QTRY_COMPARE(feed->collection()->totalCount(), 36);
        QCOMPARE(preview->collection()->totalCount(), 6);
        QTRY_VERIFY(feed->collection()->storageReady());
        playback.playIndex(33);
        QTRY_COMPARE(replies.size(), 1);
        const auto ordinary = PlaybackTrack::fromDomain(domainSong(99));
        playback.playTracks({ordinary}, 0, "Other", "queue:ordinary");
        auto stale = replies.takeFirst();
        stale({0, QJsonDocument(QJsonObject{{"songs", QJsonArray{domainSong(37)}}}).toJson()});
        QTest::qWait(30);
        QCOMPARE(playback.sourceKey(), QString("queue:ordinary"));
        QCOMPARE(playback.queue()->rowCount(), 1);
        QCOMPARE(feed->collection()->totalCount(), 36);
        service.shutdown();
        api.removeInterceptor(interceptor);
        QSignalSpy drained(&playback, &PlaybackService::closed);
        playback.shutdown();
        QTRY_COMPARE(drained.size(), 1);
        QSignalSpy closed(&database, &SessionDatabase::closed);
        database.shutdown();
        QTRY_COMPARE(closed.size(), 1);
        api.shutdown();
    }
    void recommendationGroupRangesBorrowFullColdResources()
    {
        QTemporaryDir dir;
        ApiClient api(DeviceIdentity::temporary());
        SessionDatabase database(dir.filePath("session"));
        SongMetadataService metadata(api);
        metadata.enableSqlIndex(database);
        SongCatalogModel source, range;
        for (auto *model : {&source, &range}) {
            model->attachMetadata(&metadata);
            model->setMetadataActive(false);
            model->enableSessionStorage(database);
        }
        QJsonArray songs;
        for (int i = 1; i <= 18; ++i)
            songs.append(domainSong(i));
        source.replaceSongs(songs);
        QTRY_VERIFY(source.storageReady());
        range.replaceSourceRange(source, 6, 6);
        QObject observer;
        connect(&source, &SongCatalogModel::storageReadyChanged, &observer, [&] {
            if (source.storageReady())
                range.replaceSourceRange(source, 6, 6);
        });
        const auto accepted = readCatalogTracks(&range);
        QCOMPARE(accepted.size(), 6);
        QTRY_VERIFY(source.storageReady() && range.storageReady());
        const auto cold = readCatalogTracks(&range);
        QCOMPARE(cold.size(), 6);
        for (int i = 0; i < 6; ++i) {
            QCOMPARE(cold[i].id, QString::number(7 + i));
            QCOMPARE(cold[i].toCatalog(), accepted[i].toCatalog());
            QCOMPARE(cold[i].toCatalog()["metadata"].toObject()["resources"],
                     songs[i + 6].toObject()["metadata"].toObject()["resources"]);
        }
        range.toggleSelected("8");
        range.setQuery("Song");
        QTRY_COMPARE(range.count(), 6);
        range.replaceSourceRange(source, 6, 6);
        QCOMPARE(range.selectedIds(), QStringList{"8"});
        QCOMPARE(source.selectedCount(), 0);
        // Range entries lease the original records after the source retires.
        disconnect(&source, nullptr, &observer, nullptr);
        source.clear();
        const auto leased = readCatalogTracks(&range);
        QCOMPARE(leased.size(), 6);
        QCOMPARE(leased.first().toCatalog(), cold.first().toCatalog());
        QSignalSpy closed(&database, &SessionDatabase::closed);
        database.shutdown();
        QTRY_COMPARE(closed.size(), 1);
        api.shutdown();
    }
    void recommendationGroupPlayUsesOnlyItsColdRange()
    {
        QTemporaryDir dir;
        SettingsStore prefs(dir.filePath("prefs.ini"));
        prefs.setValue("playback/volume", 0.);
        ApiClient api(DeviceIdentity::temporary());
        PlaybackService playback(api, prefs, nullptr, dir.filePath("playback.sqlite"));
        SessionDatabase database(dir.filePath("session"));
        SongMetadataService metadata(api);
        metadata.enableSqlIndex(database);
        playback.attachMetadata(metadata);
        RecommendationCard card("3103", "曲风盲盒", playback);
        QJsonArray songs, groups;
        for (int i = 1; i <= 18; ++i) {
            auto song = domainSong(i);
            // Exercise queue publication without contacting a live music API.
            song["local_path"] = dir.filePath(QString("missing-%1.wav").arg(i));
            songs.append(song);
        }
        for (int i = 0; i < 3; ++i)
            groups.append(QJsonObject{{"title", "曲风 " + QString::number(i)},
                                      {"start_index", i * 6},
                                      {"song_count", 6}});
        card.accept({{"songs", songs}, {"groups", groups}});
        QTRY_VERIFY(card.collection()->storageReady());
        QTRY_VERIFY(DiscoveryServiceTestAccess::itemPersisted(*card.groups()));
        card.playGroup(1);
        QTRY_COMPARE(playback.sourceKey(), card.groupQueueKey(1));
        playback.stop();
        QCOMPARE(card.preparingGroup(), -1);
        QCOMPARE(playback.queue()->rowCount(), 6);
        for (int i = 0; i < 6; ++i)
            QCOMPARE(playback.queue()->at(i)->id, QString::number(i + 7));
        // A different style can occupy the same position after a refresh.
        auto refreshed = groups[1].toObject();
        refreshed["title"] = "新曲风";
        refreshed["start_index"] = 12;
        groups[1] = refreshed;
        card.accept({{"songs", songs}, {"groups", groups}});
        QTRY_VERIFY(card.collection()->storageReady());
        QTRY_VERIFY(DiscoveryServiceTestAccess::itemPersisted(*card.groups()));
        card.playGroup(1);
        QTRY_COMPARE(playback.queueTitle(), QString("新曲风"));
        playback.stop();
        QCOMPARE(playback.queue()->rowCount(), 6);
        QCOMPARE(playback.queue()->at(0)->id, QString("13"));
        card.playGroup(2);
        card.clear(); // Reset cancels an accepted, still-cold group metadata read.
        QCOMPARE(card.preparingGroup(), -1);
        QTest::qWait(100);
        QCOMPARE(playback.sourceKey(), card.groupQueueKey(1));
        QSignalSpy drained(&playback, &PlaybackService::closed);
        playback.shutdown();
        QTRY_COMPARE(drained.size(), 1);
        QSignalSpy closed(&database, &SessionDatabase::closed);
        database.shutdown();
        QTRY_COMPARE(closed.size(), 1);
        api.shutdown();
    }
    void recommendationContentLayoutsUseCanonicalModels_data()
    {
        QTest::addColumn<int>("viewWidth");
        QTest::newRow("wide") << 1560;
        QTest::newRow("narrow") << 960;
    }
    void recommendationContentLayoutsUseCanonicalModels()
    {
        if (QGuiApplication::platformName() != "offscreen")
            QSKIP("Nonintrusive content layout regression");
        QFETCH(int, viewWidth);
        QTemporaryDir dir;
        SettingsStore prefs(dir.filePath("prefs.ini"));
        prefs.setValue("playback/volume", 0.);
        ApiClient api(DeviceIdentity::temporary());
        AccountService account(api);
        PlaybackService playback(api, prefs, nullptr, dir.filePath("playback.sqlite"));
        SessionDatabase database(dir.filePath("session"));
        SongMetadataService metadata(api);
        metadata.enableSqlIndex(database);
        playback.attachMetadata(metadata);
        DiscoveryService service(api, playback, account);
        service.setPage("home", "content-layout", false);
        ThemeStore theme(prefs);
        theme.setMotionEnabled(false);
        theme.setFloatingFrosted(false);
        RouteController router;
        DiscoveryRouteCoordinator routes(service, router, false);
        EchoPageStateStore state;
        QJsonArray songs;
        for (int i = 1; i <= 30; ++i) {
            auto song = domainSong(i, i % 2 ? "Good Time" : "听见新的故事");
            song["cover_url"] = QString("image://home/%1").arg(i);
            auto facts = song["metadata"].toObject();
            facts["recommendation"] =
                QJsonObject{{"detail", i % 2 ? "音乐把今天变得不一样，分享这一刻的心动。"
                                             : "留给自己的一首歌，慢慢听。"},
                            {"curator", QJsonObject{{"name", "推荐人" + QString::number(i)},
                                                    {"avatar_url", "image://home/2"}}},
                            {"artwork_url", QString("image://home/%1").arg(i)}};
            song["metadata"] = facts;
            songs.append(song);
        }
        QJsonArray groups;
        for (const auto &name : {"摇滚", "电子", "欧美"})
            groups.append(QJsonObject{
                {"title", name},
                {"description", "只有音乐才是解药"},
                {"cover_url", "image://home/3"},
                {"color", "#478C96"},
                {"preview",
                 QJsonArray{
                     QJsonObject{{"id", "1"}, {"title", "Good Time"}, {"artist", "Owl City"}},
                     QJsonObject{{"id", "2"}, {"title", "听见新的故事"}, {"artist", "推荐歌手"}},
                     QJsonObject{{"id", "3"}, {"title", "Memories"}, {"artist", "Maroon 5"}}}}});
        for (int i = 3; i < 12; ++i) {
            auto group = groups[0].toObject();
            group["title"] = QString("主题 %1").arg(i + 1);
            groups.append(group);
        }
        for (int i = 0; i < groups.size(); ++i) {
            auto group = groups[i].toObject();
            group["id"] = i + 1;
            group["cover_url"] = QString("image://home/theme-%1").arg(i);
            group["start_index"] = (i % 5) * 6;
            group["song_count"] = 6;
            groups[i] = group;
        }
        for (const auto &key : {"3006", "3004", "3102", "3103"}) {
            auto contentSongs = songs;
            if (QString::fromLatin1(key) == "3006") {
                auto song = contentSongs[1].toObject();
                auto facts = song["metadata"].toObject();
                auto recommendation = facts["recommendation"].toObject();
                recommendation["detail"] = "《自困人海》---1\n执笔：小耀\n先导：……";
                recommendation["curator"] = QJsonObject{{"name", "推荐人\n第二行"}};
                facts["recommendation"] = recommendation;
                song["metadata"] = facts;
                contentSongs[1] = song;
            }
            service.recommendationCard(key)->accept(
                {{"songs", contentSongs},
                 {"groups", QString::fromLatin1(key) == "3102"   ? QJsonArray{groups[0]}
                            : QString::fromLatin1(key) == "3103" ? groups
                                                                 : QJsonArray{}}});
        }
        QQmlEngine engine;
        theme.attachToEngine(engine);
        auto *images = new ArtistImageRequests;
        engine.addImageProvider("home", images);
        QStringList warnings;
        QObject observer;
        connect(&engine, &QQmlEngine::warnings, &observer, [&](const auto &messages) {
            for (const auto &m : messages)
                warnings.append(m.toString());
        });
        QQmlComponent component(&engine);
        component.setData(R"(
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Controls
import EchoMusic.Discovery
import EchoMusic.Shell
import EchoMusic.Theme
ApplicationWindow {
 id: host
 required property DiscoveryService service
 required property RouteController router
 required property var stateStore
 property bool showPanels: true
 visible: true; width: 1440; height: 840; color: Theme.main
 Item {
  anchors.fill: parent
  EchoPageState.scope: host.stateStore
  GridLayout {
   anchors.fill: parent; anchors.margins: 40; columns: 2; rowSpacing: 20; columnSpacing: 80
   Repeater {
    model: ["3006", "3004", "3102", "3103"]
    RecommendationPanel {
     required property string modelData
     Layout.fillWidth: true; Layout.fillHeight: true
     Layout.minimumWidth: 0; Layout.minimumHeight: 0
     card: host.service.recommendationCard(modelData)
     service: host.service; router: host.router; resourceActive: host.showPanels
    }
   }
  }
 }
})",
                          QUrl("qrc:/recommendation-content-test.qml"));
        QScopedPointer<QObject> root(
            component.createWithInitialProperties({{"service", QVariant::fromValue(&service)},
                                                   {"router", QVariant::fromValue(&router)},
                                                   {"stateStore", QVariant::fromValue(&state)},
                                                   {"width", viewWidth}}));
        QVERIFY2(root, qPrintable(component.errorString()));
        auto *window = qobject_cast<QQuickWindow *>(root.data());
        QVERIFY(window);
        QVERIFY(Test::prepareNativeInput(window));
        QPointer<QQuickItem> features;
        QTRY_VERIFY(
            (features = findVisual(window->contentItem(), "recommendationFeatureCarousel-3006")));
        QCOMPARE(features->property("model").value<SongCatalogModel *>(),
                 service.recommendationCard("3006")->collection());
        QCOMPARE(features->x(), 0.);
        QCOMPARE(features->width(), features->parentItem()->width());
        QQuickItem *vipSleeve = nullptr, *vipCaption = nullptr, *vipArtwork = nullptr;
        QTRY_VERIFY((vipSleeve = findVisual(features, "recommendationVipSleeve")));
        QTRY_VERIFY((vipCaption = findVisual(features, "recommendationVipCaption")));
        QTRY_VERIFY((vipArtwork = findVisual(features, "recommendationVipArtwork")));
        QTRY_VERIFY(vipArtwork->height() > 120);
        QCOMPARE(vipArtwork->width(), vipArtwork->height());
        QVERIFY(vipCaption->mapToScene(QPointF()).y() >=
                vipSleeve->mapToScene(QPointF(0, vipSleeve->height())).y() + 7);
        auto *vipTitle = findVisual(features, "recommendationVipTitle");
        auto *vipArtist = findVisual(features, "recommendationVipArtist");
        QVERIFY(vipTitle && vipArtist);
        QVERIFY(vipTitle->mapToScene(QPointF()).y() < vipArtist->mapToScene(QPointF()).y());
        QVERIFY(vipArtist->mapToScene(QPointF(0, vipArtist->height())).y() <
                vipArtwork->mapToScene(QPointF()).y());
        QTRY_VERIFY(!findVisual(features, "recommendationVipReason")
                         ->property("text")
                         .toString()
                         .isEmpty());
        QQuickItem *secondVip = nullptr;
        QTRY_VERIFY(QMetaObject::invokeMethod(features, "itemAtIndex",
                                              Q_RETURN_ARG(QQuickItem *, secondVip),
                                              Q_ARG(int, 1)) &&
                    secondVip);
        QQuickItem *secondSleeve = nullptr, *secondArtwork = nullptr, *secondCaption = nullptr;
        QTRY_VERIFY((secondSleeve = findVisual(secondVip, "recommendationVipSleeve")));
        QTRY_VERIFY((secondArtwork = findVisual(secondVip, "recommendationVipArtwork")));
        QTRY_VERIFY((secondCaption = findVisual(secondVip, "recommendationVipCaption")));
        // A real upstream paragraph must not steal height from its artwork.
        QTRY_COMPARE(secondSleeve->size(), vipSleeve->size());
        QTRY_COMPARE(secondArtwork->size(), vipArtwork->size());
        QCOMPARE(secondCaption->height(), vipCaption->height());
        QVERIFY(std::abs(secondArtwork->mapToItem(features, QPointF()).y() -
                         vipArtwork->mapToItem(features, QPointF()).y()) < .5);
        QVERIFY(std::abs(secondCaption->mapToItem(features, QPointF()).y() -
                         vipCaption->mapToItem(features, QPointF()).y()) < .5);
        auto *secondReason = findVisual(secondVip, "recommendationVipReason");
        QVERIFY(secondReason);
        QVERIFY(!secondReason->property("text").toString().contains('\n'));
        QVERIFY(secondReason->property("text").toString().contains("执笔"));
        auto *surface = findVisual(features, "recommendationVipSurface");
        QVERIFY(surface);
        for (const auto mode : {ThemeStore::Light, ThemeStore::Dark}) {
            theme.setMode(mode);
            QCoreApplication::processEvents();
            const auto background = theme.main();
            const auto fill = surface->property("color").value<QColor>();
            const int difference = std::max({std::abs(fill.red() - background.red()),
                                             std::abs(fill.green() - background.green()),
                                             std::abs(fill.blue() - background.blue())});
            QVERIFY2(difference >= 12,
                     qPrintable(QString("VIP surface blends into page: %1").arg(difference)));
        }
        theme.setMode(ThemeStore::Light);
        auto *stories = findVisual(window->contentItem(), "recommendationFeatureCarousel-3004");
        auto *themes = findVisual(window->contentItem(), "recommendationGroupCarousel-3103");
        QTRY_VERIFY(stories && themes);
        auto *storyArtist = findVisual(stories, "recommendationFeatureArtist");
        auto *storyReason = findVisual(stories, "recommendationFeatureReason");
        QTRY_VERIFY(storyArtist && storyReason);
        QTRY_VERIFY(storyReason->mapToScene(QPointF()).y() >
                    storyArtist->mapToScene(QPointF(0, storyArtist->height())).y() + 12);
        for (auto *view : {stories, themes}) {
            QCOMPARE(view->property("compositionColumns").toInt(), viewWidth > 1000 ? 2 : 1);
            QQuickItem *second = nullptr;
            if (viewWidth > 1000) {
                QTRY_VERIFY(QMetaObject::invokeMethod(view, "itemAtIndex",
                                                      Q_RETURN_ARG(QQuickItem *, second),
                                                      Q_ARG(int, 1)) &&
                            second);
                QTRY_VERIFY(second->mapToItem(view, QPointF()).x() < view->width());
            }
        }
        const auto neighborIndex = themes->property("compositionColumns").toInt();
        auto *groupSource = service.recommendationCard("3103")->groups();
        QTRY_VERIFY(DiscoveryServiceTestAccess::itemCached(*groupSource, neighborIndex));
        QVERIFY(!DiscoveryServiceTestAccess::itemCached(*groupSource, 11));
        QTRY_VERIFY(images->requested().contains(QString("theme-%1").arg(neighborIndex)));
        QVERIFY(!images->requested().contains("theme-11"));
        QQuickItem *neighbor = nullptr;
        QTRY_VERIFY(QMetaObject::invokeMethod(themes, "itemAtIndex",
                                              Q_RETURN_ARG(QQuickItem *, neighbor),
                                              Q_ARG(int, neighborIndex)) &&
                    neighbor);
        auto *groupArtwork = findVisual(neighbor, "recommendationGroupArtwork");
        auto *groupInformation = findVisual(neighbor, "recommendationGroupInformation");
        QVERIFY(groupArtwork && groupInformation);
        QTRY_COMPARE(groupArtwork->property("status").toInt(), 1);
        QTRY_VERIFY(std::abs(groupArtwork->height() - groupInformation->height()) < 1);
        QCOMPARE(groupArtwork->y(), groupInformation->y());
        QVERIFY(countVisual(window->contentItem(), "recommendationGroupCard") <= 8);
        auto *groupPanel = findVisual(window->contentItem(), "recommendationPanel-3103");
        QVERIFY(groupPanel);
        QVERIFY(!findVisual(groupPanel, "recommendationViewAll"));
        QVERIFY(!findVisual(groupPanel, "recommendationPlayAll"));
        auto *singlePanel = findVisual(window->contentItem(), "recommendationPanel-3102");
        QVERIFY(singlePanel);
        QQuickItem *singleMore = nullptr;
        QTRY_VERIFY((singleMore = findVisual(singlePanel, "recommendationViewAll")) &&
                    singleMore->isEnabled());
        auto *singleLocalMore = findVisual(singlePanel, "recommendationGroupMore-3102-0");
        QVERIFY(singleLocalMore && !singleLocalMore->isVisible());
        auto *singleHeading = findVisual(singlePanel, "recommendationHeading-3102");
        QVERIFY(singleHeading);
        const auto singleMoreBounds = singleMore->mapRectToScene(singleMore->boundingRect());
        const auto singleTitleBounds = singleHeading->mapRectToScene(singleHeading->boundingRect());
        QVERIFY(singleMoreBounds.top() < singleTitleBounds.bottom());
        QVERIFY(singleMoreBounds.bottom() > singleTitleBounds.top());
        QTest::mouseClick(
            window, Qt::LeftButton, Qt::NoModifier,
            singleMore->mapToScene(QPointF(singleMore->width() / 2, singleMore->height() / 2))
                .toPoint());
        QTRY_COMPARE(router.current()->resourceId(), QString("3102"));
        QCOMPARE(router.current()->query().value("group"), QString("0"));
        auto *groupMore = findVisual(groupPanel, "recommendationGroupMore-3103-0");
        auto *groupPlay = findVisual(groupPanel, "recommendationGroupPlay-3103-0");
        QVERIFY(groupMore && groupPlay);
        QTest::mouseMove(window, QPoint(4, 4));
        QTRY_VERIFY(!groupPlay->isVisible());
        QTest::mouseMove(window, groupPlay->mapToScene(QPointF(20, 20)).toPoint());
        QTRY_VERIFY(groupPlay->isVisible());
        auto *firstGroupArtwork = findVisual(groupPanel, "recommendationGroupArtwork");
        QVERIFY(firstGroupArtwork);
        const auto playCenter = groupPlay->mapToScene(QPointF(20, 20));
        const auto coverCenter = firstGroupArtwork->mapToScene(
            QPointF(firstGroupArtwork->width() / 2, firstGroupArtwork->height() / 2));
        QVERIFY(std::hypot(playCenter.x() - coverCenter.x(), playCenter.y() - coverCenter.y()) <
                .5);
        QTest::mouseClick(
            window, Qt::LeftButton, Qt::NoModifier,
            groupMore->mapToScene(QPointF(groupMore->width() / 2, groupMore->height() / 2))
                .toPoint());
        QVERIFY(router.current());
        QCOMPARE(router.current()->resourceId(), QString("3103"));
        QCOMPARE(router.current()->query().value("group").toInt(), 0);
        theme.setMotionEnabled(true);
        auto *groupNext = findVisual(window->contentItem(), "recommendationCarouselNext-3103");
        QVERIFY(groupNext);
        QTest::mouseMove(
            window, groupNext->mapToScene(QPointF(groupNext->width() / 2, groupNext->height() / 2))
                        .toPoint());
        QTRY_VERIFY(groupNext->isVisible());
        Test::FrameSpy firstGroupFrame(window);
        QElapsedTimer firstGroupSlide;
        firstGroupSlide.start();
        QTest::mouseClick(
            window, Qt::LeftButton, Qt::NoModifier,
            groupNext->mapToScene(QPointF(groupNext->width() / 2, groupNext->height() / 2))
                .toPoint());
        QVERIFY(firstGroupFrame.wait(1000));
        qInfo() << "first prepared group slide to observed frame us"
                << firstGroupSlide.nsecsElapsed() / 1000 << "group hot entries"
                << DiscoveryServiceTestAccess::itemStatistics(*groupSource).entries;
        QTRY_COMPARE(themes->property("currentIndex").toInt(), 1);
        auto *groupSlide =
            findVisualOwnerObject(window->contentItem(), "recommendationCarouselSlide-3103");
        QVERIFY(groupSlide);
        QTRY_VERIFY(!groupSlide->property("running").toBool());
        theme.setMotionEnabled(false);
        QVERIFY(QMetaObject::invokeMethod(themes, "step", Q_ARG(int, -1)));
        QCOMPARE(themes->property("currentIndex").toInt(), 0);
        QVERIFY(!DiscoveryServiceTestAccess::itemCached(*groupSource, 11));
        auto *favorite = findVisual(window->contentItem(), "recommendationSongFavorite");
        QVERIFY(favorite);
        QCOMPARE(favorite->size(), QSizeF(32, 32));
        QCOMPARE(favorite->property("padding").toReal(), 5.);
        QTRY_VERIFY(findVisual(window->contentItem(), "recommendationFeatureReason") &&
                    !findVisual(window->contentItem(), "recommendationFeatureReason")
                         ->property("text")
                         .toString()
                         .isEmpty());
        QTRY_VERIFY(findVisual(window->contentItem(), "recommendationGroupCarousel-3103"));
        QTRY_VERIFY(findVisual(window->contentItem(), "recommendationGroupDescription") &&
                    !findVisual(window->contentItem(), "recommendationGroupDescription")
                         ->property("text")
                         .toString()
                         .isEmpty());
        QTRY_VERIFY(findVisual(window->contentItem(), "recommendationGroupSong") &&
                    !findVisual(window->contentItem(), "recommendationGroupSong")
                         ->property("text")
                         .toString()
                         .isEmpty());
        QVERIFY(countVisual(window->contentItem(), "homeSongCard") <= 16);
        auto *next = findVisual(window->contentItem(), "recommendationCarouselNext-3006");
        QVERIFY(next);
        auto *previous = findVisual(window->contentItem(), "recommendationCarouselPrevious-3006");
        QVERIFY(previous);
        QTest::mouseMove(window, QPoint(4, 4));
        QTRY_VERIFY(!next->isVisible() && !previous->isVisible());
        auto *play = findVisual(features, "recommendationFeaturePlay");
        QVERIFY(play);
        QTRY_VERIFY(!play->isVisible());
        auto *vipItem = findVisual(features, "recommendationVipSong");
        QVERIFY(vipItem);
        vipItem->forceActiveFocus(Qt::TabFocusReason);
        QTRY_VERIFY(play->isVisible());
        vipItem->setFocus(false);
        features->forceActiveFocus(Qt::TabFocusReason);
        QTRY_VERIFY(!play->isVisible());
        QTest::mouseMove(
            window, play->mapToScene(QPointF(play->width() / 2, play->height() / 2)).toPoint());
        QTRY_VERIFY(play->isVisible());
        QTest::mouseMove(window, QPoint(4, 4));
        QTRY_VERIFY(!play->isVisible());
        QTest::mouseMove(
            window,
            features->mapToScene(QPointF(features->width() / 2, features->height() / 2)).toPoint());
        QTRY_VERIFY(next->isVisible());
        QVERIFY(!previous->isVisible());
        QCOMPARE(next->size(), QSizeF(40, 48));
        QCOMPARE(next->property("padding").toReal(), 6.);
        QCOMPARE(next->property("topPadding").toReal(), 10.);
        QCOMPARE(next->parentItem(), features->parentItem());
        QCOMPARE(next->mapToItem(features, QPointF()).x(), features->width() + 8);
        // Native centered anchors round an odd-height viewport to a logical
        // pixel; retain that snapping rather than shifting the product arrow.
        QVERIFY(std::abs(next->mapToItem(features, QPointF(0, next->height() / 2)).y() -
                         features->height() / 2) <= .5);
        QVERIFY(!next->property("background").value<QQuickItem *>());
        QTest::mouseMove(
            window, next->mapToScene(QPointF(next->width() / 2, next->height() / 2)).toPoint());
        QTRY_VERIFY(next->isVisible() && next->property("hovered").toBool());
        QTest::mouseClick(
            window, Qt::LeftButton, Qt::NoModifier,
            next->mapToScene(QPointF(next->width() / 2, next->height() / 2)).toPoint());
        QTRY_COMPARE(features->property("currentIndex").toInt(), 1);
        QTRY_VERIFY(previous->isVisible());
        QCOMPARE(previous->mapToItem(features, QPointF(previous->width(), 0)).x(), -8.);
        QVERIFY(!previous->property("background").value<QQuickItem *>());
        QTest::mouseMove(window, QPoint(4, 4));
        QTRY_VERIFY(!next->isVisible() && !previous->isVisible());
        QVERIFY(countVisual(window->contentItem(), "homeSongCard") <= 16);
        QCOMPARE(state.load("recommendation-preview-3006")["index"].toInt(), 1);
        features->forceActiveFocus(Qt::TabFocusReason);
        QTest::keyClick(window, Qt::Key_Right);
        QTRY_VERIFY(next->isVisible() && previous->isVisible());
        QTRY_COMPARE(features->property("currentIndex").toInt(), 2);
        QTest::keyClick(window, Qt::Key_Left);
        QTRY_COMPARE(features->property("currentIndex").toInt(), 1);
        QTRY_VERIFY(features->property("contentX").toReal() > 0);
        const auto previewPosition = features->property("contentX").toReal();
        root->setProperty("showPanels", false);
        QTRY_VERIFY(features.isNull());
        root->setProperty("showPanels", true);
        QTRY_VERIFY(
            (features = findVisual(window->contentItem(), "recommendationFeatureCarousel-3006")));
        QTRY_COMPARE(features->property("currentIndex").toInt(), 1);
        QTRY_COMPARE(features->property("contentX").toReal(), previewPosition);
        theme.setMotionEnabled(true);
        QPointer<QObject> slide =
            findVisualOwnerObject(window->contentItem(), "recommendationCarouselSlide-3006");
        QVERIFY(slide);
        const auto before = window->grabWindow();
        const auto start = features->property("contentX").toReal();
        Test::FrameSpy movingFrames(window);
        QVERIFY(QMetaObject::invokeMethod(features, "step", Q_ARG(int, 1)));
        QTRY_VERIFY(slide->property("running").toBool());
        QTRY_VERIFY(features->property("contentX").toReal() > start &&
                    features->property("contentX").toReal() < slide->property("to").toReal());
        QTRY_VERIFY(!movingFrames.isEmpty());
        const auto middle = window->grabWindow();
        QVERIFY(middle != before);
        QVERIFY(countVisual(window->contentItem(), "homeSongCard") <= 16);
        QTRY_VERIFY(!slide->property("running").toBool());
        QCOMPARE(features->property("contentX").toReal(), slide->property("to").toReal());
        Test::FrameSpy finalMotionFrame(window);
        window->update();
        QTRY_VERIFY(!finalMotionFrame.isEmpty());
        QVERIFY(middle != window->grabWindow());
        QVERIFY(QMetaObject::invokeMethod(features, "step", Q_ARG(int, 1)));
        QTRY_VERIFY(slide->property("running").toBool());
        theme.setMotionEnabled(false);
        QVERIFY(!slide->property("running").toBool());
        QCOMPARE(features->property("contentX").toReal(), slide->property("to").toReal());
        theme.setMotionEnabled(true);
        const auto reverseTarget = features->property("contentX").toReal();
        QVERIFY(QMetaObject::invokeMethod(features, "step", Q_ARG(int, 1)));
        QTRY_VERIFY(features->property("contentX").toReal() > reverseTarget);
        const auto reverseStart = features->property("contentX").toReal();
        QVERIFY(QMetaObject::invokeMethod(features, "step", Q_ARG(int, -1)));
        QCOMPARE(features->property("contentX").toReal(), reverseStart);
        QTRY_VERIFY(!slide->property("running").toBool());
        QCOMPARE(features->property("contentX").toReal(), reverseTarget);
        QVERIFY(QMetaObject::invokeMethod(features, "step", Q_ARG(int, 1)));
        QTRY_VERIFY(slide->property("running").toBool());
        window->hide();
        QVERIFY(!slide->property("running").toBool());
        QCOMPARE(features->property("contentX").toReal(), slide->property("to").toReal());
        window->show();
        QVERIFY(Test::prepareNativeInput(window));
        QVERIFY(QMetaObject::invokeMethod(features, "step", Q_ARG(int, 1)));
        QTRY_VERIFY(slide->property("running").toBool());
        const auto resumeIndex = features->property("currentIndex").toInt();
        const auto resumePosition = slide->property("to").toReal();
        const auto resumeOrigin = features->property("originX").toReal();
        root->setProperty("showPanels", false);
        QTRY_VERIFY(features.isNull() && slide.isNull());
        root->setProperty("showPanels", true);
        QTRY_VERIFY(
            (features = findVisual(window->contentItem(), "recommendationFeatureCarousel-3006")));
        QTRY_COMPARE(features->property("currentIndex").toInt(), resumeIndex);
        QTRY_COMPARE(features->property("contentX").toReal(), resumePosition);
        QQuickItem *resumedItem = nullptr;
        QTRY_VERIFY(QMetaObject::invokeMethod(features, "itemAtIndex",
                                              Q_RETURN_ARG(QQuickItem *, resumedItem),
                                              Q_ARG(int, resumeIndex)) &&
                    resumedItem);
        QTRY_VERIFY(qAbs(resumedItem->mapToItem(features, QPointF()).x()) < .5);
        qInfo() << "restored-carousel origin/position before" << resumeOrigin << resumePosition
                << "after" << features->property("originX").toReal()
                << features->property("contentX").toReal() << "visibleOffset"
                << resumedItem->mapToItem(features, QPointF()).x();
        const auto artifacts = qEnvironmentVariable("ECHO_CONTENT_UI_ARTIFACTS");
        if (!artifacts.isEmpty()) {
            QDir().mkpath(artifacts);
            middle.save(artifacts + "/motion-" + QTest::currentDataTag() + ".png");
            for (auto mode : {ThemeStore::Light, ThemeStore::Dark}) {
                Test::FrameSpy frames(window);
                theme.setMode(mode);
                window->update();
                QTRY_VERIFY(!frames.isEmpty());
                window->grabWindow().save(artifacts + "/content-" + QTest::currentDataTag() +
                                          (mode == ThemeStore::Light ? "-light.png" : "-dark.png"));
                frames.clear();
                QTest::mouseMove(
                    window,
                    features->mapToScene(QPointF(features->width() / 2, features->height() / 2))
                        .toPoint());
                window->update();
                QTRY_VERIFY(!frames.isEmpty());
                window->grabWindow().save(artifacts + "/hover-" + QTest::currentDataTag() +
                                          (mode == ThemeStore::Light ? "-light.png" : "-dark.png"));
                QTest::mouseMove(window, QPoint(4, 4));
            }
        }
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
        root.reset();
        service.shutdown();
        QSignalSpy drain(&playback, &PlaybackService::closed);
        playback.shutdown();
        QTRY_COMPARE_WITH_TIMEOUT(drain.size(), 1, 15000);
        api.shutdown();
    }
    void recommendationSourcesFollowCardSemantics()
    {
        QTemporaryDir dir;
        SettingsStore prefs(dir.filePath("prefs.ini"));
        ApiClient api(DeviceIdentity::temporary());
        AccountService account(api);
        PlaybackService playback(api, prefs, nullptr, dir.filePath("playback.sqlite"));
        PlaybackServiceTestAccess::currentTrack(playback,
                                                PlaybackTrack::fromDomain(domainSong(999)));
        DiscoveryService service(api, playback, account);
        service.setPage("home", "sources", true);
        for (const auto &key : {"3014", "3001"}) {
            service.activateCard(key, true);
            const auto id = DiscoveryServiceTestAccess::cardRequest(service, key);
            QVERIFY(id);
            QCOMPARE(ApiClientTestAccess::query(api, id)["source_mixsong"].toString(), QString());
            api.cancel(id);
            service.activateCard(key, false);
            DiscoveryServiceTestAccess::completeCard(service, id, {{"songs", QJsonArray{}}});
        }
        PlaybackServiceTestAccess::currentTrack(playback, {});
        service.refreshCard("3007");
        QCOMPARE(DiscoveryServiceTestAccess::cardRequest(service, "3007"), quint64(0));
        QVERIFY(!service.recommendationCard("3007"));
        QVERIFY(!service.recommendationCard("3032"));
        service.shutdown();
        QSignalSpy drain(&playback, &PlaybackService::closed);
        playback.shutdown();
        QTRY_COMPARE_WITH_TIMEOUT(drain.size(), 1, 15000);
        api.shutdown();
    }
    void recommendationInformationUsesResponseFacts()
    {
        QTemporaryDir dir;
        SettingsStore prefs(dir.filePath("prefs.ini"));
        ApiClient api(DeviceIdentity::temporary());
        AccountService account(api);
        PlaybackService playback(api, prefs, nullptr, dir.filePath("playback.sqlite"));
        DiscoveryService service(api, playback, account);
        auto *related = service.recommendationCard("3014");
        related->accept({{"title", "喜欢这首歌的TA也喜欢"}, {"description", "晴天 - 周杰伦"}});
        QCOMPARE(related->title(), QString("喜欢这首歌的TA也喜欢"));
        QCOMPARE(related->subtitle(), QString("晴天 - 周杰伦"));
        related->clear();
        QCOMPARE(related->title(), QString("喜欢这首歌的 TA 也喜欢"));
        auto *tagCard = service.recommendationCards()->find("3102");
        tagCard->accept({{"title", "喜欢「粤语」的TA也喜欢"}, {"description", "接口标签说明"}});
        QCOMPARE(tagCard->title(), QString("喜欢「粤语」的TA也喜欢"));
        QCOMPARE(tagCard->subtitle(), QString("接口标签说明"));
        tagCard->clear();
        QCOMPARE(tagCard->title(), QString("喜欢「…」的 TA 也喜欢"));
        auto song = domainSong(1);
        song["artists"] = QJsonArray{QJsonObject{{"name", "Alpha"}}, QJsonObject{{"name", "Alpha"}},
                                     QJsonObject{{"name", "Beta"}}};
        auto next = domainSong(2);
        next["artists"] = QJsonArray{QJsonObject{{"name", "Gamma"}}};
        for (const auto &key : {"3001", "3004", "3005", "3006", "3107"}) {
            auto *card = service.recommendationCards()->find(key);
            card->accept({{"songs", QJsonArray{song, next}}});
            QCOMPARE(card->subtitle(), QString("歌手：Alpha / Beta / Gamma"));
            card->accept({{"songs", QJsonArray{song}}, {"description", "提供方推荐说明"}});
            QCOMPARE(card->subtitle(), QString("提供方推荐说明"));
            card->clear();
            QVERIFY(card->subtitle().isEmpty());
        }
        auto metadata = next["metadata"].toObject();
        metadata["recommendation"] = QJsonObject{{"headline", "新歌推荐理由"}};
        next["metadata"] = metadata;
        auto *newSongs = service.recommendationCards()->find("new");
        newSongs->accept({{"songs", QJsonArray{next}}});
        QCOMPARE(newSongs->subtitle(), QString("新歌推荐理由"));
        QJsonArray prefix;
        for (int i = 0; i < 6; ++i)
            prefix.append(QJsonObject{});
        prefix.append(song);
        auto *privateSongs = service.recommendationCards()->find("3001");
        privateSongs->accept({{"songs", prefix}});
        QVERIFY(privateSongs->subtitle().isEmpty());
        service.shutdown();
        QSignalSpy drain(&playback, &PlaybackService::closed);
        playback.shutdown();
        QTRY_COMPARE_WITH_TIMEOUT(drain.size(), 1, 15000);
        api.shutdown();
    }
    void recommendationDetailsFollowEachMoreButton()
    {
        if (QGuiApplication::platformName() != "offscreen")
            QSKIP("Nonintrusive offscreen route regression");
        QTemporaryDir dir;
        SettingsStore prefs(dir.filePath("prefs.ini"));
        ApiClient api(DeviceIdentity::temporary());
        AccountService account(api);
        PlaybackService playback(api, prefs, nullptr, dir.filePath("playback.sqlite"));
        ThemeStore theme(prefs);
        theme.setMotionEnabled(false);
        ShellSettings settings(prefs);
        settings.acceptUserAgreement();
        RouteController router;
        DiscoveryService service(api, playback, account);
        service.setPreferences(&prefs);
        DiscoveryRouteCoordinator routes(service, router, false);
        QQmlEngine engine;
        theme.attachToEngine(engine);
        QStringList warnings;
        QObject observer;
        connect(&engine, &QQmlEngine::warnings, &observer, [&](const auto &messages) {
            for (const auto &message : messages)
                warnings.append(message.toString());
        });
        QQmlComponent component(&engine);
        component.setData(R"(import QtQuick
import QtQuick.Controls.Basic
import EchoMusic.Shell
ApplicationWindow { id:host; required property RouteController router; required property ShellSettings settings; visible:true; width:1400; height:800
 RouteHost { anchors.fill:parent; router:host.router; settings:host.settings }
})",
                          QUrl("qrc:/recommendation-detail-identity.qml"));
        QScopedPointer<QObject> root(
            component.createWithInitialProperties({{"router", QVariant::fromValue(&router)},
                                                   {"settings", QVariant::fromValue(&settings)}}));
        QVERIFY2(root, qPrintable(component.errorString()));
        auto *window = qobject_cast<QQuickWindow *>(root.data());
        QVERIFY(window);
        QVERIFY(router.navigate("home"));
        DiscoveryServiceTestAccess::complete(
            service, DiscoveryService::Daily,
            QJsonObject{{"songs", QJsonArray{domainSong(1, "daily song")}}});
        DiscoveryServiceTestAccess::complete(
            service, DiscoveryService::Style,
            QJsonObject{{"songs", QJsonArray{domainSong(2, "style song")}}});
        service.recommendationCards()->find("3001")->accept(
            {{"songs", QJsonArray{domainSong(3, "3001 song")}}});
        service.recommendationCards()->find("new")->accept(
            {{"songs", QJsonArray{domainSong(4, "new song")}}});
        QSet<QString> identities;
        for (const auto &key :
             {QString("daily"), QString("style"), QString("3001"), QString("new")}) {
            QQuickItem *panel = nullptr;
            QTRY_VERIFY((panel = findVisual(window->contentItem(), "recommendationPanel-" + key)));
            auto *more = findVisual(panel, "recommendationViewAll");
            QVERIFY(more);
            QTest::mouseClick(
                window, Qt::LeftButton, Qt::NoModifier,
                more->mapToScene(QPointF(more->width() / 2, more->height() / 2)).toPoint());
            QTRY_COMPARE(router.current()->resourceId(), key);
            identities.insert(router.current()->key());
            QQuickItem *detail = nullptr;
            QTRY_VERIFY((detail = findVisual(window->contentItem(), "freeListenPage")));
            auto *expected = service.recommendationCards()->find(key);
            QCOMPARE(service.actions(expected->collection()), expected->actions());
            QTRY_COMPARE(detail->property("card").value<RecommendationCard *>(), expected);
            QCOMPARE(detail->property("collection").value<SongCatalogModel *>(),
                     expected->collection());
            QCOMPARE(expected->collection()
                         ->data(expected->collection()->index(0), SongCatalogModel::Title)
                         .toString(),
                     key + " song");
            router.back();
            QTRY_VERIFY(findVisual(window->contentItem(), "homePage"));
        }
        QCOMPARE(identities.size(), 4);
        // A card that has never entered the viewport must request its own data.
        QVERIFY(router.navigate("recommendation-detail", {}, "3004"));
        service.setPage("recommendation-detail", router.current()->instanceId(), true);
        auto *cold = service.recommendationCards()->find("3004");
        QCOMPARE(cold->status(), int(DiscoveryService::Loading));
        const auto request = DiscoveryServiceTestAccess::cardRequest(service, "3004");
        QVERIFY(request);
        api.cancel(request);
        DiscoveryServiceTestAccess::completeCard(
            service, request, {{"songs", QJsonArray{domainSong(5, "fresh card result")}}});
        QQuickItem *detail = nullptr;
        QTRY_VERIFY((detail = findVisual(window->contentItem(), "freeListenPage")));
        QTRY_COMPARE(detail->property("card").value<RecommendationCard *>(), cold);
        QCOMPARE(cold->status(), int(DiscoveryService::Ready));
        QCOMPARE(cold->collection()
                     ->data(cold->collection()->index(0), SongCatalogModel::Title)
                     .toString(),
                 "fresh card result");
        // A cold group route waits for its canonical parent, never showing the
        // unrelated free-listening source while its group metadata arrives.
        QVERIFY(router.navigate("recommendation-detail", {{"group", QString("0")}}, "3103"));
        QTRY_VERIFY((detail = findVisual(window->contentItem(), "freeListenPage")));
        QPointer<RecommendationCard> firstGroup;
        QTRY_VERIFY((firstGroup = detail->property("card").value<RecommendationCard *>()));
        QVERIFY(firstGroup != service.recommendationCard("3103"));
        QJsonArray groupSongs, groupRows;
        for (int i = 0; i < 18; ++i)
            groupSongs.append(domainSong(i + 100, "group song " + QString::number(i)));
        for (int i = 0; i < 3; ++i)
            groupRows.append(QJsonObject{{"title", "曲风 " + QString::number(i)},
                                         {"description", "分组介绍"},
                                         {"start_index", i * 6},
                                         {"song_count", 6}});
        service.recommendationCard("3103")->accept({{"songs", groupSongs}, {"groups", groupRows}});
        QSet<QString> groupRoutes;
        for (int i = 0; i < 3; ++i) {
            QVERIFY(
                router.navigate("recommendation-detail", {{"group", QString::number(i)}}, "3103"));
            groupRoutes.insert(router.current()->key());
            QTRY_VERIFY((detail = findVisual(window->contentItem(), "freeListenPage")));
            RecommendationCard *scoped = nullptr;
            QTRY_VERIFY((scoped = detail->property("card").value<RecommendationCard *>()));
            QTRY_COMPARE(scoped->status(), int(DiscoveryService::Ready));
            QCOMPARE(scoped->title(), "曲风 " + QString::number(i));
            const auto tracks = readCatalogTracks(scoped->collection());
            QCOMPARE(tracks.size(), 6);
            QCOMPARE(tracks.first().id, QString::number(100 + i * 6));
            QCOMPARE(tracks.last().id, QString::number(105 + i * 6));
            if (!i)
                QCOMPARE(scoped, firstGroup.data());
            QVERIFY(SongCatalogStorageTestAccess::metadataActive(*scoped->collection()));
            router.setPresentationActive(false);
            QVERIFY(!SongCatalogStorageTestAccess::metadataActive(*scoped->collection()));
            router.setPresentationActive(true);
            QVERIFY(SongCatalogStorageTestAccess::metadataActive(*scoped->collection()));
        }
        QCOMPARE(groupRoutes.size(), 3);
        QVERIFY(router.navigate("recommendation-detail", {{"group", QString("99")}}, "3103"));
        QTRY_VERIFY((detail = findVisual(window->contentItem(), "freeListenPage")));
        QTRY_COMPARE(detail->property("catalogStatus").toInt(), int(DiscoveryService::Error));
        QCOMPARE(detail->property("collection").value<SongCatalogModel *>()->totalCount(), 0);
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
        root.reset();
        service.shutdown();
        QSignalSpy drained(&playback, &PlaybackService::closed);
        playback.shutdown();
        QTRY_COMPARE(drained.size(), 1);
        api.shutdown();
    }

    // The outer page is a natural-height virtual ListView, whose extent for
    // unmeasured delegates is estimated. A loading section must already reserve
    // the extent its response will occupy, otherwise cards below shift the
    // whole page and leave blank space when a fast scroll reaches the bottom.
    void recommendationSectionsReserveLoadedExtentWhileLoading_data()
    {
        QTest::addColumn<QString>("key");
        for (const auto &key : {"daily", "style", "3001", "3004", "3005", "3006", "3014", "3101",
                                "3102", "3103", "new", "3107"})
            QTest::newRow(key) << QString(key);
    }
    void recommendationSectionsReserveLoadedExtentWhileLoading()
    {
        QFETCH(QString, key);
        if (QGuiApplication::platformName() != "offscreen")
            QSKIP("Nonintrusive offscreen route regression");
        QTemporaryDir dir;
        SettingsStore prefs(dir.filePath("prefs.ini"));
        ApiClient api(DeviceIdentity::temporary());
        AccountService account(api);
        PlaybackService playback(api, prefs, nullptr, dir.filePath("playback.sqlite"));
        SessionDatabase cache(dir.filePath("session"));
        SongMetadataService metadata(api);
        metadata.enableSqlIndex(cache);
        playback.attachMetadata(metadata);
        ThemeStore theme(prefs);
        theme.setMotionEnabled(false);
        theme.setFloatingFrosted(false);
        ShellSettings settings(prefs);
        settings.acceptUserAgreement();
        RouteController router;
        DiscoveryService service(api, playback, account);
        prefs.setValue("discovery/cards", QStringList{key});
        prefs.setValue("discovery/cardsDefaultsVersion", 1);
        service.setPreferences(&prefs);
        DiscoveryRouteCoordinator routes(service, router, false);
        QQmlEngine engine;
        theme.attachToEngine(engine);
        QStringList warnings;
        QObject observer;
        connect(&engine, &QQmlEngine::warnings, &observer, [&](const auto &messages) {
            for (const auto &message : messages)
                warnings.append(message.toString());
        });
        QQmlComponent component(&engine);
        component.setData(R"(
            import QtQuick
            import QtQuick.Controls.Basic
            import EchoMusic.Shell
            ApplicationWindow {
                id: host
                required property RouteController router
                required property ShellSettings settings
                visible: true; width: 1200; height: 900
                RouteHost { anchors.fill: parent; router: host.router; settings: host.settings }
            })",
                          QUrl("qrc:/recommendation-loading-extent.qml"));
        QScopedPointer<QObject> root(
            component.createWithInitialProperties({{"router", QVariant::fromValue(&router)},
                                                   {"settings", QVariant::fromValue(&settings)}}));
        QVERIFY2(root, qPrintable(component.errorString()));
        auto *window = qobject_cast<QQuickWindow *>(root.data());
        QVERIFY(window);
        QVERIFY(router.navigate("home"));
        QQuickItem *panel = nullptr;
        QTRY_VERIFY((panel = findVisual(window->contentItem(), "recommendationPanel-" + key)));
        auto *card = service.recommendationCards()->find(key);
        QVERIFY(card);
        QVERIFY(card->status() != int(DiscoveryService::Ready));
        auto *surface = findVisual(panel, "recommendationSectionSurface");
        QVERIFY(surface);
        QCOMPARE(surface->width(), panel->width());
        QCOMPARE(surface->height(), panel->height());
        if (key != "3102" && key != "3103") {
            auto *more = findVisual(panel, "recommendationViewAll");
            auto *play = findVisual(panel, "recommendationPlayAll");
            auto *heading = findVisual(panel, "recommendationHeading-" + key);
            QVERIFY(more && play && heading);
            QVERIFY(more->property("plain").toBool());
            QCOMPARE(more->property("text").toString(), QString("更多"));
            QCOMPARE(more->property("glyph").toString(), QString("chevron-right"));
            QVERIFY(more->property("glyphAfterText").toBool());
            QQuickItem *moreGlyph = nullptr, *moreLabel = nullptr;
            QTRY_VERIFY((moreGlyph = findVisual(more, "buttonGlyph")) &&
                        (moreLabel = findVisual(more, "buttonLabel")));
            QVERIFY(moreGlyph->mapRectToScene(moreGlyph->boundingRect()).left() >=
                    moreLabel->mapRectToScene(moreLabel->boundingRect()).right());
            QCOMPARE(play->width(), play->height());
            const auto playRect = play->mapRectToScene(play->boundingRect());
            const auto headingRect = heading->mapRectToScene(heading->boundingRect());
            const auto moreRect = more->mapRectToScene(more->boundingRect());
            QVERIFY(playRect.left() >= headingRect.left());
            QVERIFY(playRect.right() <= headingRect.right() + 1);
            QVERIFY(playRect.right() < moreRect.left());
            QVERIFY(qAbs(more->mapToItem(panel, QPointF(more->width(), 0)).x() -
                         (panel->width() - 16)) < 1);
        }
        const auto loadingHeight = panel->height();
        QVERIFY(loadingHeight >= 252);
        QJsonArray songs;
        for (int i = 1; i <= 6; ++i)
            songs.append(domainSong(i));
        card->accept({{"songs", songs}});
        QTRY_COMPARE(card->status(), int(DiscoveryService::Ready));
        QTRY_COMPARE(panel->height(), loadingHeight);
        card->setState(DiscoveryService::Loading);
        QTRY_COMPARE(panel->height(), loadingHeight);
        card->setState(DiscoveryService::Error, "refresh failed");
        QTRY_COMPARE(panel->height(), loadingHeight);
        // A theme group is real content even when its song source is empty.
        if (key == "3102" || key == "3103") {
            card->accept({{"groups",
                           QJsonArray{QJsonObject{{"title", "主题"}, {"preview", QJsonArray{}}}}}});
            QTRY_COMPARE(panel->height(), loadingHeight);
        }

        card->accept({{"songs", QJsonArray{domainSong(1)}}});
        QTest::qWait(20);
        const auto retainedHeight = panel->height();
        if (key != "3004" && key != "3006" && key != "3102" && key != "3103")
            QVERIFY(retainedHeight <
                    loadingHeight); // A ready one-song section remains naturally short.
        card->setState(DiscoveryService::Loading);
        QTRY_COMPARE(panel->height(), retainedHeight);
        card->setState(DiscoveryService::Error, "refresh failed");
        QTRY_COMPARE(panel->height(), retainedHeight);
        card->accept({});
        QTRY_VERIFY(panel->height() < loadingHeight);
        card->setState(DiscoveryService::Loading);
        QTRY_COMPARE(panel->height(), loadingHeight);
        if (key == "daily" || key == "style" || key == "3014") {
            auto *heading = findVisual(panel, "recommendationHeading-" + key);
            auto *detail = findVisual(panel, key == "style" ? QString("styleRecommendationFilters")
                                                            : "recommendationInformation-" + key);
            QVERIFY(heading && detail);
            QVERIFY(panel->property("inlineInformation").toBool());
            QVERIFY(detail->mapToScene(QPointF()).x() >=
                    heading->mapToScene(QPointF(heading->width(), 0)).x());
            window->resize(240, 900);
            QTRY_VERIFY(!panel->property("inlineInformation").toBool());
            QTRY_VERIFY(
                (detail = findVisual(panel, key == "style" ? QString("styleRecommendationFilters")
                                                           : "recommendationInformation-" + key)));
            QTRY_VERIFY(detail->mapToScene(QPointF()).y() >=
                        heading->mapToScene(QPointF(0, heading->height())).y());
            QVERIFY(detail->mapToItem(panel, QPointF(detail->width(), 0)).x() <=
                    panel->width() + 1);
        }
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
        root.reset();
        service.shutdown();
        QSignalSpy drained(&playback, &PlaybackService::closed);
        playback.shutdown();
        QTRY_COMPARE(drained.size(), 1);
        api.shutdown();
    }

    void homeFastBottomKeepsViewportCovered_data()
    {
        QTest::addColumn<int>("testWidth");
        QTest::addColumn<bool>("featureFirst");
        QTest::newRow("desktop-mixed") << 1200 << false;
        QTest::newRow("narrow-mixed") << 380 << false;
        QTest::newRow("desktop-feature-first") << 1200 << true;
        QTest::newRow("narrow-feature-first") << 380 << true;
    }
    void homeFastBottomKeepsViewportCovered()
    {
        QFETCH(int, testWidth);
        QFETCH(bool, featureFirst);
        if (QGuiApplication::platformName() != "offscreen")
            QSKIP("Nonintrusive offscreen route regression");
        QTemporaryDir dir;
        SettingsStore prefs(dir.filePath("prefs.ini"));
        ApiClient api(DeviceIdentity::temporary());
        AccountService account(api);
        PlaybackService playback(api, prefs, nullptr, dir.filePath("playback.sqlite"));
        SessionDatabase cache(dir.filePath("session"));
        SongMetadataService metadata(api);
        metadata.enableSqlIndex(cache);
        playback.attachMetadata(metadata);
        ThemeStore theme(prefs);
        theme.setMotionEnabled(false);
        theme.setFloatingFrosted(false);
        ShellSettings settings(prefs);
        settings.acceptUserAgreement();
        RouteController router;
        DiscoveryService service(api, playback, account);
        QStringList keys{"daily", "style", "3001", "3004", "3005", "3006",
                         "3014",  "3101",  "3102", "3103", "new",  "3107"};
        if (featureFirst) {
            keys.move(keys.indexOf("3006"), 0);
            keys.move(keys.indexOf("3103"), 1);
        }
        prefs.setValue("discovery/cards", keys);
        prefs.setValue("discovery/cardsDefaultsVersion", 1);
        service.setPreferences(&prefs);
        DiscoveryRouteCoordinator routes(service, router, false);
        QQmlEngine engine;
        theme.attachToEngine(engine);
        QStringList warnings;
        QObject observer;
        connect(&engine, &QQmlEngine::warnings, &observer, [&](const auto &messages) {
            for (const auto &message : messages)
                warnings.append(message.toString());
        });
        QQmlComponent component(&engine);
        component.setData(R"(
            import QtQuick
            import QtQuick.Controls.Basic
            import EchoMusic.Shell
            ApplicationWindow {
                id: host
                required property RouteController router
                required property ShellSettings settings
                required property int testWidth
                visible: true; width: testWidth; height: 900
                RouteHost { anchors.fill: parent; router: host.router; settings: host.settings }
            })",
                          QUrl("qrc:/recommendation-loading-extent.qml"));
        QScopedPointer<QObject> root(
            component.createWithInitialProperties({{"router", QVariant::fromValue(&router)},
                                                   {"settings", QVariant::fromValue(&settings)},
                                                   {"testWidth", testWidth}}));
        QVERIFY2(root, qPrintable(component.errorString()));
        auto *window = qobject_cast<QQuickWindow *>(root.data());
        QVERIFY(window);
        QVERIFY(router.navigate("home"));
        QQuickItem *view = nullptr;
        QTRY_VERIFY((view = findVisual(window->contentItem(), "recommendationCardsView")));
        QTRY_VERIFY(findVisual(window->contentItem(), "recommendationPanel-daily"));
        QJsonArray songs;
        for (int i = 1; i <= 6; ++i)
            songs.append(domainSong(i));

        service.recommendationCards()->find("daily")->accept({{"songs", songs}});
        service.recommendationCards()->find("style")->accept({{"songs", songs}});
        QTest::qWait(50);
        const auto response = [&](const QString &key) {
            QJsonObject result{{"songs", songs}};
            if (key == "3102" || key == "3103") {
                QJsonArray preview;
                for (int i = 0; i < 3; ++i)
                    preview.append(QJsonObject{
                        {"id", QString::number(i + 1)}, {"title", "主题歌曲"}, {"artist", "歌手"}});
                QJsonObject group{{"title", "主题"},
                                  {"description", "按真实呈现结构测试多种推荐内容"},
                                  {"preview", preview},
                                  {"start_index", 0},
                                  {"song_count", 3}};
                result["groups"] = QJsonArray{group, group};
            }
            return result;
        };
        const auto number = [&](const char *name) { return view->property(name).toReal(); };
        const auto visibleBottom = [&] {
            qreal bottom = number("contentY");
            auto *content = view->property("contentItem").value<QQuickItem *>();
            for (auto *slot : content->childItems()) {
                if (slot->property("card").isValid() &&
                    slot->y() < number("contentY") + view->height())
                    bottom = std::max(bottom, slot->y() + slot->height());
            }
            return bottom;
        };
        const auto bottom =
            number("originY") + number("contentHeight") - view->height() + number("bottomMargin");
        view->setProperty("contentY", bottom);
        QMetaObject::invokeMethod(view, "forceLayout");
        auto *lastPanel = findVisual(window->contentItem(), "recommendationPanel-3107");
        QVERIFY(lastPanel);
        // Establish the first rendered bottom frame, after native bounds fixup;
        // coordinates before that first paint are not a stationary viewport.
        Test::FrameSpy bottomFrame(window);
        window->update();
        QVERIFY(bottomFrame.wait(1000));
        // originY is an estimate when preceding variable-height delegates are
        // absent. Follow a real visible section in viewport coordinates instead.
        const auto anchorY = [&] { return lastPanel->mapToItem(view, QPointF()).y(); };
        qreal maximumBlank = 0, maximumShift = 0;
        qreal previousY = anchorY();
        for (int frame = 0; frame < 40; ++frame) {
            if (frame >= 4 && frame < 4 + keys.size())
                service.recommendationCards()
                    ->find(keys[frame - 4])
                    ->accept(response(keys[frame - 4]));
            window->update();
            QTest::qWait(16);
            const qreal y = number("contentY");
            const qreal blank = y + view->height() - visibleBottom();
            maximumBlank = std::max(maximumBlank, blank);
            maximumShift = std::max(maximumShift, qAbs(anchorY() - previousY));
            previousY = anchorY();
        }
        qInfo() << "home-fast-bottom maxBlank/maxShift" << maximumBlank << maximumShift;
        QVERIFY2(maximumBlank <= 25,
                 "Fast bottom scroll left an uncovered viewport beyond its 24px margin");
        QVERIFY2(maximumShift <= 1,
                 "Loaded sections moved the stationary viewport after a fast bottom scroll");
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
        root.reset();
        service.shutdown();
        QSignalSpy drained(&playback, &PlaybackService::closed);
        playback.shutdown();
        QTRY_COMPARE(drained.size(), 1);
        api.shutdown();
    }

    void homeScrollGeometryRemainsStable_data()
    {
        QTest::addColumn<bool>("dark");
        QTest::addColumn<int>("zoomSteps");
        QTest::addColumn<int>("windowWidth");
        QTest::newRow("light-100") << false << 0 << 1150;
        QTest::newRow("dark-100") << true << 0 << 1150;
        QTest::newRow("light-125") << false << 5 << 1150;
        QTest::newRow("dark-125") << true << 5 << 1150;
        QTest::newRow("narrow-light-100") << false << 0 << 780;
        QTest::newRow("narrow-dark-125") << true << 5 << 780;
        QTest::newRow("wide-light-100") << false << 0 << 1400;
    }

    void unchangedMetadataReadNotifiesColdPresentationDemand()
    {
        QTemporaryDir dir;
        SessionDatabase cache(dir.filePath("session"));
        SongCatalogModel model;
        model.enableSessionStorage(cache);
        model.replaceSongs({domainSong(1)});
        QTRY_VERIFY(model.storageReady());
        QCOMPARE(SongCatalogStorageTestAccess::statistics(model).entries, 0);
        QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
        SongCatalogStorageTestAccess::raceUnchangedMetadataWithViewRead(model);
        QTRY_VERIFY(model.data(model.index(0), SongCatalogModel::PresentationReady).toBool());
        QVERIFY(!changed.isEmpty());
        QSignalSpy drained(&cache, &SessionDatabase::closed);
        cache.shutdown();
        QTRY_COMPARE(drained.size(), 1);
    }

    void homeScrollGeometryRemainsStable()
    {
        QFETCH(bool, dark);
        QFETCH(int, zoomSteps);
        QFETCH(int, windowWidth);
        QTemporaryDir dir;
        SettingsStore prefs(dir.filePath("prefs.ini"));
        ApiClient api(DeviceIdentity::temporary());
        AccountService account(api);
        PlaybackService playback(api, prefs, nullptr, dir.filePath("playback.sqlite"));
        SessionDatabase cache(dir.filePath("session"));
        SongMetadataService metadata(api);
        metadata.enableSqlIndex(cache);
        playback.attachMetadata(metadata);
        ThemeStore theme(prefs);
        theme.setMode(dark ? ThemeStore::Dark : ThemeStore::Light);
        theme.setMotionEnabled(false);
        theme.setFloatingFrosted(false);
        ShellSettings settings(prefs);
        settings.acceptUserAgreement();
        for (int i = 0; i < zoomSteps; ++i)
            settings.zoomIn();
        RouteController router;
        DiscoveryService service(api, playback, account);
        const QStringList stressCards{"daily", "style", "3001", "3004", "3005", "3006",
                                      "3014",  "3101",  "3102", "3103", "new"};
        prefs.setValue("discovery/cards", stressCards);
        prefs.setValue("discovery/cardsDefaultsVersion", 1);
        service.setPreferences(&prefs);
        DiscoveryRouteCoordinator routes(service, router, false);
        QQmlEngine engine;
        theme.attachToEngine(engine);
        engine.addImageProvider("home", new ArtistImageRequests);
        QStringList warnings;
        QObject observer;
        connect(&engine, &QQmlEngine::warnings, &observer, [&](const auto &messages) {
            for (const auto &m : messages)
                warnings.append(m.toString());
        });
        QQmlComponent component(&engine);
        component.setData(R"(
            import QtQuick
            import QtQuick.Controls.Basic
            import EchoMusic.Shell
            import EchoMusic.Theme
            ApplicationWindow {
                id: host
                required property RouteController router
                required property ShellSettings settings
                required property int testWidth
                visible: true; width: testWidth; height: 800; color: Theme.main
                Item { width: parent.width / host.settings.uiScale; height: parent.height / host.settings.uiScale; scale: host.settings.uiScale; transformOrigin: Item.TopLeft
                    RouteHost { anchors.fill: parent; router: host.router; settings: host.settings }
                }
            })",
                          QUrl("qrc:/recommendation-ui-test.qml"));
        QScopedPointer<QObject> root(
            component.createWithInitialProperties({{"router", QVariant::fromValue(&router)},
                                                   {"settings", QVariant::fromValue(&settings)},
                                                   {"testWidth", windowWidth}}));
        QVERIFY2(root, qPrintable(component.errorString()));
        auto *window = qobject_cast<QQuickWindow *>(root.data());
        QVERIFY(window);
        QVERIFY(Test::prepareNativeInput(window));
        QVERIFY(router.navigate("home"));
        QJsonArray songs;
        QJsonArray compositionSongs;
        const auto compositionSource = qEnvironmentVariable("ECHO_HOME_COMPOSITION_SOURCE");
        if (!compositionSource.isEmpty()) {
            QFile fixture(compositionSource);
            QVERIFY(fixture.open(QIODevice::ReadOnly));
            compositionSongs =
                QJsonDocument::fromJson(fixture.readAll()).object()["songs"].toArray();
            QVERIFY(compositionSongs.size() >= 6);
        }
        for (int i = 1; i <= 1000; ++i) {
            auto song = domainSong(i, "这是一首用于核对歌名空间的中文长歌名");
            if (i <= std::min(6, int(compositionSongs.size())))
                song = compositionSongs[i - 1].toObject();
            else
                song["cover_url"] = QString("image://home/%1").arg(i);
            songs.append(song);
        }
        DiscoveryServiceTestAccess::complete(service, DiscoveryService::Daily,
                                             QJsonObject{{"songs", songs}, {"date", "20261124"}});
        DiscoveryServiceTestAccess::complete(
            service, DiscoveryService::Style,
            QJsonObject{
                {"songs", songs},
                {"groups",
                 QJsonArray{QJsonObject{
                     {"name", "风格"},
                     {"child", QJsonArray{QJsonObject{{"id", "test-pop"}, {"name", "流行"}},
                                          QJsonObject{{"id", "test-rock"}, {"name", "摇滚"}}}}}}}});
        auto *card = service.recommendationCards()->find("3001");
        card->accept({{"songs", songs}});
        if (!compositionSource.isEmpty())
            service.recommendationCards()->find("new")->accept({{"songs", songs}});
        QTRY_VERIFY(findVisual(window->contentItem(), "recommendationCardsView"));
        QPointer<QQuickItem> view = findVisual(window->contentItem(), "recommendationCardsView");
        QTRY_VERIFY(findVisual(window->contentItem(), "recommendationPreview"));
        auto *dailyInformation =
            findVisual(window->contentItem(), "recommendationInformation-daily");
        QVERIFY(dailyInformation);
        QTRY_COMPARE(dailyInformation->property("text").toString(), QString("11月24日"));
        QQuickItem *stylePreview = nullptr, *styleFilters = nullptr;
        QTRY_VERIFY(
            (stylePreview = findVisual(window->contentItem(), "styleRecommendationPreview")) &&
            (styleFilters = findVisual(window->contentItem(), "styleRecommendationFilters")));
        QTRY_VERIFY(stylePreview->height() >= 186);
        QVERIFY(stylePreview->mapToScene(QPointF(0, 0)).y() >=
                styleFilters->mapToScene(QPointF(0, styleFilters->height())).y());
        for (const auto &key : {QString("daily"), QString("style")}) {
            auto *section = findVisual(window->contentItem(), "recommendationPanel-" + key);
            auto *heading = findVisual(section, "recommendationHeading-" + key);
            auto *detail = key == "daily" ? dailyInformation : styleFilters;
            QVERIFY(section && heading && detail);
            if (section->property("inlineInformation").toBool()) {
                const auto titleBounds = heading->mapRectToScene(heading->boundingRect());
                const auto detailBounds = detail->mapRectToScene(detail->boundingRect());
                QVERIFY(detailBounds.left() >= titleBounds.right());
                QVERIFY(detailBounds.top() < titleBounds.bottom() &&
                        detailBounds.bottom() > titleBounds.top());
            }
            if (section->width() > 600)
                QVERIFY(section->property("inlineInformation").toBool());
        }

        const auto countVisual = [&](const QString &name) {
            int count = 0;
            QList<QQuickItem *> pending{window->contentItem()};
            while (!pending.isEmpty()) {
                auto *item = pending.takeLast();
                pending.append(item->childItems());
                if (item->objectName() == name)
                    ++count;
            }
            return count;
        };
        QCOMPARE(view->property("spacing").toReal(), 32.);
        auto *dailyPanel = findVisual(window->contentItem(), "recommendationPanel-daily");
        auto *stylePanel = findVisual(window->contentItem(), "recommendationPanel-style");
        QVERIFY(dailyPanel && stylePanel);
        QCOMPARE(dailyPanel->x(), 32.);
        QCOMPARE(dailyPanel->width(), view->width() - 64);
        QCOMPARE(stylePanel->mapToItem(view, QPointF()).y() -
                     dailyPanel->mapToItem(view, QPointF()).y() - dailyPanel->height(),
                 32.);
        QVERIFY(countVisual("homeSongCard") <= 40);
        QVERIFY(countVisual("homeSongCard") > 0);
        QTRY_COMPARE(previewPresentationError(window->contentItem()), QString());
        auto *songRow = findVisual(window->contentItem(), "homeSongCard");
        auto *songTitle = findVisual(songRow, "recommendationSongTitle");
        auto *songTags = findVisual(songRow, "recommendationSongTag");
        auto *songArtist = findVisual(songRow, "recommendationSongArtist");
        auto *artwork = findVisual(songRow, "recommendationSongArtwork");
        auto *info = findVisual(songRow, "recommendationSongInfo");
        auto *favorite = findVisual(songRow, "recommendationSongFavorite");
        QVERIFY(songTitle && songTags && songArtist);
        QVERIFY(artwork && info);
        QVERIFY(favorite);
        QCOMPARE(favorite->size(), QSizeF(32, 32));
        QCOMPARE(favorite->property("padding").toReal(), 5.);
        QCOMPARE(artwork->height(), 54.);
        QVERIFY(std::abs(artwork->mapToScene(QPointF(0, artwork->height() / 2)).y() -
                         info->mapToScene(QPointF(0, info->height() / 2)).y()) < 1);
        auto *nameBox = songTitle->property("textBox").value<QQuickItem *>();
        // Short titles retain their natural width; long titles must still have
        // enough room beside the inline badges.
        QVERIFY(nameBox && (nameBox->width() >= 90 || !songTitle->property("truncated").toBool()));
        QVERIFY(
            songTags->mapToScene(QPointF(0, 0)).x() >=
            songArtist->property("textBox").value<QQuickItem *>()->mapToScene(QPointF(0, 0)).x());
        QVERIFY(std::abs(songTags->mapToScene(QPointF(0, songTags->height() / 2)).y() -
                         songArtist->mapToScene(QPointF(0, songArtist->height() / 2)).y()) < 1);
        // The style picker is created only when requested and supports multiple
        // selections without recreating the popup after each choice.
        QVERIFY(!findVisualOwnerObject(window->contentItem(), "styleRecommendationPicker"));
        QTest::mouseClick(
            window, Qt::LeftButton, Qt::NoModifier,
            styleFilters->mapToScene(QPointF(styleFilters->width() / 2, styleFilters->height() / 2))
                .toPoint());
        QPointer<QObject> picker;
        QTRY_VERIFY(
            (picker = findVisualOwnerObject(window->contentItem(), "styleRecommendationPicker")));
        QTRY_VERIFY(picker->property("opened").toBool());
        for (const auto &id : {"test-pop", "test-rock"}) {
            auto *option = findVisual(window->contentItem(), "option-" + QString::fromLatin1(id));
            QVERIFY(option);
            QTest::mouseClick(
                window, Qt::LeftButton, Qt::NoModifier,
                option->mapToScene(QPointF(option->width() / 2, option->height() / 2)).toPoint());
            QTRY_VERIFY(service.selectedTags().contains(QString::fromLatin1(id)));
            QVERIFY(picker && picker->property("opened").toBool());
        }
        QTest::keyClick(window, Qt::Key_Escape);
        QTRY_VERIFY(picker.isNull());
        QVERIFY(Test::prepareNativeInput(window));
        QList<qint64> presentLatencies;
        for (int step = 1; step <= 20; ++step) {
            Test::FrameSpy frame(window);
            QElapsedTimer elapsed;
            elapsed.start();
            view->setProperty("contentY", -108. + step * 48);
            window->update();
            QVERIFY(frame.wait(1000));
            presentLatencies.append(elapsed.nsecsElapsed() / 1000);
            QVERIFY(countVisual("homeSongCard") <= 40);
        }
        std::sort(presentLatencies.begin(), presentLatencies.end());
        qInfo() << "home scroll to presented frame us p50/p95/max" << presentLatencies[10]
                << presentLatencies[18] << presentLatencies.last();
        view->setProperty("contentY", -108.);
        const auto artifacts = qEnvironmentVariable("ECHO_HOME_SCROLL_ARTIFACTS");
        if (!artifacts.isEmpty()) {
            QDir().mkpath(artifacts);
            window->grabWindow().save(artifacts + "/" +
                                      QString::fromLatin1(QTest::currentDataTag()) + ".png");
        }
        // Config creates only on demand and destroys the complete Popup at close.
        service.recommendationCards()->restoreDefaults();
        if (!artifacts.isEmpty()) {
            QTRY_COMPARE(view->property("count").toInt(), 4);
            QTRY_COMPARE(previewPresentationError(window->contentItem()), QString());
            window->grabWindow().save(artifacts + "/home-defaults-" +
                                      QString::fromLatin1(QTest::currentDataTag()) + ".png");
        }
        QVERIFY(!findVisualOwnerObject(window->contentItem(), "recommendationConfigPopup"));
        auto *add = findVisual(window->contentItem(), "recommendationAddCard");
        QVERIFY(add);
        QCOMPARE(add->property("text").toString(), QStringLiteral("管理推荐"));
        QVERIFY(std::abs(add->mapToScene(QPointF(add->width(), 0)).x() -
                         view->mapToScene(QPointF(view->width() - 32, 0)).x()) < 1);
        Test::FrameSpy popupFrame(window);
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                          add->mapToScene(QPointF(add->width() / 2, add->height() / 2)).toPoint());
        QPointer<QObject> popup;
        QTRY_VERIFY(
            (popup = findVisualOwnerObject(window->contentItem(), "recommendationConfigPopup")));
        QTRY_VERIFY(popup->property("opened").toBool());
        QCOMPARE(popup->property("heading").toString(), QStringLiteral("管理推荐"));
        QTRY_VERIFY(!findVisual(window->contentItem(), "recommendationPageScrollBar")->isVisible());
        auto *configList = findVisual(window->contentItem(), "recommendationConfigList");
        QVERIFY(configList);
        QCOMPARE(countVisual("popupScrollBar"), 1);
        auto *configIcon = findVisual(window->contentItem(), "recommendationConfigIcon-daily");
        QVERIFY(configIcon);
        QTRY_COMPARE(configIcon->property("status").toInt(), 1);
        auto *remove = findVisual(window->contentItem(), "recommendationRemove-daily");
        QVERIFY(remove);
        auto *rowBody = remove->parentItem()->parentItem();
        QVERIFY(rowBody->mapToScene(QPointF(rowBody->width(), 0)).x() -
                    remove->mapToScene(QPointF(remove->width(), 0)).x() >=
                11 * settings.uiScale());
        auto *reorder = findVisual(window->contentItem(), "recommendationCardReorder");
        QVERIFY(reorder);
        QTRY_VERIFY(reorder->property("usable").toBool());
        QTRY_VERIFY(!popupFrame.isEmpty());
        if (!artifacts.isEmpty())
            window->grabWindow().save(artifacts + "/config-top-" +
                                      QString::fromLatin1(QTest::currentDataTag()) + ".png");
        const auto startY = configList->property("contentY").toReal();
        configList->setProperty("contentY", configList->property("originY").toReal() +
                                                configList->property("contentHeight").toReal() -
                                                configList->height());
        auto *candidate = findVisual(window->contentItem(), "recommendationAdd-3107");
        QVERIFY(candidate);
        const auto beforeAdd = service.recommendationCards()->count();
        QTest::mouseClick(
            window, Qt::LeftButton, Qt::NoModifier,
            candidate->mapToScene(QPointF(candidate->width() / 2, candidate->height() / 2))
                .toPoint());
        QTRY_COMPARE(service.recommendationCards()->count(), beforeAdd + 1);
        service.recommendationCards()->toggle("3107");
        QCOMPARE(service.recommendationCards()->count(), beforeAdd);
        if (!artifacts.isEmpty())
            window->grabWindow().save(artifacts + "/config-" +
                                      QString::fromLatin1(QTest::currentDataTag()) + ".png");
        configList->setProperty("contentY", startY);
        const auto firstKey = service.recommendationCards()->at(0)->key();
        const auto secondKey = service.recommendationCards()->at(1)->key();
        QQuickItem *firstRow = nullptr;
        // Find index zero rather than assuming a viewport without a header.
        QTRY_VERIFY(QMetaObject::invokeMethod(configList, "itemAtIndex",
                                              Q_RETURN_ARG(QQuickItem *, firstRow),
                                              Q_ARG(int, 0)) &&
                    firstRow);
        const auto press = firstRow->mapToScene(QPointF(16, 26)).toPoint();
        const auto release = press + QPoint(0, qRound(58 * settings.uiScale()));
        QTest::mouseMove(window, press);
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, press);
        QTest::mouseMove(window, press + QPoint(0, 16), 30);
        QTRY_COMPARE(reorder->property("phase").toString(), "dragging");
        QTest::mouseMove(window, release, 50);
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, release);
        QTRY_COMPARE(service.recommendationCards()->at(0)->key(), secondKey);
        QCOMPARE(service.recommendationCards()->at(1)->key(), firstKey);
        QTest::keyClick(window, Qt::Key_Escape);
        QTRY_VERIFY(popup.isNull());
        QTRY_VERIFY(findVisual(window->contentItem(), "recommendationPageScrollBar")->isVisible());
        for (const auto &key : stressCards)
            if (!service.recommendationCards()->contains(key))
                service.recommendationCards()->toggle(key);
        QTRY_COMPARE(view->property("count").toInt(), stressCards.size());
        QTRY_VERIFY(view->property("contentHeight").toReal() > view->height());
        const qreal target = 700;
        auto *pageViewport = findVisual(window->contentItem(), "recommendationViewport");
        QVERIFY(pageViewport);
        QVERIFY(QMetaObject::invokeMethod(pageViewport, "scrollTo", Q_ARG(double, target + 108)));
        QTest::qWait(30);
        QVERIFY(view->property("contentY").toReal() - view->property("originY").toReal() > 0);
        auto *savedPanel = findVisual(window->contentItem(), "recommendationPanel-3001");
        QVERIFY(savedPanel);
        const auto savedPanelY = savedPanel->mapToScene(QPointF()).y();
        auto *source = card->collection();
        QVERIFY(SongCatalogStorageTestAccess::hasStore(*source));
        const auto hot = SongCatalogStorageTestAccess::statistics(*source);
        QVERIFY(hot.entries <= 256);
        QVERIFY(hot.cost <= 4 * 1024 * 1024);
        QVERIFY(router.navigate("recommendation-detail", {}, "3001"));
        QTRY_VERIFY(view.isNull());
        QCOMPARE(service.detailCard()->collection(), source);
        QCOMPARE(source->totalCount(), 1000);
        router.back();
        QTRY_VERIFY((view = findVisual(window->contentItem(), "recommendationCardsView")));
        QQuickItem *restoredPanel = nullptr;
        QTRY_VERIFY(
            (restoredPanel = findVisual(window->contentItem(), "recommendationPanel-3001")));
        QTRY_VERIFY(std::abs(restoredPanel->mapToScene(QPointF()).y() - savedPanelY) < 1);
        QCOMPARE(card->collection(), source);
        QCOMPARE(source->totalCount(), 1000);
        QVERIFY(countVisual("homeSongCard") <= 40);
        for (int round = 0; round < 3; ++round) {
            QPointer<QQuickItem> old = view;
            QVERIFY(router.navigate("recommendation-detail", {}, "3001"));
            QTRY_VERIFY(old.isNull());
            router.back();
            QTRY_VERIFY((view = findVisual(window->contentItem(), "recommendationCardsView")));
        }
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
        root.reset();
        service.shutdown();
        QSignalSpy drained(&playback, &PlaybackService::closed);
        playback.shutdown();
        QTRY_COMPARE_WITH_TIMEOUT(drained.size(), 1, 15000);
        QSignalSpy metadataDrained(&metadata, &SongMetadataService::closed);
        metadata.shutdown();
        QTRY_COMPARE_WITH_TIMEOUT(metadataDrained.size(), 1, 15000);
        QSignalSpy cacheDrained(&cache, &SessionDatabase::closed);
        cache.shutdown();
        QTRY_COMPARE_WITH_TIMEOUT(cacheDrained.size(), 1, 15000);
        api.shutdown();
    }

    void artistDirectorySlicesRetainIdentityWithoutDuplicatingTheCatalog()
    {
        auto source = std::make_unique<SongCatalogModel>();
        ArtistDirectorySliceModel featured, names;
        names.setFeatured(false);
        featured.setSourceModel(source.get());
        names.setSourceModel(source.get());
        QAbstractItemModelTester firstCheck(&featured,
                                            QAbstractItemModelTester::FailureReportingMode::QtTest);
        QAbstractItemModelTester tailCheck(&names,
                                           QAbstractItemModelTester::FailureReportingMode::QtTest);
        QJsonArray cards;
        for (int i = 1; i <= 21; ++i)
            cards.append(
                QJsonObject{{"id", QString::number(i)}, {"title", "歌手 " + QString::number(i)}});
        source->replaceCards(cards);
        QCOMPARE(featured.count(), 20);
        QCOMPARE(names.count(), 1);
        QCOMPARE(featured.index(19, 0).data(SongCatalogModel::ResourceId).toString(), "20");
        QCOMPARE(names.index(0, 0).data(SongCatalogModel::ResourceId).toString(), "21");
        featured.setAvatarLimit(3);
        names.setAvatarLimit(3);
        QCOMPARE(featured.count(), 3);
        QCOMPARE(names.count(), 18);
        QCOMPARE(names.index(0, 0).data(SongCatalogModel::ResourceId).toString(), "4");
        source->toggleSort(2);
        QCOMPARE(featured.index(0, 0).data(SongCatalogModel::ResourceId),
                 source->index(0, 0).data(SongCatalogModel::ResourceId));
        QCOMPARE(names.index(0, 0).data(SongCatalogModel::ResourceId),
                 source->index(3, 0).data(SongCatalogModel::ResourceId));
        source->replaceCards({QJsonObject{{"id", "replacement"}, {"title", "新歌手"}}});
        QCOMPARE(featured.count(), 1);
        QCOMPARE(names.count(), 0);
        featured.setAvatarLimit(-1);
        names.setAvatarLimit(-1);
        QCOMPARE(featured.count(), 0);
        QCOMPARE(names.count(), 1);
        QCOMPARE(names.index(0, 0).data(SongCatalogModel::ResourceId).toString(), "replacement");
        source.reset();
        QCOMPARE(featured.count(), 0);
        QCOMPARE(names.count(), 0);
        QStandardItemModel incremental;
        featured.setAvatarLimit(20);
        names.setAvatarLimit(20);
        featured.setSourceModel(&incremental);
        names.setSourceModel(&incremental);
        for (int i = 1; i <= 21; ++i)
            incremental.appendRow(new QStandardItem(QString::number(i)));
        QCOMPARE(featured.count(), 20);
        QCOMPARE(names.count(), 1);
        incremental.insertRow(0, new QStandardItem("front"));
        QCOMPARE(featured.count(), 20);
        QCOMPARE(names.count(), 2);
        QCOMPARE(names.index(0, 0).data().toString(), "20");
        incremental.removeRow(0);
        QCOMPARE(featured.count(), 20);
        QCOMPARE(names.index(0, 0).data().toString(), "21");
    }
    void artistDirectoryLoadsOnlyTheFeaturedAvatars_data()
    {
        QTest::addColumn<bool>("stored");
        QTest::newRow("memory") << false;
        QTest::newRow("session-sqlite") << true;
    }
    void artistDirectoryLoadsOnlyTheFeaturedAvatars()
    {
        QFETCH(bool, stored);
        QTemporaryDir dir;
        SessionDatabase database(dir.filePath("session"));
        EchoMusic::SettingsStore prefs(dir.filePath("prefs.ini"));
        ThemeStore theme(prefs);
        theme.setMotionEnabled(false);
        ShellSettings settings(prefs);
        SongCatalogModel collection;
        ArtistDirectoryModel storedCollection;
        storedCollection.attach(&database);
        const auto replace = [&](QJsonArray values) {
            if (stored)
                storedCollection.replace({QJsonObject{{"label", "热门"}, {"artists", values}}});
            else
                collection.replaceCards(values);
        };
        QQmlEngine engine;
        theme.attachToEngine(engine);
        auto *images = new ArtistImageRequests;
        engine.addImageProvider("artist-limit", images);
        QSignalSpy warnings(&engine, &QQmlEngine::warnings);
        QJsonArray cards;
        for (int i = 1; i <= 1000; ++i)
            cards.append(QJsonObject{
                {"id", QString::number(i)},
                {"title", i == 21 ? QStringLiteral("第21位长名称歌手 Artist ").repeated(8)
                                  : "歌手 Artist " + QString::number(i)},
                {"cover_url", "image://artist-limit/" + QString::number(i)}});
        replace(cards);
        QQmlComponent component(&engine);
        component.setData(R"(
            import QtQuick
            import QtQuick.Window
            import EchoMusic.Music
            import EchoMusic.Controls
            import EchoMusic.Theme
            import EchoMusic.Shell
            Window {
                id: window
                required property var collection
                required property ShellSettings settings
                property real uiScale: settings.uiScale
                property string openedArtist: ""
                function scrollDirectory(offset: real): void { viewport.scrollTo(offset); }
                function showNames(): void {
                    viewport.scrollTo(artists.headerItem.height);
                    artists.currentIndex = 0;
                }
                visible: true; width: 1000; height: 700
                Rectangle { anchors.fill: parent; color: Theme.main }
                Item {
                    width: parent.width / window.uiScale; height: parent.height / window.uiScale
                    scale: window.uiScale; transformOrigin: Item.TopLeft
                    EchoPageScrollContainer {
                        id: viewport
                        anchors.fill: parent; scrollView: artists
                        ArtistDirectory {
                            id: artists
                            objectName: "artistsUnderTest"
                            x: 40; width: parent.width - 60; height: parent.height
                            collection: window.collection
                            navigationAvailable: true
                            pageContainer: viewport
                            onOpenRequested: id => window.openedArtist = id
                        }
                    }
                }
            })",
                          QUrl("qrc:/artist-limit-test.qml"));
        QScopedPointer<QObject> root(component.createWithInitialProperties(
            {{"collection", QVariant::fromValue(stored ? static_cast<QObject *>(&storedCollection)
                                                       : static_cast<QObject *>(&collection))},
             {"settings", QVariant::fromValue(&settings)}}));
        QVERIFY2(root, qPrintable(component.errorString()));
        auto *window = qobject_cast<QQuickWindow *>(root.data());
        QVERIFY(window);
        auto *view = findVisual(window->contentItem(), "artistsUnderTest");
        QVERIFY(view);
        QVERIFY(Test::prepareNativeInput(window));
        QTRY_COMPARE(view->property("featuredCount").toInt(), 20);
        QTRY_COMPARE(view->property("count").toInt(), 980);
        QTRY_VERIFY(!images->requested().isEmpty());
        QCOMPARE(countVisual(view, "artistAvatarCard"), 20);
        const auto artifacts = qEnvironmentVariable("ECHO_ARTIST_DIRECTORY_ARTIFACTS");
        int peakNames = 0;
        if (!artifacts.isEmpty())
            QVERIFY(QDir().mkpath(artifacts));
        for (bool dark : {false, true})
            for (double scale : {1., 1.25}) {
                theme.setMode(dark ? ThemeStore::Dark : ThemeStore::Light);
                settings.resetZoom();
                if (scale > 1.)
                    for (int i = 0; i < 5; ++i)
                        settings.zoomIn();
                for (int width : {1000, 650}) {
                    window->resize(width, 700);
                    QTest::qWait(40);
                    QVERIFY(QMetaObject::invokeMethod(root.data(), "scrollDirectory",
                                                      Q_ARG(double, 0.)));
                    QTest::qWait(50);
                    auto *avatar = findVisual(view, "artistAvatarCard");
                    QVERIFY(avatar);
                    QVERIFY(!avatar->property("background").value<QQuickItem *>());
                    auto *portrait = findVisual(avatar, "artistDirectoryAvatar");
                    auto *caption = findVisual(avatar, "artistDirectoryCaption");
                    QVERIFY(portrait && caption);
                    QVERIFY(qAbs(portrait->width() - avatar->width()) < 0.5);
                    QVERIFY(qAbs(portrait->height() - portrait->width()) < 0.5);
                    QCOMPARE(caption->property("horizontalAlignment").toInt(),
                             int(Qt::AlignHCenter));
                    const auto avatarColumnCenter =
                        avatar->mapToItem(view, QPointF(avatar->width() / 2, 0)).x();
                    if (!artifacts.isEmpty())
                        QVERIFY(
                            window->grabWindow().save(artifacts + QString("/avatars-%1-%2-w%3.png")
                                                                      .arg(dark ? "dark" : "light")
                                                                      .arg(scale)
                                                                      .arg(width)));
                    // Visit all featured rows, then the first and final names.
                    const auto *header = view->property("headerItem").value<QQuickItem *>();
                    QVERIFY(header);
                    for (qreal offset = 0; offset < header->height(); offset += 250) {
                        QVERIFY(QMetaObject::invokeMethod(root.data(), "scrollDirectory",
                                                          Q_ARG(double, offset)));
                        QTest::qWait(12);
                    }
                    QVERIFY(QMetaObject::invokeMethod(root.data(), "showNames"));
                    QTRY_VERIFY(view->property("currentItem").value<QQuickItem *>());
                    QTest::qWait(40);
                    auto *firstName = view->property("currentItem").value<QQuickItem *>();
                    QVERIFY(firstName);
                    QCOMPARE(firstName->property("resourceId").toString(), "21");
                    QCOMPARE(countVisual(firstName, "coverImage"), 0);
                    QVERIFY(
                        qAbs(firstName->mapToItem(view, QPointF(firstName->width() / 2, 0)).x() -
                             avatarColumnCenter) < 0.5);
                    auto *nameContent = firstName->property("contentItem").value<QQuickItem *>();
                    QVERIFY(nameContent);
                    QCOMPARE(nameContent->property("horizontalAlignment").toInt(),
                             int(Qt::AlignHCenter));
                    QTRY_VERIFY(firstName->property("presentationReady").toBool());
                    QSignalSpy opened(view, SIGNAL(openRequested(QString)));
                    firstName->forceActiveFocus(Qt::TabFocusReason);
                    QTest::keyClick(window, Qt::Key_Space);
                    QCOMPARE(opened.size(), 1);
                    QCOMPARE(opened.first().first().toString(),
                             firstName->property("resourceId").toString());
                    QVERIFY(firstName->property("resourceId").toString().toInt() >= 21);
                    peakNames = qMax(peakNames, countVisual(view, "artistNameEntry"));
                    QVERIFY2(peakNames > 0 && peakNames < 150,
                             qPrintable(QString("names=%1 width=%2 scale=%3 dark=%4")
                                            .arg(peakNames)
                                            .arg(width)
                                            .arg(scale)
                                            .arg(dark)));
                    if (!artifacts.isEmpty())
                        QVERIFY(
                            window->grabWindow().save(artifacts + QString("/names-%1-%2-w%3.png")
                                                                      .arg(dark ? "dark" : "light")
                                                                      .arg(scale)
                                                                      .arg(width)));
                    view->setProperty("currentIndex", 979);
                    QVERIFY(QMetaObject::invokeMethod(view, "positionViewAtEnd"));
                    QTest::qWait(40);
                    QTRY_VERIFY(view->property("currentItem").value<QQuickItem *>());
                    auto *lastName = view->property("currentItem").value<QQuickItem *>();
                    QCOMPARE(lastName->property("resourceId").toString(), "1000");
                    QVERIFY(lastName->mapRectToItem(view, lastName->boundingRect())
                                .intersects(view->boundingRect()));
                    peakNames = qMax(peakNames, countVisual(view, "artistNameEntry"));
                    QVERIFY(peakNames > 0 && peakNames < 150);
                    for (const auto &id : images->requested())
                        QVERIFY2(id.toInt() >= 1 && id.toInt() <= 20, qPrintable(id));
                }
            }
        // An incomplete avatar row stays separate; text starts at the first column
        // of the following row instead of filling empty avatar slots.
        settings.resetZoom();
        window->resize(1000, 700);
        view->setProperty("avatarLimit", 6);
        QTRY_COMPARE(view->property("featuredCount").toInt(), 6);
        QTRY_COMPARE(view->property("count").toInt(), 994);
        QVERIFY(QMetaObject::invokeMethod(root.data(), "scrollDirectory", Q_ARG(double, 0.)));
        QTest::qWait(60);
        QCOMPARE(view->property("columns").toInt(), 4);
        QCOMPARE(countVisual(view, "artistAvatarCard"), 6);
        const auto *partialHeader = view->property("headerItem").value<QQuickItem *>();
        QVERIFY(partialHeader);
        QVERIFY(qAbs(partialHeader->height() - (2 * view->property("cardHeight").toReal() + 40)) <
                0.5);
        if (!artifacts.isEmpty())
            QVERIFY(window->grabWindow().save(artifacts + "/partial-avatar-row.png"));
        QVERIFY(QMetaObject::invokeMethod(root.data(), "showNames"));
        QTRY_VERIFY(view->property("currentItem").value<QQuickItem *>());
        QCOMPARE(
            view->property("currentItem").value<QQuickItem *>()->property("resourceId").toString(),
            "7");
        view->setProperty("pageActive", false);
        qInfo("Artist1000: %d distinct avatar URLs requested; 20 bounded cards; peak %d "
              "materialized names",
              int(images->requested().size()), peakNames);
        QTest::qWait(30);
        const auto verifyReleased = [&](auto &&self, QQuickItem *item) -> void {
            if (item->objectName() == "coverImage")
                QVERIFY(item->property("source").toUrl().isEmpty());
            for (auto *child : item->childItems())
                self(self, child);
        };
        verifyReleased(verifyReleased, view);
        QCOMPARE(stored ? storedCollection.count() : collection.count(), 1000);
        replace({QJsonObject{{"id", "new"}, {"title", "筛选后的歌手"}}});
        QTRY_COMPARE(view->property("featuredCount").toInt(), 1);
        QTRY_COMPARE(view->property("count").toInt(), 0);
        if (stored)
            storedCollection.clear();
        else
            collection.clear();
        QTRY_COMPARE(view->property("featuredCount").toInt(), 0);
        QCOMPARE(warnings.size(), 0);
    }
    void catalogFilterSortSelectionAndMetadata()
    {
        SongCatalogModel model;
        QAbstractItemModelTester check(&model,
                                       QAbstractItemModelTester::FailureReportingMode::QtTest);
        model.replaceSongs(
            {domainSong(1, "乙 Beta"), domainSong(2, "甲 Alpha"), domainSong(3, "丙 Gamma")});
        QCOMPARE(model.count(), 3);
        model.toggleSelected("1");
        model.setQuery("Alpha");
        QCOMPARE(model.count(), 1);
        model.selectAll(true);
        QCOMPARE(model.selectedCount(), 3);
        QCOMPARE(readCatalogTracks(&model, true).size(), 3);
        model.toggleSelected("1");
        QCOMPARE(model.selectedCount(), 2);
        QCOMPARE(readCatalogTracks(&model, true).first().id, QString("2"));
        model.setQuery({});
        model.toggleSort(2);
        const auto ascending = model.item(0).value("resourceId");
        model.toggleSort(2);
        QVERIFY(model.item(0).value("resourceId") != ascending);
        model.toggleSort(2);
        QCOMPARE(model.item(0).value("resourceId").toString(), QString("1"));
        QCOMPARE(model.item(0).value("qualityLabel").toString(), QString("SQ"));
        QVERIFY(model.item(0).value("original").toBool());
        QCOMPARE(
            model.item(0).value("privilegeTags").toList().first().toMap().value("label").toString(),
            QString("VIP"));
        const auto track = readCatalogTracks(&model).first();
        QCOMPARE(track.catalogMetadata.value("audio_id").toInt(), 120);
        QVERIFY(!track.catalogMetadata.value("resources").toArray().isEmpty());
        auto mapOnlyHigh = domainSong(9);
        auto metadata = mapOnlyHigh["metadata"].toObject();
        metadata["rights"] = QJsonObject{{"quality_map", QJsonObject{{"attr0", 64}}}};
        // Cloud entries may include only base/ogg resource hashes. The explicit
        // encoded availability still says Hi-Res; do not discard that fact.
        mapOnlyHigh["metadata"] = metadata;
        SongCatalogModel high;
        high.replaceSongs({mapOnlyHigh});
        QCOMPARE(high.item(0)["qualityLabel"].toString(), QString("Hi-Res"));
        QCOMPARE(track.toDomain().value("metadata").toObject(), track.catalogMetadata);
    }
    void borrowedCommandsSurviveQmlGarbageCollection()
    {
        QTemporaryDir dir;
        EchoMusic::SettingsStore prefs(dir.filePath("prefs.ini"));
        ApiClient api(DeviceIdentity::temporary());
        AccountService account(api);
        PlaybackService playback(api, prefs, nullptr, dir.filePath("playback.sqlite"));
        DiscoveryService service(api, playback, account);
        QPointer<SongCatalogActions> commands = service.actions(service.dailySongs());
        {
            QQmlEngine engine;
            engine.globalObject().setProperty("service", engine.newQObject(&service));
            {
                const auto value = engine.evaluate("service.actions(service.dailySongs)");
                QVERIFY(!value.isError());
            }
            for (int i = 0; i < 3; ++i)
                engine.collectGarbage();
            QVERIFY(commands);
            QVERIFY(!commands->isFavorite("unknown"));
        }
        QVERIFY(commands);
        service.shutdown();
        QSignalSpy drain(&playback, &PlaybackService::closed);
        playback.shutdown();
        QTRY_COMPARE_WITH_TIMEOUT(drain.size(), 1, 15000);
        api.shutdown();
    }
    void filteringAndSortUseSourceFieldsAndTieBreaks()
    {
        SongCatalogModel model;
        auto a = domainSong(1, "同名 Song "), b = domainSong(2, "同名 Song");
        a["artists"] = QJsonArray{QJsonObject{{"id", 1}, {"name", "Beta"}}};
        b["artists"] = QJsonArray{QJsonObject{{"id", 2}, {"name", "Alpha"}}};
        model.replaceSongs({a, b});
        model.setQuery("Song  Beta");
        QCOMPARE(model.count(), 0); // Do not match across unrelated columns.
        model.setQuery({});
        model.toggleSort(2);
        QCOMPARE(model.item(0)["resourceId"].toString(), QString("2"));
        model.toggleSort(2);
        QCOMPARE(model.item(0)["resourceId"].toString(), QString("1"));
        model.toggleSort(2);
        QCOMPARE(model.item(0)["resourceId"].toString(), QString("1"));
        a["title"] = "éclair";
        b["title"] = "eclair";
        a["artists"] = QJsonArray{QJsonObject{{"id", 1}, {"name", "Alpha"}}};
        b["artists"] = QJsonArray{QJsonObject{{"id", 2}, {"name", "Beta"}}};
        model.replaceSongs({a, b});
        model.toggleSort(2);
        QCOMPARE(model.item(0)["resourceId"].toString(), QString("1"));
    }
    void independentRequestsAndSessionIsolation()
    {
        QTemporaryDir dir;
        EchoMusic::SettingsStore prefs(dir.filePath("prefs.ini"));
        prefs.setValue("playback/volume", 0.);
        ApiClient api(DeviceIdentity::temporary());
        AccountService account(api);
        PlaybackService playback(api, prefs, nullptr, dir.filePath("playback.sqlite"));
        DiscoveryService service(api, playback, account);
        RouteController router;
        DiscoveryRouteCoordinator routes(service, router, false);
        QVERIFY(router.navigate("home"));
        DiscoveryServiceTestAccess::pending(service, DiscoveryService::Style, 71);
        DiscoveryServiceTestAccess::pending(service, DiscoveryService::Playlists, 72);
        DiscoveryServiceTestAccess::complete(
            service, DiscoveryService::Playlists,
            QJsonArray{QJsonObject{{"id", "collection_1"}, {"title", "歌单"}}}, 72);
        QCOMPARE(service.playlists()->count(), 1);
        QVERIFY(DiscoveryServiceTestAccess::hasPending(service));
        QVERIFY(router.navigate("recommend-songs"));
        QVERIFY(!DiscoveryServiceTestAccess::hasPending(service));
        DiscoveryServiceTestAccess::retired(
            service, 71, {{"songs", QJsonArray{domainSong(10)}}, {"groups", QJsonArray{}}});
        QCOMPARE(service.styleSongs()->count(), 0);
        DiscoveryServiceTestAccess::complete(service, DiscoveryService::Daily,
                                             QJsonObject{{"songs", QJsonArray{domainSong(1)}},
                                                         {"date", "20261016"},
                                                         {"subtitle", "真实领域说明"}});
        QCOMPARE(service.dailySongs()->count(), 1);
        api.advanceSession();
        QCOMPARE(service.dailySongs()->count(), 0);
        QCOMPARE(service.playlists()->count(), 0);
        service.shutdown();
        QSignalSpy drained(&playback, &PlaybackService::closed);
        playback.shutdown();
        QTRY_COMPARE_WITH_TIMEOUT(drained.size(), 1, 15000);
        api.shutdown();
    }
    // FreeListen replaces its list; DiscoverFlow replaces on refresh and appends
    // at the bottom. Failed appends retain rows; empty appends show a notice.
    void freeListenAndDiscoverFlowPages()
    {
        QTemporaryDir dir;
        EchoMusic::SettingsStore prefs(dir.filePath("prefs.ini"));
        prefs.setValue("playback/volume", 0.);
        ApiClient api(DeviceIdentity::temporary());
        AccountService account(api);
        UserPlaylistModel library(api);
        PlaylistMutationService mutations(api);
        PlaybackService playback(api, prefs, nullptr, dir.filePath("playback.sqlite"));
        playback.attachLibrary(library, mutations);
        ThemeStore theme(prefs);
        theme.setMotionEnabled(false);
        ShellSettings shell(prefs);
        shell.acceptUserAgreement(); // These scenarios start after first-run consent.
        RouteController router;
        DiscoveryService service(api, playback, account);
        DiscoveryRouteCoordinator routes(service, router, false);
        // The two pages exist now, so both routes stop being unavailable.
        QVERIFY(router.available("free-listen"));
        QVERIFY(router.available("discover-flow"));

        // FreeListen: one replace, and every distinct quality label is exposed.
        QVERIFY(router.navigate("free-listen"));
        DiscoveryServiceTestAccess::pending(service, DiscoveryService::FreeListen, 201, true);
        DiscoveryServiceTestAccess::complete(
            service, DiscoveryService::FreeListen,
            QJsonObject{{"songs", QJsonArray{domainSong(1), domainSong(2)}},
                        {"card_info", QJsonObject{{"card_title", "免费听"}}}},
            201);
        QCOMPARE(service.state(DiscoveryService::FreeListen), int(DiscoveryService::Ready));
        QCOMPARE(service.freeListen()->count(), 2);
        QVERIFY(service.freeListen()->item(0).contains("qualityTags"));
        QVERIFY(service.freeListen()->item(0).value("qualityTags").toList().contains("SQ"));
        // Size alone does not identify a playable quality resource or yield a label.
        const QJsonObject hashless{
            {"id", "9009"},
            {"hash", QString(32, '9')},
            {"title", "空 hash"},
            {"duration_ms", 1000},
            {"artists", QJsonArray{}},
            {"metadata", QJsonObject{{"resources", QJsonArray{QJsonObject{{"quality", "high"},
                                                                          {"hash", ""}}}}}}};
        service.freeListen()->replaceSongs({hashless});
        QVERIFY(!service.freeListen()->item(0).value("qualityTags").toList().contains("Hi-Res"));
        service.freeListen()->replaceSongs({domainSong(1), domainSong(2)});
        // DiscoverFlow follows the shared discovery source, never its own queue.
        QVERIFY(!service.discoverFlowActive());

        // DiscoverFlow: itemized replace, then two appends.
        QVERIFY(router.navigate("discover-flow"));
        DiscoveryServiceTestAccess::pending(service, DiscoveryService::DiscoverFlow, 301, true);
        DiscoveryServiceTestAccess::complete(
            service, DiscoveryService::DiscoverFlow,
            QJsonObject{
                {"items",
                 QJsonArray{
                     QJsonObject{{"item_id", "a"}, {"alg_path", "alg-a"}, {"song", domainSong(10)}},
                     QJsonObject{
                         {"item_id", "b"}, {"alg_path", "alg-b"}, {"song", domainSong(11)}}}},
                {"unmapped_items", QJsonArray{}}},
            301);
        QCOMPARE(service.state(DiscoveryService::DiscoverFlow), int(DiscoveryService::Ready));
        QCOMPARE(service.discoverFlow()->count(), 2);
        DiscoveryServiceTestAccess::completeAppend(
            service, DiscoveryService::DiscoverFlow,
            QJsonObject{
                {"items", QJsonArray{QJsonObject{
                              {"item_id", "c"}, {"alg_path", "alg-c"}, {"song", domainSong(12)}}}},
                {"unmapped_items", QJsonArray{}}},
            302);
        QCOMPARE(service.discoverFlow()->count(), 3);
        // A batch of already-served rows adds nothing and raises the info notice.
        QSignalSpy notices(&service, &DiscoveryService::notice);
        DiscoveryServiceTestAccess::completeAppend(
            service, DiscoveryService::DiscoverFlow,
            QJsonObject{
                {"items", QJsonArray{QJsonObject{
                              {"item_id", "a"}, {"alg_path", "alg-a"}, {"song", domainSong(10)}}}},
                {"unmapped_items", QJsonArray{}}},
            303);
        QCOMPARE(service.discoverFlow()->count(), 3);
        QCOMPARE(notices.size(), 1);
        // A failed bottom preload keeps the served rows and stays usable.
        DiscoveryServiceTestAccess::failAppend(service, DiscoveryService::DiscoverFlow, 304);
        QCOMPARE(service.state(DiscoveryService::DiscoverFlow), int(DiscoveryService::Ready));
        QCOMPARE(service.discoverFlow()->count(), 3);
        // A successful replacement without items shows the no-recommendations error card.
        DiscoveryServiceTestAccess::complete(
            service, DiscoveryService::DiscoverFlow,
            QJsonObject{{"items", QJsonArray{}}, {"unmapped_items", QJsonArray{}}}, 305);
        QCOMPARE(service.state(DiscoveryService::DiscoverFlow), int(DiscoveryService::Error));
        QCOMPARE(service.error(DiscoveryService::DiscoverFlow), QStringLiteral("暂时没有推荐歌曲"));
        QCOMPARE(service.discoverFlow()->count(), 0);

        // Both pages load through the real RouteHost without QML warnings.
        QQmlEngine engine;
        theme.attachToEngine(engine);
        QStringList warnings;
        connect(&engine, &QQmlEngine::warnings, this, [&](const auto &messages) {
            for (const auto &message : messages)
                warnings.append(message.toString());
        });
        QQmlComponent component(&engine);
        component.setData(R"(
            import QtQuick
            import QtQuick.Window
            import EchoMusic.Shell
            import EchoMusic.Theme
            Window {
                id: testWindow
                required property RouteController router
                required property ShellSettings settings
                visible: true; width: 1150; height: 800
                Rectangle { anchors.fill: parent; color: Theme.main }
                Item {
                    width: parent.width / testWindow.settings.uiScale
                    height: parent.height / testWindow.settings.uiScale
                    scale: testWindow.settings.uiScale; transformOrigin: Item.TopLeft
                    RouteHost { anchors.fill: parent; router: testWindow.router; settings: testWindow.settings }
                }
            })",
                          QUrl("qrc:/free-listen-flow-test.qml"));
        QScopedPointer<QObject> root(component.createWithInitialProperties(
            {{"router", QVariant::fromValue(&router)}, {"settings", QVariant::fromValue(&shell)}}));
        QVERIFY2(root, qPrintable(component.errorString()));
        auto *window = qobject_cast<QQuickWindow *>(root.data());
        QVERIFY(window);
        if (QGuiApplication::platformName() != "offscreen")
            QVERIFY(QTest::qWaitForWindowExposed(window));
        QVERIFY(router.navigate("free-listen"));
        QTRY_VERIFY(findVisual(window->contentItem(), "freeListenPage"));
        auto *freePage = findVisual(window->contentItem(), "freeListenPage");
        auto *countRow = findVisual(freePage, "songCatalogCountRow");
        auto *countBadge = findVisual(freePage, "songCatalogCountBadge");
        auto *underline = findVisual(freePage, "songCatalogTabUnderline");
        QVERIFY(countRow && countBadge);
        QVERIFY(underline && underline->isVisible());
        QCOMPARE(underline->height(), 2.);
        QCOMPARE(countRow->property("text").toString(), QString("歌曲"));
        QVERIFY(countBadge->isVisible());
        QCOMPARE(countBadge->property("count").toInt(), service.freeListen()->totalCount());
        const auto tabArtifacts = qEnvironmentVariable("ECHO_FREE_LISTEN_TAB_ARTIFACTS");
        if (const auto width = qEnvironmentVariableIntValue("ECHO_FREE_LISTEN_TAB_WIDTH");
            width > 0)
            window->setWidth(width);
        if (!tabArtifacts.isEmpty())
            QVERIFY(QDir().mkpath(tabArtifacts));
        for (const auto mode : {ThemeStore::Light, ThemeStore::Dark})
            for (const auto zoom : {1., 1.25}) {
                theme.setMode(mode);
                shell.resetZoom();
                if (zoom > 1.)
                    for (int i = 0; i < 5; ++i)
                        shell.zoomIn();
                QTest::qWait(80);
                // Leaving now destroys the view; each reopen owns new widgets.
                freePage = findVisual(window->contentItem(), "freeListenPage");
                QVERIFY(freePage);
                countRow = findVisual(freePage, "songCatalogCountRow");
                countBadge = findVisual(freePage, "songCatalogCountBadge");
                underline = findVisual(freePage, "songCatalogTabUnderline");
                QVERIFY(countRow && countBadge && underline);
                auto *textBox = countRow->property("textBox").value<QQuickItem *>();
                QVERIFY(textBox);
                QTRY_VERIFY(qAbs(textBox->mapToScene({0, textBox->height() / 2}).y() -
                                 countBadge->mapToScene({0, countBadge->height() / 2}).y()) < zoom);
                const auto image = window->grabWindow();
                QVERIFY(!image.isNull());
                if (QGuiApplication::platformName() != "offscreen") {
                    const auto center = underline->mapToScene({underline->width() / 2, 1});
                    const auto ratio = qreal(image.width()) / window->width();
                    const auto pixel =
                        image.pixelColor(qRound(center.x() * ratio), qRound(center.y() * ratio));
                    QCOMPARE(pixel, theme.accent());
                    const auto titleInk =
                        Test::renderedInkCenter(image, window, textBox, theme.text());
                    const auto badgeInk = Test::renderedInkCenter(image, window, countBadge,
                                                                  theme.text(), {3, 3, 3, 3});
                    QVERIFY(titleInk && badgeInk);
                    QVERIFY(qAbs(*titleInk - *badgeInk) <= zoom);
                }
                if (!tabArtifacts.isEmpty())
                    QVERIFY(image.save(tabArtifacts +
                                       QString("/free-listen-%1-%2.png").arg(int(mode)).arg(zoom)));
                // Use the production header button, then real checkbox/footer
                // input. Both first open and reopen must own a fresh Popup.
                QVERIFY(Test::prepareNativeInput(window));
                const auto popupCount = [&] {
                    QSet<QObject *> visited;
                    int count = 0;
                    const auto visit = [&](auto &&self, QObject *object) -> void {
                        if (!object || visited.contains(object))
                            return;
                        visited.insert(object);
                        count += object->inherits("QQuickPopup");
                        for (auto *child : object->children())
                            self(self, child);
                        if (auto *item = qobject_cast<QQuickItem *>(object))
                            for (auto *child : item->childItems())
                                self(self, child);
                    };
                    visit(visit, root.data());
                    return count;
                };
                QTRY_COMPARE(popupCount(), 0);
                for (int cycle = 0; cycle < 2; ++cycle) {
                    auto *entry = findVisual(freePage, "catalogBatch");
                    QVERIFY(entry && entry->isVisible() && entry->isEnabled());
                    QVERIFY(Test::prepareNativeInput(window));
                    const auto click = [&](QQuickItem *item) {
                        const auto point =
                            item->mapToScene({item->width() / 2, item->height() / 2}).toPoint();
                        Test::moveNativePointer(window, point);
                        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, point);
                    };
                    click(entry);
                    QPointer<QObject> drawer;
                    QTRY_VERIFY((drawer = findVisualOwnerObject(window->contentItem(),
                                                                "discoveryBatchDrawer")));
                    QTRY_VERIFY(drawer->property("opened").toBool());
                    // Native Popup.opened precedes the nested footer Flow's
                    // layout polish after a scale change. Let rendered input
                    // geometry settle before mapping its button coordinates.
                    QTest::qWait(80);
                    QCOMPARE(popupCount(), 1);
                    QCOMPARE(service.freeListen()->selectedCount(), 0);
                    auto *select = findVisual(window->contentItem(), "batchSelectAll");
                    QVERIFY(select);
                    click(select);
                    QTRY_COMPARE(service.freeListen()->selectedCount(), 2);
                    auto *footer = findVisual(window->contentItem(), "batchActions");
                    QVERIFY(footer);
                    QStringList labels;
                    for (auto *item : footer->childItems())
                        if (item->isVisible() && item->property("text").isValid())
                            labels.append(item->property("text").toString());
                    QCOMPARE(labels,
                             (QStringList{QStringLiteral("播放"), QStringLiteral("添加到")}));
                    QSignalSpy addRequested(playback.libraryActions(),
                                            &PlaybackLibraryActions::dialogRequested);
                    auto *add = findVisual(window->contentItem(), "batchAddTo");
                    QVERIFY(add && add->isEnabled());
                    QSignalSpy addClicked(add, SIGNAL(clicked()));
                    click(add);
                    QTRY_COMPARE(addClicked.size(), 1);
                    QTRY_COMPARE(addRequested.size(), 1);
                    if (!tabArtifacts.isEmpty() && cycle == 0)
                        QVERIFY(window->grabWindow().save(
                            tabArtifacts +
                            QString("/free-listen-batch-%1-%2.png").arg(int(mode)).arg(zoom)));
                    if (cycle == 0)
                        QTest::keyClick(window, Qt::Key_Escape);
                    else
                        QVERIFY(router.navigate("discover-flow"));
                    QTRY_VERIFY(drawer.isNull());
                    QTRY_COMPARE(popupCount(), 0);
                }
                QVERIFY(router.navigate("free-listen"));
                QTRY_VERIFY(findVisual(window->contentItem(), "freeListenPage"));
            }
        QVERIFY(router.navigate("discover-flow"));
        QTRY_VERIFY(findVisual(window->contentItem(), "discoverFlowPage"));
        QTRY_VERIFY(findVisual(window->contentItem(), "discoverFlowStage") ||
                    findVisual(window->contentItem(), "discoverFlowError"));
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
        root.reset();
        service.shutdown();
        QSignalSpy drained(&playback, &PlaybackService::closed);
        playback.shutdown();
        QTRY_COMPARE_WITH_TIMEOUT(drained.size(), 1, 15000);
        api.shutdown();
    }
    void queuePersistsCatalogMetadata()
    {
        QTemporaryDir dir;
        const auto path = dir.filePath("playback.sqlite");
        PlaybackStorageWorker worker;
        QSignalSpy failures(&worker, &PlaybackStorageWorker::failed),
            loaded(&worker, &PlaybackStorageWorker::loaded);
        worker.initialize(path);
        const auto track = PlaybackTrack::fromDomain(domainSong(1));
        PlaybackQueueState state{"queue:test", "Catalog", {track}, 0, {}};
        worker.save(42, {state}, state.key);
        worker.record(42, track, 1500);
        worker.load(42, 7);
        QCOMPARE(failures.size(), 0);
        QCOMPARE(loaded.size(), 1);
        const auto states = loaded.first()[2].value<PlaybackQueueStates>();
        QCOMPARE(states.size(), 1);
        QCOMPARE(states[0].tracks.first().catalogMetadata, track.catalogMetadata);
        QCOMPARE(states[0].tracks.first().catalogPrivilege, 10u);
        worker.close();
        PlaybackStorageWorker reopened;
        QSignalSpy restored(&reopened, &PlaybackStorageWorker::loaded);
        reopened.initialize(path);
        reopened.load(42, 8);
        QCOMPARE(restored.size(), 1);
        QCOMPARE(restored.first()[2].value<PlaybackQueueStates>()[0].tracks.first().toDomain(),
                 track.toDomain());
        QCOMPARE(restored.first()[2]
                     .value<PlaybackQueueStates>()[0]
                     .tracks.first()
                     .catalogMetadata["source_file_name"]
                     .toString(),
                 QString("Artist 歌手 - 中文 Song (Live).mp3"));
        reopened.close();
        const auto name = QStringLiteral("discovery-history-check");
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", name);
            db.setDatabaseName(path);
            QVERIFY(db.open());
            QSqlQuery query(db);
            QVERIFY(query.exec("SELECT p.snapshot FROM playback_history h JOIN "
                               "playback_history_payloads p ON p.payload_key=h.payload_key"));
            QVERIFY(query.next());
            QCOMPARE(QJsonDocument::fromJson(query.value(0).toByteArray()).object(),
                     track.toDomain());
            QVERIFY(query.exec("UPDATE playback_tracks SET catalog_metadata='[1]'"));
            db.close();
        }
        QSqlDatabase::removeDatabase(name);
        PlaybackStorageWorker damaged;
        QSignalSpy rejected(&damaged, &PlaybackStorageWorker::failed),
            invalidLoad(&damaged, &PlaybackStorageWorker::loaded);
        damaged.initialize(path);
        damaged.load(42, 9);
        QCOMPARE(rejected.size(), 1);
        QCOMPARE(invalidLoad.size(), 0);
        damaged.close();
    }
    void partialCatalogFactsAndUnknownPrivilegesSurviveQueueAndHistory()
    {
        QTemporaryDir dir;
        const auto path = dir.filePath("partial.sqlite");
        auto song = domainSong(1);
        song["privilege"] = QJsonValue::Null;
        song["album_title"] = QJsonValue::Null;
        song["original_code"] = 3;
        song["duration_ms"] = QJsonValue::Null;
        auto artists = song["artists"].toArray();
        auto artist = artists.first().toObject();
        artist["id"] = QJsonValue::Null;
        artists[0] = artist;
        song["artists"] = artists;
        auto metadata = song["metadata"].toObject();
        metadata["resources"] = QJsonArray{
            QJsonObject{{"quality", "high"}, {"hash", QJsonValue::Null}, {"size_bytes", 12345}}};
        metadata["rights"] = QJsonObject{
            {"preview", QJsonObject{{"start_ms", 81000}, {"end_ms", QJsonValue::Null}}}};
        song["metadata"] = metadata;
        const auto track = PlaybackTrack::fromDomain(song);
        QVERIFY(track.toDomain()["privilege"].isNull());
        QVERIFY(track.toDomain()["duration_ms"].isNull());
        QCOMPARE(track.catalogMetadata["artist_refs"], song["artists"]);
        QVERIFY(track.catalogMetadata["album_title"].isNull());
        SongCatalogModel model;
        model.replaceSongs(QJsonArray{song});
        QCOMPARE(model.data(model.index(0), SongCatalogModel::QualityLabel).toString(), QString());
        PlaybackStorageWorker worker;
        QSignalSpy errors(&worker, &PlaybackStorageWorker::failed);
        worker.initialize(path);
        PlaybackQueueState state{"queue:partial", "Partial", {track}, 0, {}};
        worker.save(42, {state}, state.key);
        worker.record(42, track, 1000);
        worker.close();
        QCOMPARE(errors.size(), 0);
        PlaybackStorageWorker reopened;
        QSignalSpy loaded(&reopened, &PlaybackStorageWorker::loaded);
        reopened.initialize(path);
        reopened.load(42, 9);
        QCOMPARE(loaded.size(), 1);
        const auto restored = loaded.first()[2].value<PlaybackQueueStates>()[0].tracks[0];
        QCOMPARE(restored.toDomain(), track.toDomain());
        QVERIFY(restored.toDomain()["privilege"].isNull());
        QVERIFY(restored.toDomain()["duration_ms"].isNull());
        QVERIFY(restored.catalogMetadata["artist_refs"].toArray()[0].toObject()["id"].isNull());
        QCOMPARE(restored.catalogMetadata["resources"], metadata["resources"]);
        reopened.close();
        const auto connection = QStringLiteral("partial-facts-history-check");
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", connection);
            db.setDatabaseName(path);
            QVERIFY(db.open());
            QSqlQuery query(db);
            QVERIFY(query.exec("SELECT p.snapshot FROM playback_history h JOIN "
                               "playback_history_payloads p ON p.payload_key=h.payload_key"));
            QVERIFY(query.next());
            QCOMPARE(QJsonDocument::fromJson(query.value(0).toByteArray()).object(),
                     track.toDomain());
            db.close();
        }
        QSqlDatabase::removeDatabase(connection);
    }
    void catalogExportsFullMetadataOnlyAtActionBoundary()
    {
        auto song = domainSong(901, "状态保留 Metadata");
        song["duration_ms"] = QJsonValue::Null;
        song["artists"] = QJsonArray{QJsonObject{{"id", "42"}, {"name", "歌手 Artist"}}};
        song["metadata"] = QJsonObject{
            {"resources", QJsonArray{QJsonObject{{"quality", "hi_res"}, {"hash", "lossless"}}}},
            {"cloud_source", QJsonObject{{"song_id", 12345}, {"opaque", "preserve"}}},
            {"originality_code", 1},
            {"unknown", QJsonValue::Null}};
        const auto expected = PlaybackTrack::fromDomain(song);
        SongCatalogModel catalog;
        catalog.replaceSongs({song});
        catalog.toggleSelected(expected.id);
        catalog.setQuery("Metadata");
        catalog.toggleSort(2);
        const auto links = catalog.data(catalog.index(0), SongCatalogModel::ArtistLinks).toList();
        QCOMPARE(links, expected.artistLinks());
        QVERIFY(catalog.data(catalog.index(0), SongCatalogModel::Original).toBool());
        QVERIFY(
            catalog.data(catalog.index(0), SongCatalogModel::DurationText).toString().isEmpty());
        const auto exported = catalog.trackProjection(expected.id);
        QVERIFY(exported);
        QCOMPARE(exported->toDomain(), expected.toDomain());
        QCOMPARE(readCatalogTracks(&catalog).first().toDomain(), expected.toDomain());
        QCOMPARE(readCatalogTracks(&catalog, true).first().toDomain(), expected.toDomain());
        // Exporting does not replace the cached page's sorting/filter/selection.
        QCOMPARE(catalog.query(), QString("Metadata"));
        QCOMPARE(catalog.sortField(), 2);
        QCOMPARE(catalog.selectedCount(), 1);
        catalog.replaceCards(
            {QJsonObject{{"id", 1}, {"creator", "Creator"}, {"duration_ms", 1200}}});
        QVERIFY(catalog.data(catalog.index(0), SongCatalogModel::ArtistLinks).toList().isEmpty());
        QVERIFY(readCatalogTracks(&catalog).isEmpty());
    }
    void incompleteAndRepeatedCatalogIdsKeepEveryRow()
    {
        auto song = domainSong(1);
        auto partial = song;
        partial["id"] = "";
        partial["title"] = "缺失标识，保留标题";
        partial["duration_ms"] = QJsonValue::Null;
        SongCatalogModel model;
        model.replaceSongs(QJsonArray{song, song, partial, partial});
        QCOMPARE(model.count(), 4);
        QSet<QString> keys;
        for (int row = 0; row < model.count(); ++row) {
            const auto key = model.data(model.index(row), SongCatalogModel::ResourceId).toString();
            QVERIFY(!keys.contains(key));
            keys.insert(key);
            const auto track = model.trackProjection(key);
            QVERIFY(track.has_value());
            QCOMPARE(track->id, row < 2 ? song["id"].toString() : QString());
            if (row >= 2) {
                QVERIFY(!track->playable());
                QVERIFY(model.data(model.index(row), SongCatalogModel::DurationText)
                            .toString()
                            .isEmpty());
            }
        }
        model.appendSongs(QJsonArray{song, partial});
        QCOMPARE(model.count(), 6);
    }
    void upgradesOwnVersionOneSchema()
    {
        QTemporaryDir dir;
        const auto path = dir.filePath("playback.sqlite");
        // Reconstruct this application's real v1 schema by removing every
        // later additive column, preserving its original queue/history rows.
        PlaybackStorageWorker seed;
        seed.initialize(path);
        const auto track = PlaybackTrack::fromDomain(domainSong(1));
        PlaybackQueueState state{"queue:test", "Catalog", {track}, 0, {}};
        seed.save(42, {state}, state.key);
        seed.record(42, track, 1500);
        seed.close();
        const auto name = QStringLiteral("discovery-v1-check");
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", name);
            db.setDatabaseName(path);
            QVERIFY(db.open());
            QSqlQuery query(db);
            for (const auto *sql :
                 {"DROP INDEX playback_queue_payload_ref",
                  "DROP INDEX playback_history_payload_ref",
                  "ALTER TABLE playback_tracks DROP COLUMN payload_key",
                  "ALTER TABLE playback_history DROP COLUMN payload_key",
                  "DROP TABLE playback_song_payloads", "DROP TABLE playback_history_payloads",
                  "ALTER TABLE playback_tracks DROP COLUMN catalog_metadata",
                  "ALTER TABLE playback_tracks DROP COLUMN catalog_privilege",
                  "ALTER TABLE playback_history DROP COLUMN track_snapshot",
                  "ALTER TABLE playback_accounts DROP COLUMN last_local_queue",
                  "ALTER TABLE playback_queues DROP COLUMN last_used", "PRAGMA user_version=1"})
                QVERIFY2(query.exec(sql), sql);
            db.close();
        }
        QSqlDatabase::removeDatabase(name);
        PlaybackStorageWorker upgraded;
        QSignalSpy failures(&upgraded, &PlaybackStorageWorker::failed),
            loaded(&upgraded, &PlaybackStorageWorker::loaded);
        upgraded.initialize(path);
        upgraded.load(42, 10);
        QCOMPARE(failures.size(), 0);
        QCOMPARE(loaded.size(), 1);
        const auto oldTrack = loaded.first()[2].value<PlaybackQueueStates>()[0].tracks.first();
        QCOMPARE(oldTrack.title, track.title);
        QVERIFY(oldTrack.catalogMetadata.isEmpty());
        QCOMPARE(oldTrack.catalogPrivilege, 0u);
        upgraded.save(42, {state}, state.key);
        loaded.clear();
        upgraded.load(42, 11);
        QCOMPARE(loaded.first()[2].value<PlaybackQueueStates>()[0].tracks.first().catalogMetadata,
                 track.catalogMetadata);
        upgraded.close();
    }
    void liveDiscoveryPlaybackThroughQtService()
    {
        if (qEnvironmentVariable("ECHO_LIVE_DISCOVERY_PLAYBACK") != "1")
            QSKIP("Opt-in current-account discovery/playback; credentials only in memory");
        const std::string input((std::istreambuf_iterator<char>(std::cin)),
                                std::istreambuf_iterator<char>());
        const auto identity = QJsonDocument::fromJson(QByteArray::fromStdString(input)).object();
        ApiClient api(DeviceIdentity{identity["mid"].toString(), identity["dfid"].toString(),
                                     identity["guid"].toString(), identity["mac"].toString(),
                                     identity["device_name"].toString(), "-", "-"});
        QTemporaryDir dir;
        EchoMusic::SettingsStore prefs(dir.filePath("prefs.ini"));
        prefs.setValue("playback/volume", 0.);
        NetworkPreferences network(prefs, api);
        QVERIFY(network.transportAvailable());
        const auto previous = QNetworkProxy::applicationProxy();
        const auto restore = qScopeGuard([&] { QNetworkProxy::setApplicationProxy(previous); });
        network.installApplicationTransport();
        QVERIFY(api.setAccount(quint64(identity["userid"].toInteger()),
                               identity["token"].toString(), identity["t1"].toString()));
        AccountService account(api);
        PlaybackService playback(api, prefs, nullptr, dir.filePath("playback.sqlite"), &network);
        DiscoveryService service(api, playback, account);
        service.setPage("home", "live-home", true);
        QTRY_VERIFY_WITH_TIMEOUT(
            service.state(DiscoveryService::Style) != DiscoveryService::Loading, 45000);
        QVERIFY2(service.state(DiscoveryService::Style) == DiscoveryService::Ready,
                 qPrintable(service.error(DiscoveryService::Style)));
        const auto tracks = readCatalogTracks(service.styleSongs());
        QVERIFY(!tracks.isEmpty());
        QVERIFY(tracks.first().playable());
        service.play(service.styleSongs(), tracks.first().id, false);
        QTRY_VERIFY_WITH_TIMEOUT(!playback.loading(), 45000);
        QVERIFY2(playback.errorMessage().isEmpty(), "Actual discovery source/decoder failed");
        QTRY_VERIFY_WITH_TIMEOUT(playback.playing() && playback.positionMs() > 200, 15000);
        QCOMPARE(playback.currentTrack().id, tracks.first().id);
        QCOMPARE(playback.currentTrack().catalogMetadata, tracks.first().catalogMetadata);
        playback.pause();
        QTRY_VERIFY(!playback.playing());
        service.setPage("recommend-songs", "live-daily", true);
        QTRY_COMPARE_WITH_TIMEOUT(service.state(DiscoveryService::Daily),
                                  int(DiscoveryService::Ready), 45000);
        QVERIFY(service.dailySongs()->count() > 0);
        service.setPage("ranking", "live-rank", true);
        QTRY_COMPARE_WITH_TIMEOUT(service.state(DiscoveryService::RankSongs),
                                  int(DiscoveryService::Ready), 45000);
        const auto ranks = readCatalogTracks(service.rankingSongs());
        QVERIFY(!ranks.isEmpty());
        service.play(service.rankingSongs(), ranks.first().id, false);
        QTRY_VERIFY_WITH_TIMEOUT(!playback.loading(), 45000);
        QVERIFY2(playback.errorMessage().isEmpty(), "Actual rank source/decoder failed");
        QTRY_VERIFY_WITH_TIMEOUT(playback.playing() && playback.positionMs() > 200, 15000);
        QCOMPARE(playback.currentTrack().catalogMetadata, ranks.first().catalogMetadata);
        QVERIFY(playback.currentTrack().catalogMetadata["ranking"].isObject());
        service.shutdown();
        QSignalSpy drained(&playback, &PlaybackService::closed);
        playback.shutdown();
        QTRY_COMPARE_WITH_TIMEOUT(drained.size(), 1, 15000);
        api.shutdown();
    }

    void recommendedAndCategoryRoutesLoadOnlyTheirCatalogs()
    {
        QTemporaryDir dir;
        SettingsStore prefs(dir.filePath("prefs.ini"));
        ApiClient api(DeviceIdentity::temporary());
        QStringList operations;
        const auto interceptor = api.addInterceptor(1, [&](const QJsonObject &request,
                                                           ApiInterceptNext,
                                                           ApiInterceptDone done) {
            const auto operation = request["operation"].toString();
            operations.append(operation);
            QByteArray payload = "[]";
            if (operation == "top_album")
                payload =
                    R"({"regions":{"chn":[{"id":"A","title":"Album"}],"eur":[{"id":"B","title":"Other"}]}})";
            else if (operation == "playlist_tags")
                payload = R"([{"name":"分类","children":[{"id":"sample","name":"分类一"}]}])";
            else if (operation == "top_playlist" || operation == "top_ip")
                payload = R"([{"id":"P","title":"Playlist"}])";
            QTimer::singleShot(0, &api, [done = std::move(done), payload] { done({0, payload}); });
            return std::function<void()>{};
        });
        AccountService account(api);
        PlaybackService playback(api, prefs, nullptr, dir.filePath("playback.sqlite"));
        RouteController router;
        DiscoveryService discovery(api, playback, account);
        DiscoveryRouteCoordinator routes(discovery, router, true);
        ExploreService service(api, playback, router, true);
        service.setDiscovery(&discovery);
        service.registerRoutes(router);
        QVERIFY(router.navigate("explore"));
        QTRY_COMPARE(service.albums()->count(), 2);
        QTRY_COMPARE(discovery.playlists()->count(), 1);
        QTRY_COMPARE(discovery.editors()->count(), 1);
        QCOMPARE(operations.count("playlist_tags"), 0);
        QCOMPARE(operations.count("top_playlist"), 1);
        QCOMPARE(operations.count("top_album"), 1);
        service.selectRegion("chn");
        QCOMPARE(service.albums()->count(), 1);
        QTest::qWait(20);
        QCOMPARE(operations.count("top_album"), 1);
        QVERIFY(router.navigate("playlist-categories"));
        QTRY_COMPARE(service.category(), QString("sample"));
        QTRY_COMPARE(service.cards()->count(), 1);
        QCOMPARE(operations.count("playlist_tags"), 1);
        QCOMPARE(operations.count("top_playlist"), 2);
        QCOMPARE(operations.count("top_album"), 1);
        router.back();
        QTest::qWait(30);
        QCOMPARE(service.region(), QString("chn"));
        QCOMPARE(operations.count("playlist_tags"), 1);
        QCOMPARE(operations.count("top_playlist"), 2);
        QCOMPARE(operations.count("top_album"), 1);
        discovery.shutdown();
        service.shutdown();
        api.removeInterceptor(interceptor);
        QSignalSpy drain(&playback, &PlaybackService::closed);
        playback.shutdown();
        QTRY_COMPARE(drain.size(), 1);
        api.shutdown();
    }

    void exploreProjectionsTabIdentityAndRetiredRequests()
    {
        QTemporaryDir dir;
        SettingsStore prefs(dir.filePath("prefs.ini"));
        ApiClient api(DeviceIdentity::temporary());
        AccountService account(api);
        PlaybackService playback(api, prefs, nullptr, dir.filePath("playback.sqlite"));
        RouteController router;
        ExploreService service(api, playback, router, false);
        DiscoveryService discovery(api, playback, account);
        service.setDiscovery(&discovery);
        service.registerRoutes(router);
        QVERIFY(router.navigate("explore"));
        QCOMPARE(service.tab(), 0);
        QVERIFY(router.navigate("playlist-categories"));
        QCOMPARE(service.tab(), 1);
        ExploreServiceTestAccess::complete(
            service, ExploreService::Playlists,
            QJsonArray{QJsonObject{{"id", "P"}, {"title", "Playlist"}}});
        ExploreServiceTestAccess::complete(
            service, ExploreService::Albums,
            QJsonObject{
                {"regions",
                 QJsonObject{{"chn", QJsonArray{QJsonObject{{"id", "A"}, {"title", "Album"}}}}}}});
        QCOMPARE(service.cards()->count(), 1);
        QCOMPARE(service.cards()->item(0)["resourceId"].toString(), "P");
        QCOMPARE(service.albums()->count(), 1);
        ExploreServiceTestAccess::pending(service, ExploreService::Playlists, 711);
        QVERIFY(router.navigate("artist-recommendations"));
        QCOMPARE(service.tab(), 4);
        ExploreServiceTestAccess::retired(service, 711, {{"id", "stale"}});
        ExploreServiceTestAccess::complete(
            service, ExploreService::Artists,
            QJsonObject{
                {"groups",
                 QJsonArray{QJsonObject{
                     {"label", "热门"},
                     {"artists", QJsonArray{QJsonObject{{"id", "20"}, {"title", "Artist"}}}}}}}});
        QCOMPARE(service.artists()->count(), 1);
        QCOMPARE(service.artists()->index(0).data(SongCatalogModel::ResourceId).toString(), "20");
        router.back();
        QCOMPARE(service.tab(), 1);
        QCOMPARE(service.cards()->item(0)["resourceId"].toString(), "P");
        api.advanceSession();
        QCOMPARE(service.cards()->count(), 0);
        QCOMPARE(service.albums()->count(), 0);
        service.shutdown();
        discovery.shutdown();
        QSignalSpy drain(&playback, &PlaybackService::closed);
        playback.shutdown();
        QTRY_COMPARE_WITH_TIMEOUT(drain.size(), 1, 15000);
        api.shutdown();
    }

    void playlistCarouselRestoresAndStaysSingleRow()
    {
        if (QGuiApplication::platformName() != "offscreen")
            QSKIP("Nonintrusive carousel regression");
        QTemporaryDir dir;
        SettingsStore prefs(dir.filePath("prefs.ini"));
        ThemeStore theme(prefs);
        theme.setMotionEnabled(false);
        SongCatalogModel cards;
        QJsonArray entries;
        for (int i = 0; i < 1000; ++i)
            entries.append(QJsonObject{{"id", QString::number(i + 1)},
                                       {"title", QString("Playlist %1").arg(i)},
                                       {"cover_url", QString("image://home/item-%1").arg(i)}});
        cards.replaceCards(entries);
        RouteController router;
        EchoPageStateStore state;
        state.save("playlist-carousel-test", {{"index", 600}});
        QQmlEngine engine;
        theme.attachToEngine(engine);
        auto *images = new ArtistImageRequests;
        engine.addImageProvider("home", images);
        QSignalSpy warnings(&engine, &QQmlEngine::warnings);
        QQmlComponent component(&engine);
        component.setData(R"(
import QtQuick
import EchoMusic.Controls
import EchoMusic.Discovery
import EchoMusic.Shell
import EchoMusic.Music
Window {
 id: host; visible: true; width: 1100; height: 500
 required property SongCatalogModel cards
 required property RouteController router
 required property var stateStore
 Item {
  anchors.fill: parent; EchoPageState.scope: host.stateStore
  Flickable {
   id: scroll; anchors.fill: parent; contentHeight: 1000
   PlaylistCollectionSection {
    objectName: "strip"; x:40; width:1000; height:implicitHeight
    stateKey:"test"; heading:"推荐歌单"; collection:host.cards
    router:host.router; scrollView:scroll; contentTop:0
   }
  }
 }
})",
                          QUrl("qrc:/playlist-carousel-test.qml"));
        QScopedPointer<QObject> root(
            component.createWithInitialProperties({{"cards", QVariant::fromValue(&cards)},
                                                   {"router", QVariant::fromValue(&router)},
                                                   {"stateStore", QVariant::fromValue(&state)}}));
        QVERIFY2(root, qPrintable(component.errorString()));
        auto *window = qobject_cast<QQuickWindow *>(root.data());
        QQuickItem *view = nullptr;
        QTRY_VERIFY((view = findVisual(window->contentItem(), "playlistCarousel-test")));
        QTRY_COMPARE(view->property("currentIndex").toInt(), 600);
        QTRY_VERIFY(!images->requested().isEmpty());
        for (const auto &id : images->requested())
            QVERIFY2(id.mid(QString("item-").size()).toInt() >= 598, qPrintable(id));
        auto *strip = findVisual(window->contentItem(), "strip");
        QVERIFY(strip);
        QVERIFY(strip->height() < 400);
        auto *card = findVisual(view, "contentDelegate");
        QVERIFY(card);
        QCOMPARE(card->height(), strip->property("cardWidth").toReal() + 70);
        QVERIFY(QMetaObject::invokeMethod(view, "step", Q_ARG(int, 1)));
        QTRY_COMPARE(state.load("playlist-carousel-test")["index"].toInt(), 601);
        strip->setProperty("loading", true);
        QTest::qWait(20);
        QCOMPARE(view->property("currentIndex").toInt(), 601);
        strip->setProperty("resourceActive", false);
        QTRY_VERIFY(!findVisual(window->contentItem(), "playlistCarousel-test"));
        strip->setProperty("resourceActive", true);
        QTRY_VERIFY((view = findVisual(window->contentItem(), "playlistCarousel-test")));
        QTRY_COMPARE(view->property("currentIndex").toInt(), 601);
        QCOMPARE(warnings.size(), 0);
    }

    void playlistGridStartsAtRestoredViewport()
    {
        if (QGuiApplication::platformName() != "offscreen")
            QSKIP("Nonintrusive restored grid regression");
        QTemporaryDir dir;
        SettingsStore preferences(dir.filePath("prefs.ini"));
        ThemeStore theme(preferences);
        theme.setMotionEnabled(false);
        SongCatalogModel cards;
        QJsonArray entries;
        for (int i = 0; i < 1000; ++i)
            entries.append(QJsonObject{{"id", QString::number(i + 1)},
                                       {"title", QString("Playlist %1").arg(i)},
                                       {"cover_url", QString("image://home/item-%1").arg(i)}});
        cards.replaceCards(entries);
        QQmlEngine engine;
        theme.attachToEngine(engine);
        auto *images = new ArtistImageRequests;
        engine.addImageProvider("home", images);
        QQmlComponent component(&engine);
        component.setData(R"(
import QtQuick
import EchoMusic.Controls
import EchoMusic.Music
Window {
 id: host; visible: true; width: 1200; height: 700
 required property SongCatalogModel source
 property int creations: 0
 property bool resources: true
 property alias regionView: region.view
 Flickable {
  id: outer; objectName: "restoredGridScroll"; anchors.fill: parent; contentWidth: width; contentHeight: region.height
  contentY: 12000
  EchoVirtualGridSection {
   id: region; anchors.left: parent.left; anchors.right: parent.right
   model: host.source; itemCount: host.source.count; columns: 6; cellHeight: 240
   scrollView: outer; contentTop: 0; resourceActive: host.resources
   delegate: ContentCard {
    width: region.cellWidth - 20; height: 220; durationText: ""
    Component.onCompleted: host.creations++
   }
  }
 }
})",
                          QUrl("qrc:/restored-playlist-grid.qml"));
        QScopedPointer<QObject> root(
            component.createWithInitialProperties({{"source", QVariant::fromValue(&cards)}}));
        QVERIFY2(root, qPrintable(component.errorString()));
        QTRY_VERIFY(images->requested().contains("item-300"));
        int minimum = 1000;
        for (const auto &name : images->requested())
            minimum = std::min(minimum, name.mid(5).toInt());
        qInfo() << "restored playlist grid first requested row" << minimum << "creations"
                << root->property("creations").toInt() << "requested images"
                << images->requested().size();
        QVERIFY2(minimum >= 280, "Restored viewport must not decode the catalog beginning");
        QVERIFY(root->property("creations").toInt() <= 90);
        QPointer<QObject> previous = root->property("regionView").value<QObject *>();
        QVERIFY(previous);
        root->setProperty("resources", false);
        QTRY_VERIFY(previous.isNull());
        root->setProperty("resources", true);
        QTRY_VERIFY(root->property("regionView").value<QObject *>());
        auto *scroll = findVisual(qobject_cast<QQuickWindow *>(root.data())->contentItem(),
                                  "restoredGridScroll");
        QVERIFY(scroll);
        scroll->setProperty("contentY", 0);
        QTRY_VERIFY(images->requested().contains("item-0"));
        scroll->setProperty("contentY", 24000);
        QTRY_VERIFY(images->requested().contains("item-600"));
        qInfo() << "playlist grid restore/top/deep creations"
                << root->property("creations").toInt();
        QVERIFY(root->property("creations").toInt() < 220);
    }

    void exploreSpacingAndScrollingHeadersStayBelowPinnedContent()
    {
        QTemporaryDir dir;
        SettingsStore prefs(dir.filePath("prefs.ini"));
        ApiClient api(DeviceIdentity::temporary());
        AccountService account(api);
        PlaybackService playback(api, prefs, nullptr, dir.filePath("playback.sqlite"));
        ThemeStore theme(prefs);
        theme.setMotionEnabled(false);
        ShellSettings settings(prefs);
        settings.acceptUserAgreement();
        RouteController router;
        DiscoveryService discovery(api, playback, account);
        DiscoveryRouteCoordinator routes(discovery, router, false);
        ExploreService service(api, playback, router, false);
        service.setDiscovery(&discovery);
        service.registerRoutes(router);
        QQmlEngine engine;
        theme.attachToEngine(engine);
        QSignalSpy warnings(&engine, &QQmlEngine::warnings);
        QQmlComponent component(&engine);
        component.setData(R"(import QtQuick
import EchoMusic.Shell
import EchoMusic.Theme
Window {id:host;required property RouteController router;required property ShellSettings settings;visible:true;width:1150;height:800;Rectangle{anchors.fill:parent;color:Theme.main}RouteHost{anchors.fill:parent;router:host.router;settings:host.settings}})",
                          QUrl("qrc:/playlist-square-test.qml"));
        QScopedPointer<QObject> root(
            component.createWithInitialProperties({{"router", QVariant::fromValue(&router)},
                                                   {"settings", QVariant::fromValue(&settings)}}));
        QVERIFY2(root, qPrintable(component.errorString()));
        auto *window = qobject_cast<QQuickWindow *>(root.data());
        QVERIFY(window);
        QVERIFY(router.navigate("explore"));
        QJsonArray cards;
        for (int i = 0; i < 60; ++i)
            cards.append(QJsonObject{{"id", QString::number(i + 1)}, {"title", "Playlist"}});
        DiscoveryServiceTestAccess::complete(discovery, DiscoveryService::Playlists, cards);
        DiscoveryServiceTestAccess::complete(discovery, DiscoveryService::Editors, cards);
        ExploreServiceTestAccess::complete(service, ExploreService::Albums,
                                           QJsonObject{{"regions", QJsonObject{{"chn", cards}}}});
        QTRY_VERIFY(findVisual(window->contentItem(), "playlistSquareView"));
        QVERIFY(!findVisual(window->contentItem(), "playlistCategoryPickerButton"));
        auto *entry = findVisual(window->contentItem(), "playlistCategoriesEntry");
        QVERIFY(entry);
        QVERIFY(Test::prepareNativeInput(window));
        QTest::mouseClick(
            window, Qt::LeftButton, Qt::NoModifier,
            entry->mapToScene(QPointF(entry->width() / 2, entry->height() / 2)).toPoint());
        QTRY_COMPARE(router.current()->name(), QString("playlist-categories"));
        QTRY_VERIFY(findVisual(window->contentItem(), "playlistCategoriesPage"));
        QVERIFY(!findVisual(window->contentItem(), "playlistSquareView"));
        ExploreServiceTestAccess::complete(service, ExploreService::Playlists, cards);
        ExploreServiceTestAccess::complete(
            service, ExploreService::Categories,
            QJsonArray{QJsonObject{
                {"name", "分类"},
                {"children", QJsonArray{QJsonObject{{"id", "sample-a"}, {"name", "分类一"}},
                                        QJsonObject{{"id", "sample-b"}, {"name", "分类二"}}}}}});
        auto *categoryButton = findVisual(window->contentItem(), "playlistCategoryPickerButton");
        QVERIFY(categoryButton);
        QVERIFY(Test::prepareNativeInput(window));
        Test::FrameSpy pickerFrame(window);
        QTest::mouseClick(
            window, Qt::LeftButton, Qt::NoModifier,
            categoryButton
                ->mapToScene(QPointF(categoryButton->width() / 2, categoryButton->height() / 2))
                .toPoint());
        QPointer<QObject> picker;
        QTRY_VERIFY(
            (picker = findVisualOwnerObject(window->contentItem(), "playlistCategoryPicker")));
        QTRY_VERIFY(picker->property("opened").toBool());
        QTRY_VERIFY(!pickerFrame.isEmpty());
        QVERIFY(!picker->property("multiple").toBool());
        auto *choice = findVisual(window->contentItem(), "option-sample-b");
        QVERIFY(choice);
        QTest::mouseClick(
            window, Qt::LeftButton, Qt::NoModifier,
            choice->mapToScene(QPointF(choice->width() / 2, choice->height() / 2)).toPoint());
        QTRY_COMPARE(service.category(), QString("sample-b"));
        QTRY_VERIFY(picker.isNull());
        QVERIFY(router.navigate("artist-recommendations"));
        QTRY_VERIFY(findVisual(window->contentItem(), "artistRecommendationsPage"));
        QVERIFY(!findVisual(window->contentItem(), "playlistSquareView"));
        QVERIFY2(warnings.isEmpty(),
                 qPrintable(warnings.isEmpty() ? QString{}
                                               : warnings.first().toList().first().toString()));
        root.reset();
        discovery.shutdown();
        service.shutdown();
        QSignalSpy drain(&playback, &PlaybackService::closed);
        playback.shutdown();
        QTRY_COMPARE_WITH_TIMEOUT(drain.size(), 1, 15000);
        api.shutdown();
    }

    void nativeRecommendationPages()
    {
        if (!qEnvironmentVariableIsSet("ECHO_RECOMMENDATION_LIVE_INPUT"))
            QSKIP("Opt-in current account read-only recommendation/UI capture");
        const std::string input((std::istreambuf_iterator<char>(std::cin)),
                                std::istreambuf_iterator<char>());
        const auto identity = QJsonDocument::fromJson(QByteArray::fromStdString(input)).object();
        ApiClient api(DeviceIdentity{identity["mid"].toString(), identity["dfid"].toString(),
                                     identity["guid"].toString(), identity["mac"].toString(),
                                     identity["device_name"].toString(), "-", "-"});
        QTemporaryDir dir;
        SettingsStore prefs(dir.filePath("prefs.ini"));
        prefs.setValue("playback/volume", 0.);
        NetworkPreferences network(prefs, api);
        QVERIFY(network.transportAvailable());
        const auto previous = QNetworkProxy::applicationProxy();
        const auto restore = qScopeGuard([&] { QNetworkProxy::setApplicationProxy(previous); });
        network.installApplicationTransport();
        QVERIFY(api.setAccount(quint64(identity["userid"].toInteger()),
                               identity["token"].toString(), identity["t1"].toString()));
        AccountService account(api);
        PlaybackService playback(api, prefs, nullptr, dir.filePath("playback.sqlite"), &network);
        SessionDatabase cache(dir.filePath("session"));
        SongMetadataService metadata(api);
        metadata.enableSqlIndex(cache);
        playback.attachMetadata(metadata);
        ThemeStore theme(prefs);
        ShellSettings settings(prefs);
        settings.acceptUserAgreement();
        RouteController router;
        DiscoveryService discovery(api, playback, account);
        discovery.setPreferences(&prefs);
        DiscoveryRouteCoordinator routes(discovery, router, true);
        ExploreService explore(api, playback, router, true);
        explore.setDiscovery(&discovery);
        explore.registerRoutes(router);
        QQmlEngine engine;
        theme.attachToEngine(engine);
        QStringList warnings;
        QObject observer;
        connect(&engine, &QQmlEngine::warnings, &observer, [&](const auto &messages) {
            for (const auto &m : messages)
                warnings.append(m.toString());
        });
        QQmlComponent component(&engine);
        component.setData(R"(import QtQuick
import QtQuick.Controls.Basic
import EchoMusic.Shell
import EchoMusic.Theme
ApplicationWindow { id:host;required property RouteController router;required property ShellSettings settings;visible:true;width:1150;height:800;color:Theme.main
 Item {width:parent.width/host.settings.uiScale;height:parent.height/host.settings.uiScale;scale:host.settings.uiScale;transformOrigin:Item.TopLeft;RouteHost{anchors.fill:parent;router:host.router;settings:host.settings}}
})",
                          QUrl("qrc:/live-recommendation-test.qml"));
        QScopedPointer<QObject> root(
            component.createWithInitialProperties({{"router", QVariant::fromValue(&router)},
                                                   {"settings", QVariant::fromValue(&settings)}}));
        QVERIFY2(root, qPrintable(component.errorString()));
        auto *window = qobject_cast<QQuickWindow *>(root.data());
        QVERIFY(window);
        QVERIFY(Test::prepareNativeInput(window));
        const auto artifacts = qEnvironmentVariable("ECHO_RECOMMENDATION_ARTIFACTS");
        if (!artifacts.isEmpty())
            QDir().mkpath(artifacts);
        const auto capture = [&](const QString &file, const QString &pageName) {
            QVERIFY(Test::prepareNativeInput(window));
            QQuickItem *visiblePage = nullptr;
            QTRY_VERIFY((visiblePage = findVisual(window->contentItem(), pageName)));
            const auto presented = [&] {
                for (auto *item = visiblePage; item; item = item->parentItem())
                    if (!item->isVisible() || item->opacity() < .999)
                        return false;
                return true;
            };
            QTRY_VERIFY(presented());
            Test::FrameSpy frames(window);
            window->update();
            QTRY_VERIFY(!frames.isEmpty());
            const auto image = window->grabWindow();
            QVERIFY(!image.isNull());
            QSet<QRgb> colors;
            for (int y = 0; y < image.height(); y += 16)
                for (int x = 0; x < image.width(); x += 16)
                    colors.insert(image.pixel(x, y));
            QVERIFY(colors.size() > 8);
            if (!artifacts.isEmpty())
                image.save(artifacts + "/" + file);
        };
        for (bool dark : {false, true})
            for (int zoom : {100, 125}) {
                theme.setMode(dark ? ThemeStore::Dark : ThemeStore::Light);
                settings.resetZoom();
                if (zoom == 125)
                    for (int i = 0; i < 5; ++i)
                        settings.zoomIn();
                QVERIFY(router.navigate("home"));
                QTRY_VERIFY(findVisual(window->contentItem(), "recommendationCardsView"));
                auto *view = findVisual(window->contentItem(), "recommendationCardsView");
                view->setProperty("contentY", -108.);
                QTRY_COMPARE_WITH_TIMEOUT(discovery.recommendationCards()->find("daily")->status(),
                                          int(DiscoveryService::Ready), 45000);
                QTRY_COMPARE_WITH_TIMEOUT(discovery.recommendationCards()->find("style")->status(),
                                          int(DiscoveryService::Ready), 45000);
                for (int i = 0; i < 2; ++i) {
                    auto *card = discovery.recommendationCards()->at(i);
                    QTRY_COMPARE_WITH_TIMEOUT(card->status(), int(DiscoveryService::Ready), 45000);
                    for (int row = 0; row < std::min(6, card->collection()->count()); ++row)
                        QTRY_VERIFY(
                            !card->collection()
                                 ->data(card->collection()->index(row), SongCatalogModel::Title)
                                 .toString()
                                 .isEmpty());
                }
                QTRY_COMPARE(previewPresentationError(window->contentItem()), QString());
                capture(QString("/songs-%1-%2.png").arg(dark ? "dark" : "light").arg(zoom).mid(1),
                        "homePage");
                for (int row = 2; row < discovery.recommendationCards()->count(); ++row) {
                    QVERIFY(QMetaObject::invokeMethod(view, "positionViewAtIndex", Q_ARG(int, row),
                                                      Q_ARG(int, 0)));
                    {
                        auto *card = discovery.recommendationCards()->at(row);
                        QTRY_VERIFY_WITH_TIMEOUT(card->status() >= 2, 45000);
                        QVERIFY2(card->status() == 2,
                                 qPrintable(card->title() + ": " + card->errorMessage()));
                    }
                }
                capture(
                    QString("/songs-tail-%1-%2.png").arg(dark ? "dark" : "light").arg(zoom).mid(1),
                    "homePage");
                QVERIFY(router.navigate("recommendation-detail", {}, "3001"));
                QTRY_VERIFY(findVisual(window->contentItem(), "freeListenPage"));
                QVERIFY(discovery.detailCard()->collection()->count() > 0);
                capture(QString("/detail-%1-%2.png").arg(dark ? "dark" : "light").arg(zoom).mid(1),
                        "freeListenPage");
                QVERIFY(router.navigate("explore"));
                QTRY_COMPARE_WITH_TIMEOUT(discovery.state(DiscoveryService::Playlists),
                                          int(DiscoveryService::Ready), 45000);
                QTRY_COMPARE_WITH_TIMEOUT(discovery.state(DiscoveryService::Editors),
                                          int(DiscoveryService::Ready), 45000);
                QTRY_VERIFY_WITH_TIMEOUT(!explore.loading(), 45000);
                QTRY_VERIFY(findVisual(window->contentItem(), "playlistSquareView"));
                capture(
                    QString("/playlists-%1-%2.png").arg(dark ? "dark" : "light").arg(zoom).mid(1),
                    "explorePage");
                QVERIFY(router.navigate("ranking"));
                QTRY_COMPARE_WITH_TIMEOUT(discovery.state(DiscoveryService::RankSongs),
                                          int(DiscoveryService::Ready), 45000);
                QVERIFY(router.navigate("artist-recommendations"));
                QTRY_VERIFY_WITH_TIMEOUT(!explore.loading(), 45000);
                QVERIFY(explore.cards()->count() > 0);
                capture(QString("/artists-%1-%2.png").arg(dark ? "dark" : "light").arg(zoom).mid(1),
                        "artistRecommendationsPage");
            }
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
        root.reset();
        discovery.shutdown();
        explore.shutdown();
        QSignalSpy drain(&playback, &PlaybackService::closed);
        playback.shutdown();
        QTRY_COMPARE_WITH_TIMEOUT(drain.size(), 1, 15000);
        QSignalSpy metadataDrained(&metadata, &SongMetadataService::closed);
        metadata.shutdown();
        QTRY_COMPARE_WITH_TIMEOUT(metadataDrained.size(), 1, 15000);
        QSignalSpy cacheDrained(&cache, &SessionDatabase::closed);
        cache.shutdown();
        QTRY_COMPARE_WITH_TIMEOUT(cacheDrained.size(), 1, 15000);
        api.shutdown();
    }
    void themedDateInkCentersInNativePixels()
    {
        QTemporaryDir dir;
        EchoMusic::SettingsStore prefs(dir.filePath("prefs.ini"));
        ThemeStore theme(prefs);
        theme.setAccent(QColor("#c95532"));
        QQmlEngine engine;
        theme.attachToEngine(engine);
        QQmlComponent component(&engine);
        component.setData(R"(
import QtQuick
import QtQuick.Window
import EchoMusic.Theme
import EchoMusic.Discovery
Window {
 id: window
 visible: true; width: 440; height: 300
 property real zoom: 1
 property real coverScale: 1
 property string day: "19"
 Rectangle { anchors.fill: parent; color: Theme.main }
 Item {
  x: 20; y: 20; scale: window.zoom; transformOrigin: Item.TopLeft
  DiscoveryThemedCover {
   objectName: "dateCover"; width: 150; height: 150; day: window.day
   scale: window.coverScale; transformOrigin: Item.TopLeft
  }
 }
})",
                          QUrl("qrc:/date-cover-native.qml"));
        QScopedPointer<QObject> root(component.create());
        QVERIFY2(root, qPrintable(component.errorString()));
        auto *window = qobject_cast<QQuickWindow *>(root.data());
        QVERIFY(window && QTest::qWaitForWindowExposed(window));
        auto *cover = findVisual(window->contentItem(), "dateCover");
        QVERIFY(cover);
        const auto artifacts = qEnvironmentVariable("ECHO_DISCOVERY_ARTIFACTS");
        if (!artifacts.isEmpty())
            QDir().mkpath(artifacts);
        for (const auto mode : {ThemeStore::Light, ThemeStore::Dark}) {
            theme.setMode(mode);
            for (const double zoom : {1., 1.25})
                for (const double size : {150., 32.})
                    for (const auto *day : {"1", "19", "31"}) {
                        root->setProperty("zoom", zoom);
                        root->setProperty("coverScale", size / 150.);
                        root->setProperty("day", day);
                        QTRY_COMPARE(cover->property("status").toInt(), 1);
                        QTest::qWait(50);
                        const auto image = window->grabWindow();
                        QVERIFY(!image.isNull());
                        const double ratio = double(image.width()) / window->width();
                        const auto scene = cover->mapRectToScene(cover->boundingRect());
                        const QRectF pixels(scene.topLeft() * ratio, scene.size() * ratio);
                        const auto ink = Test::renderedInkBounds(
                            image,
                            pixels
                                .adjusted(pixels.width() * .2, pixels.height() * .2,
                                          -pixels.width() * .2, -pixels.height() * .2)
                                .toAlignedRect(),
                            Qt::white);
                        QVERIFY(ink);
                        const QPointF center(ink->x() + ink->width() / 2.,
                                             ink->y() + ink->height() / 2.);
                        QVERIFY2(
                            qAbs(center.x() - pixels.center().x()) <= 1. &&
                                qAbs(center.y() - pixels.center().y()) <= 1.,
                            qPrintable(QString("day %1 size %2 zoom %3 ink %4,%5 expected %6,%7")
                                           .arg(day)
                                           .arg(size)
                                           .arg(zoom)
                                           .arg(center.x())
                                           .arg(center.y())
                                           .arg(pixels.center().x())
                                           .arg(pixels.center().y())));
                        if (!artifacts.isEmpty() && QString(day) == "19")
                            QVERIFY(image.save(
                                artifacts + QString("/date-%1-%2-%3.png")
                                                .arg(mode == ThemeStore::Light ? "light" : "dark")
                                                .arg(zoom)
                                                .arg(size)));
                    }
        }
    }
    void detailHeadersDistributeEveryMetadataRow()
    {
        QTemporaryDir dir;
        EchoMusic::SettingsStore prefs(dir.filePath("prefs.ini"));
        prefs.setValue("playback/volume", 0.);
        ApiClient api(DeviceIdentity::temporary());
        AccountService account(api);
        PlaybackService playback(api, prefs, nullptr, dir.filePath("playback.sqlite"));
        ThemeStore theme(prefs);
        ShellSettings shell(prefs);
        shell.acceptUserAgreement(); // These scenarios start after first-run consent.
        RouteController router;
        DiscoveryService service(api, playback, account);
        DiscoveryRouteCoordinator routes(service, router, false);
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
            Window {
                id: testWindow
                required property RouteController router
                required property ShellSettings settings
                visible: true; width: 1150; height: 800
                Rectangle { anchors.fill: parent; color: Theme.main }
                Item {
                    width: parent.width / testWindow.settings.uiScale
                    height: parent.height / testWindow.settings.uiScale
                    scale: testWindow.settings.uiScale
                    transformOrigin: Item.TopLeft
                    RouteHost { anchors.fill: parent; router: testWindow.router; settings: testWindow.settings }
                }
            })",
                          QUrl("qrc:/discovery-detail-spacing-test.qml"));
        QScopedPointer<QObject> root(component.createWithInitialProperties(
            {{"router", QVariant::fromValue(&router)}, {"settings", QVariant::fromValue(&shell)}}));
        QVERIFY2(root, qPrintable(component.errorString()));
        auto *window = qobject_cast<QQuickWindow *>(root.data());
        QVERIFY(window);
        const auto artifacts = qEnvironmentVariable("ECHO_DISCOVERY_ARTIFACTS");
        if (!artifacts.isEmpty())
            QVERIFY(QDir().mkpath(artifacts));
        for (bool dark : {false, true})
            for (double scale : {1., 1.25})
                // Production minimum window 1100 minus the expanded 230px
                // sidebar leaves 870px for this routed page.
                for (int width : {1150, 870})
                    for (const QString &route :
                         {QStringLiteral("ranking"), QStringLiteral("recommend-songs")}) {
                        theme.setMode(dark ? ThemeStore::Dark : ThemeStore::Light);
                        shell.resetZoom();
                        if (scale > 1.)
                            for (int i = 0; i < 5; ++i)
                                shell.zoomIn();
                        window->resize(width, 800);
                        QVERIFY(router.navigate(route));
                        const auto pageName =
                            route == "ranking" ? "rankingPage" : "dailyRecommendPage";
                        QTRY_VERIFY(findVisual(window->contentItem(), pageName));
                        auto *page = findVisual(window->contentItem(), pageName);
                        for (bool optional : {true, false}) {
                            const bool intro = route == "ranking" && optional;
                            DiscoveryServiceTestAccess::seedDetailPresentation(service, intro,
                                                                               optional);
                            QTRY_VERIFY(findVisual(page, "songDetailHeader"));
                            auto *list = findVisual(page, "discoverySongs");
                            QVERIFY(list);
                            list->setProperty("contentY", -list->property("topMargin").toReal());
                            QTest::qWait(60);
                            auto *header = findVisual(page, "songDetailHeader");
                            QTRY_COMPARE(header->property("progress").toReal(), 0.);
                            QList<QQuickItem *> rows;
                            for (const auto *name :
                                 {"songDetailTitleRow", "songDetailPrimaryDetails",
                                  "songDetailSecondaryDetails", "songDetailDescription",
                                  "songDetailActions"}) {
                                auto *item = findVisual(header, name);
                                QVERIFY(item);
                                if (item->isVisible())
                                    rows.append(item);
                            }
                            QCOMPARE(rows.size(), route == "ranking" && !intro ? 3 : 4);
                            for (auto *row : rows) {
                                const auto rect = row->mapRectToItem(header, row->boundingRect());
                                QVERIFY2(rect.right() <= header->width() - 23,
                                         qPrintable(QString("%1 %2 right %3 header %4")
                                                        .arg(route, row->objectName())
                                                        .arg(rect.right())
                                                        .arg(header->width())));
                            }
                            QList<qreal> gaps;
                            for (int i = 1; i < rows.size(); ++i) {
                                const auto before =
                                    rows[i - 1]->mapRectToItem(header, rows[i - 1]->boundingRect());
                                const auto after =
                                    rows[i]->mapRectToItem(header, rows[i]->boundingRect());
                                gaps.append(after.top() - before.bottom());
                            }
                            for (auto gap : gaps)
                                QVERIFY2(gap >= -1 && qAbs(gap - gaps.first()) <= 1.01,
                                         qPrintable(QString("%1 width%2 scale%3 gaps %4 / %5")
                                                        .arg(route)
                                                        .arg(width)
                                                        .arg(scale)
                                                        .arg(gaps.first())
                                                        .arg(gap)));
                            auto *cover = findVisual(header, "songDetailCoverSlot");
                            QVERIFY(cover);
                            const auto bottom = [](QQuickItem *item, QQuickItem *target) {
                                return item->mapRectToItem(target, item->boundingRect()).bottom();
                            };
                            QVERIFY(qAbs(bottom(rows.last(), header) - bottom(cover, header)) <= 1);
                            QCOMPARE(countVisual(header, "discoveryDetailMetadataField"),
                                     optional ? (intro ? 3 : 1) : 0);
                            const auto image = window->grabWindow();
                            QVERIFY(!image.isNull());
                            if (!artifacts.isEmpty())
                                QVERIFY(image.save(artifacts +
                                                   QString("/%1-%2-%3-w%4-%5.png")
                                                       .arg(route)
                                                       .arg(dark ? "dark" : "light")
                                                       .arg(scale)
                                                       .arg(width)
                                                       .arg(optional ? "full" : "minimal")));
                            // The same rows collapse without leaving metadata
                            // interactive or displacing the 32px title/cover.
                            list->setProperty("contentY", 500);
                            QTRY_VERIFY(!rows[1]->isVisible());
                            auto *title = rows.first();
                            QTRY_VERIFY(qAbs(title->height() - 32) < .01);
                            QVERIFY(qAbs(title->mapToItem(header, QPointF{}).y() -
                                         cover->mapToItem(header, QPointF{}).y()) < .01);
                            list->setProperty("contentY", -list->property("topMargin").toReal());
                            QTRY_COMPARE(header->property("progress").toReal(), 0.);
                        }
                    }
        QVERIFY2(errors.isEmpty(), qPrintable(errors.join('\n')));
        service.shutdown();
        root.reset();
        QSignalSpy drained(&playback, &PlaybackService::closed);
        playback.shutdown();
        QTRY_COMPARE_WITH_TIMEOUT(drained.size(), 1, 15000);
        api.shutdown();
    }
};
QTEST_MAIN_WRAPPER(DiscoveryTest, QTEST_MAIN_SETUP())
#include "DiscoveryTest.moc"
