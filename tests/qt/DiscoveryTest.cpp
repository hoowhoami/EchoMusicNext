#include "PlaybackRepository_p.h"
#include "SongCatalogModel.h"
#include "ArtistDirectorySliceModel.h"
#include "DiscoveryService.h"
#include "ExploreService.h"
#include "DiscoveryRouteCoordinator.h"
#include "PlaybackService.h"
#include "PlaybackRepository.h"
#include "AccountService.h"
#include "RouteController.h"
#include "ShellSettings.h"
#include "RenderedInk.h"
#include "ThemeStore.h"
#include "NetworkPreferences.h"
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
#include <QSettings>
#include <QStandardItemModel>
#include <QSignalSpy>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest>
using namespace EchoMusic;
namespace EchoMusic
{
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
class DiscoveryServiceTestAccess
{
public:
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
        QSettings prefs(dir.filePath("prefs.ini"), QSettings::IniFormat);
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
 visible: true; width: 850; height: 600
 Rectangle { anchors.fill: parent; color: Theme.main }
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
            // A uniform source isolates the media from cover content. The
            // artwork renders at full strength (no top entrance, no scrim);
            // probe the stage gap, away from all foreground glyphs.
            if (QGuiApplication::platformName() != "offscreen") {
                const auto ratio = image.width() / qreal(window->width());
                const auto x = (page->width() - (narrow ? 80 : 100)) * zoom;
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
                    const auto pixel = image.pixelColor(qRound(x * ratio), py);
                    for (int channel = 0; channel < 3; ++channel) {
                        const auto c = [channel](const QColor &color) {
                            return channel == 0   ? color.red()
                                   : channel == 1 ? color.green()
                                                  : color.blue();
                        };
                        const auto expected = c(coverColor);
                        QVERIFY2(
                            qAbs(c(pixel) - expected) <= 3,
                            qPrintable(QString("source alpha y=%1 channel=%2 actual=%3 expected=%4")
                                           .arg(logicalY)
                                           .arg(channel)
                                           .arg(c(pixel))
                                           .arg(expected)));
                    }
                }
                // The media must leave the shell's rounded bottom corners
                // clear. Check actual pixels at each scale and theme,
                // including the inside of the arc, rather than just a radius
                // property.
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
            QTRY_VERIFY_WITH_TIMEOUT(!info->property("running").toBool(), 2000);
            QVERIFY(!backdrop->property("running").toBool());
            QCOMPARE(playback.queue()->rowCount(), 0); // Presentation is not playback intent.
            if (QGuiApplication::platformName() != "offscreen") {
                const auto frame = window->grabWindow();
                const auto dpr = frame.width() / qreal(window->width());
                const qreal x = page->width() - 100, y = page->height() * .3;
                // The media has no fades; compare the 35% vertical source crop
                // directly against the rendered pixel.
                const auto crop = page->height() / page->width();
                const auto uv = (1 - crop) * .35 + (y / page->height()) * crop;
                const auto expected = qRound(uv * 255);
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
            QVERIFY(router.navigate("free-listen"));
            QTRY_VERIFY(retired.isNull());
            router.back();
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
                QSignalSpy frames(window, &QQuickWindow::frameSwapped);
                QTest::qWait(350);
                // A returned native window may still present one queued
                // finalization frame. It must not become a persistent clock.
                const auto finalizationFrames = frames.size();
                frames.clear();
                QTest::qWait(350);
                qInfo() << "FLOW return finalization_frames=" << finalizationFrames
                        << "following_idle_350ms_frames=" << frames.size();
                QVERIFY(finalizationFrames <= 1);
                QCOMPARE(frames.size(), 0);
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
        QSettings prefs(dir.filePath("prefs.ini"), QSettings::IniFormat);
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
            QSignalSpy frames(window, &QQuickWindow::frameSwapped);
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
        QSignalSpy acceptedFrame(window, &QQuickWindow::frameSwapped);
        window->update();
        QTRY_VERIFY(!acceptedFrame.isEmpty());
        QTest::mouseClick(
            window, Qt::LeftButton, Qt::NoModifier,
            accept->mapToScene({accept->width() / 2, accept->height() / 2}).toPoint());
        QTRY_VERIFY(shell.userAgreementAccepted());
        QTRY_VERIFY(agreement.isNull());
        QCOMPARE(nativePopups(), 0);
        QSettings reread(dir.filePath("prefs.ini"), QSettings::IniFormat);
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
        QSettings prefs(dir.filePath("prefs.ini"), QSettings::IniFormat);
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
    }
    void homeScrollGeometryRemainsStable()
    {
        QFETCH(bool, dark);
        QFETCH(int, zoomSteps);
        QFETCH(int, windowWidth);
        QTemporaryDir dir;
        QSettings prefs(dir.filePath("prefs.ini"), QSettings::IniFormat);
        prefs.setValue("playback/volume", 0.);
        ApiClient api(DeviceIdentity::temporary());
        AccountService account(api);
        PlaybackService playback(api, prefs, nullptr, dir.filePath("playback.sqlite"));
        ThemeStore theme(prefs);
        ShellSettings shell(prefs);
        shell.acceptUserAgreement(); // These scenarios start after first-run consent.
        theme.setMode(dark ? ThemeStore::Dark : ThemeStore::Light);
        for (int i = 0; i < zoomSteps; ++i)
            shell.zoomIn();
        RouteController router;
        DiscoveryService service(api, playback, account);
        DiscoveryRouteCoordinator routes(service, router, false);
        QQmlEngine engine;
        theme.attachToEngine(engine);
        engine.addImageProvider("home", new ArtistImageRequests);
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
                required property int testWidth
                visible: true; width: testWidth; height: 800
                Rectangle { anchors.fill: parent; color: Theme.main }
                Item {
                    width: parent.width / testWindow.settings.uiScale
                    height: parent.height / testWindow.settings.uiScale
                    scale: testWindow.settings.uiScale; transformOrigin: Item.TopLeft
                    RouteHost { anchors.fill: parent; router: testWindow.router; settings: testWindow.settings }
                }
            })",
                          QUrl("qrc:/home-scroll-geometry-test.qml"));
        QScopedPointer<QObject> root(
            component.createWithInitialProperties({{"router", QVariant::fromValue(&router)},
                                                   {"settings", QVariant::fromValue(&shell)},
                                                   {"testWidth", windowWidth}}));
        QVERIFY2(root, qPrintable(component.errorString()));
        auto *window = qobject_cast<QQuickWindow *>(root.data());
        QVERIFY(window);
        if (QGuiApplication::platformName() != "offscreen")
            QVERIFY(QTest::qWaitForWindowExposed(window));
        QVERIFY(router.navigate("home"));
        QJsonArray songs, cards;
        const QJsonArray groups{
            QJsonObject{
                {"name", "场景"},
                {"child", QJsonArray{QJsonObject{{"id", "S1"}, {"name", "学习"}, {"default", 1}},
                                     QJsonObject{{"id", "S2"}, {"name", "工作"}}}}},
            QJsonObject{{"name", "风格"}, {"child", QJsonArray{}}},
            QJsonObject{{"name", "心情"}, {"child", QJsonArray{}}}};
        for (int i = 1; i <= 60; ++i) {
            const auto cover = QString("image://home/%1").arg(i);
            if (i <= 18) {
                auto song = domainSong(i);
                song["cover_url"] = cover;
                songs.append(song);
            }
            cards.append(QJsonObject{{"id", QString::number(i)},
                                     {"title", "歌单 Playlist " + QString::number(i)},
                                     {"cover_url", cover}});
        }
        DiscoveryServiceTestAccess::complete(service, DiscoveryService::Style,
                                             QJsonObject{{"songs", songs}, {"groups", groups}});
        DiscoveryServiceTestAccess::complete(service, DiscoveryService::Playlists, cards);
        DiscoveryServiceTestAccess::complete(service, DiscoveryService::Editors, cards);
        QTRY_VERIFY(findVisual(window->contentItem(), "homeView"));
        auto *view = findVisual(window->contentItem(), "homeView");
        QTest::qWait(150);
        const auto artifacts = qEnvironmentVariable("ECHO_HOME_SCROLL_ARTIFACTS");
        if (!artifacts.isEmpty())
            QVERIFY(QDir().mkpath(artifacts));
        const auto verifyReadySection = [&]() {
            auto *section = findVisual(window->contentItem(), "homePlaylistSection");
            auto *grid = findVisual(window->contentItem(), "homePlaylistGrid");
            QVERIFY(section && grid);
            QCOMPARE(section->height(), grid->y() + grid->height());
            auto *editors = findVisual(window->contentItem(), "homeEditorSection");
            QCOMPARE(editors->y(), section->y() + section->height());
        };
        verifyReadySection();
        auto *homeRoute = router.current();
        QVERIFY(router.navigate("ranking"));
        QTest::qWait(40);
        for (const auto *name :
             {"homeStyleEmptyLoader", "homePlaylistEmptyLoader", "homeEditorEmptyLoader"}) {
            auto *loader = findVisual(window->contentItem(), name);
            QVERIFY(loader);
            QVERIFY(!loader->property("active").toBool());
            QVERIFY(!loader->property("item").value<QObject *>());
        }
        router.back();
        QCOMPARE(router.current(), homeRoute);
        QTest::qWait(40);
        verifyReadySection();
        for (int count : {8, 2, 0, 5, 6, 10, 60}) {
            QJsonArray sample;
            for (int i = 0; i < count; ++i)
                sample.append(cards[i]);
            DiscoveryServiceTestAccess::complete(service, DiscoveryService::Playlists, sample);
            QTest::qWait(40);
            auto *section = findVisual(window->contentItem(), "homePlaylistSection");
            auto *grid = findVisual(window->contentItem(), "homePlaylistGrid");
            auto *editors = findVisual(window->contentItem(), "homeEditorSection");
            qInfo() << "Home changed count" << count << "section" << section->height() << "grid"
                    << grid->y() << grid->height() << grid->property("itemCount") << "editor"
                    << editors->y();
            if (count > 0)
                verifyReadySection();
            else {
                auto *loader = findVisual(window->contentItem(), "homePlaylistEmptyLoader");
                QVERIFY(loader && loader->property("active").toBool());
                QVERIFY(loader->property("item").value<QObject *>());
                QCOMPARE(section->height(), loader->y() + loader->height());
            }
            if (count == 5 && !artifacts.isEmpty()) {
                view->setProperty("contentY",
                                  grid->property("contentTop").toReal() + grid->height() - 180);
                QSignalSpy frames(window, &QQuickWindow::frameSwapped);
                window->update();
                QTRY_VERIFY(!frames.isEmpty());
                QVERIFY(window->grabWindow().save(artifacts + "/home-boundary-" +
                                                  QString::fromLatin1(QTest::currentDataTag()) +
                                                  ".png"));
                view->setProperty("contentY", 0.);
            }
        }
        const qreal initialHeight = view->property("contentHeight").toReal();
        qreal minHeight = initialHeight, maxHeight = initialHeight;
        int checkedCells = 0, maxCards = 0;
        const auto verifyViewport = [&]() {
            const qreal offset = view->property("contentY").toReal();
            const qreal extent = view->property("contentHeight").toReal();
            minHeight = std::min(minHeight, extent);
            maxHeight = std::max(maxHeight, extent);
            for (const auto *name : {"homeStyleGrid", "homePlaylistGrid", "homeEditorGrid"}) {
                auto *section = findVisual(window->contentItem(), name);
                QVERIFY(section);
                const qreal top = section->property("contentTop").toReal();
                const qreal firstY = std::max(0., offset - top);
                const qreal lastY = std::min(section->height(), offset + view->height() - top);
                if (lastY <= firstY)
                    continue;
                auto *grid = section->property("view").value<QQuickItem *>();
                QVERIFY2(grid, name);
                QVERIFY(grid->height() <= view->height());
                QVERIFY(!grid->property("interactive").toBool());
                QCOMPARE(grid->property("contentY").toReal(), grid->parentItem()->y());
                const int columns = section->property("columns").toInt();
                const qreal rowHeight = section->property("cellHeight").toReal();
                const int count = section->property("itemCount").toInt();
                auto *content = grid->property("contentItem").value<QQuickItem *>();
                QVERIFY(content);
                const int allocationBudget =
                    (int(std::ceil(grid->height() / rowHeight)) + 6) * columns * 2;
                QVERIFY(countVisual(grid, "contentDelegate") + countVisual(grid, "homeSongCard") <=
                        allocationBudget);
                // Check every intersecting native cell, including section edges.
                for (int row = int(firstY / rowHeight); row < std::ceil(lastY / rowHeight); ++row)
                    for (int col = 0; col < columns && row * columns + col < count; ++col) {
                        const int index = row * columns + col;
                        QQuickItem *cell = nullptr;
                        QVERIFY(QMetaObject::invokeMethod(grid, "itemAtIndex",
                                                          Q_RETURN_ARG(QQuickItem *, cell),
                                                          Q_ARG(int, index)));
                        QVERIFY2(cell, qPrintable(QString("%1: missing visible cell %2 at %3")
                                                      .arg(name)
                                                      .arg(index)
                                                      .arg(offset)));
                        QCOMPARE(cell->y(), row * rowHeight);
                        auto *card = cell;
                        if (card->objectName() == "contentDelegate")
                            QCOMPARE(card->property("resourceId").toString(),
                                     QString::number(index + 1));
                        ++checkedCells;
                    }
            }
            maxCards = std::max(maxCards, countVisual(window->contentItem(), "contentDelegate") +
                                              countVisual(window->contentItem(), "homeSongCard"));
        };
        // Reproduce fast scrolling and scrollbar jumps independently of input.
        const qreal lastOffset = initialHeight - view->height();
        for (int step = 0; step < 110; ++step) {
            view->setProperty("contentY", lastOffset * (step < 55 ? step : 109 - step) / 54.);
            QTest::qWait(16);
            verifyViewport();
        }
        for (qreal offset : {lastOffset, 0., lastOffset * .53, lastOffset * .19}) {
            view->setProperty("contentY", offset);
            QTest::qWait(20);
            verifyViewport();
            if (!artifacts.isEmpty())
                QVERIFY(window->grabWindow().save(
                    artifacts + QString("/%1-%2.png")
                                    .arg(QString::fromLatin1(QTest::currentDataTag()))
                                    .arg(qRound(offset))));
        }
        // Native pixel-delta phases exercise the real Flickable input timeline.
        ulong timestamp = 10000;
        const auto wheel = [&](QPoint pixel, Qt::ScrollPhase phase) {
            const auto point = view->mapToScene(QPointF(view->width() / 2, view->height() / 2));
            QWheelEvent event(point, window->mapToGlobal(point.toPoint()), pixel,
                              pixel.isNull() ? QPoint{} : pixel * 2, Qt::NoButton, Qt::NoModifier,
                              phase, false, Qt::MouseEventSynthesizedBySystem);
            event.setTimestamp(timestamp += 16);
            QCoreApplication::sendEvent(window, &event);
        };
        view->setProperty("contentY", 1000.);
        QTest::qWait(20);
        const qreal beforeWheel = view->property("contentY").toReal();
        QSignalSpy movement(view, SIGNAL(movementStarted()));
        wheel({}, Qt::ScrollBegin);
        for (int step = 0; step < 45; ++step) {
            wheel(QPoint(0, -75), Qt::ScrollUpdate);
            QTest::qWait(16);
            verifyViewport();
        }
        wheel({}, Qt::ScrollEnd);
        QVERIFY(movement.count() > 0);
        QVERIFY(view->property("contentY").toReal() > beforeWheel + 2000);
        QMetaObject::invokeMethod(view, "cancelFlick");
        view->setProperty("contentY", 1000.);
        const auto mousePoint = view->mapToScene(QPointF(view->width() / 2, view->height() / 2));
        QWheelEvent mouseWheel(mousePoint, window->mapToGlobal(mousePoint.toPoint()), {},
                               QPoint(0, -120), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase,
                               false);
        mouseWheel.setTimestamp(timestamp += 20);
        QCoreApplication::sendEvent(window, &mouseWheel);
        QTRY_VERIFY(view->property("contentY").toReal() > 1000.);
        verifyViewport();
        qInfo() << "Home geometry extent" << minHeight << maxHeight << "checked visible cells"
                << checkedCells << "maximum cards" << maxCards;
        QVERIFY2(maxHeight - minHeight < 1,
                 "Scrolling changed the height of unchanged home content");
        QVERIFY(maxCards < 160);
        // Isolate late data completion from the preceding wheel gesture.
        // cancelFlick alone does not wait for the shared wheel animation.
        QMetaObject::invokeMethod(view, "cancelFlick");
        const QQmlProperty wheelRunning(view, "EchoWheelScroll.running", qmlContext(view));
        QVERIFY(wheelRunning.isValid());
        QTRY_VERIFY(!wheelRunning.read().toBool());
        QTRY_VERIFY(!view->property("moving").toBool());
        view->setProperty("contentY", 1500.);
        QTest::qWait(30);
        const auto position = view->property("contentY").toReal();
        auto updatedCards = cards;
        updatedCards.append(QJsonObject{{"id", "61"}, {"title", "晚到的歌单"}});
        DiscoveryServiceTestAccess::complete(service, DiscoveryService::Editors, updatedCards);
        QTest::qWait(50);
        QCOMPARE(view->property("contentY").toReal(), position);
        QJsonArray large;
        for (int i = 1; i <= 10000; ++i)
            large.append(domainSong(i));
        service.styleSongs()->replaceSongs(large);
        view->setProperty("contentY", 0.);
        QTest::qWait(50);
        minHeight = maxHeight = view->property("contentHeight").toReal();
        const auto largeEnd = maxHeight - view->height();
        for (qreal offset : {largeEnd * .1, largeEnd * .9, largeEnd, 0.}) {
            view->setProperty("contentY", offset);
            QTest::qWait(25);
            verifyViewport();
        }
        QCOMPARE(minHeight, maxHeight);
        qInfo() << "Home10000 maximum cards" << maxCards;
        QVERIFY(maxCards < 160);
        // Each region must preserve loading/error/retry even with stale data.
        quint64 request = 80000;
        for (auto part :
             {DiscoveryService::Style, DiscoveryService::Playlists, DiscoveryService::Editors}) {
            const auto *name = part == DiscoveryService::Style       ? "homeStyleGrid"
                               : part == DiscoveryService::Playlists ? "homePlaylistGrid"
                                                                     : "homeEditorGrid";
            auto *section = findVisual(window->contentItem(), name);
            QVERIFY(section);
            view->setProperty(
                "contentY",
                std::min(section->property("contentTop").toReal(),
                         std::max(0., view->property("contentHeight").toReal() - view->height())));
            QTest::qWait(25);
            DiscoveryServiceTestAccess::pending(service, part, ++request, true);
            QTest::qWait(25);
            DiscoveryServiceTestAccess::complete(
                service, part,
                part == DiscoveryService::Style
                    ? QJsonValue(QJsonObject{{"songs", songs}, {"groups", groups}})
                    : QJsonValue(cards),
                request);
            QTest::qWait(25);
            // Several filter/response transitions can land before the next
            // frame. Only the final model/delegate pair may materialize.
            for (int burst = 0; burst < 3; ++burst) {
                DiscoveryServiceTestAccess::pending(service, part, ++request, true);
                DiscoveryServiceTestAccess::complete(
                    service, part,
                    part == DiscoveryService::Style
                        ? QJsonValue(QJsonObject{{"songs", songs}, {"groups", groups}})
                        : QJsonValue(cards),
                    request);
            }
            QTest::qWait(25);
            DiscoveryServiceTestAccess::fail(service, part, ++request);
            QTRY_VERIFY(!section->isVisible());
            QTRY_VERIFY(!section->property("view").value<QQuickItem *>());
            DiscoveryServiceTestAccess::pending(service, part, ++request, true);
            QTRY_VERIFY(section->isVisible());
            QTest::qWait(20);
            const auto target =
                std::min(section->property("contentTop").toReal(),
                         std::max(0., view->property("contentHeight").toReal() - view->height()));
            view->setProperty("contentY", target);
            QTRY_VERIFY(section->property("view").value<QQuickItem *>());
            auto *grid = section->property("view").value<QQuickItem *>();
            QTRY_COMPARE(grid->property("count").toInt(), section->property("columns").toInt() * 2);
            DiscoveryServiceTestAccess::complete(
                service, part,
                part == DiscoveryService::Style
                    ? QJsonValue(QJsonObject{{"songs", songs}, {"groups", groups}})
                    : QJsonValue(cards),
                request);
            QTRY_COMPARE(section->property("itemCount").toInt(),
                         part == DiscoveryService::Style ? 18 : 60);
            QTest::qWait(20);
        }
        QVERIFY(router.navigate("ranking"));
        QTRY_COMPARE(countVisual(window->contentItem(), "homeSongCard"), 0);
        QTRY_COMPARE(countVisual(window->contentItem(), "contentDelegate"), 0);
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
        service.shutdown();
        root.reset();
        QSignalSpy drained(&playback, &PlaybackService::closed);
        playback.shutdown();
        QTRY_COMPARE_WITH_TIMEOUT(drained.size(), 1, 15000);
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
    void artistDirectoryLoadsOnlyTheFeaturedAvatars()
    {
        QTemporaryDir dir;
        QSettings prefs(dir.filePath("prefs.ini"), QSettings::IniFormat);
        ThemeStore theme(prefs);
        theme.setMotionEnabled(false);
        ShellSettings settings(prefs);
        SongCatalogModel collection;
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
        collection.replaceCards(cards);
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
                required property SongCatalogModel collection
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
        QScopedPointer<QObject> root(
            component.createWithInitialProperties({{"collection", QVariant::fromValue(&collection)},
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
                    QSignalSpy opened(view, SIGNAL(openRequested(QString)));
                    firstName->forceActiveFocus(Qt::TabFocusReason);
                    QTest::keyClick(window, Qt::Key_Space);
                    QCOMPARE(opened.size(), 1);
                    QCOMPARE(opened.first().first().toString(),
                             firstName->property("resourceId").toString());
                    QVERIFY(firstName->property("resourceId").toString().toInt() >= 21);
                    peakNames = qMax(peakNames, countVisual(view, "artistNameEntry"));
                    QVERIFY(peakNames > 0 && peakNames < 150);
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
        QCOMPARE(collection.count(), 1000);
        collection.replaceCards({QJsonObject{{"id", "new"}, {"title", "筛选后的歌手"}}});
        QTRY_COMPARE(view->property("featuredCount").toInt(), 1);
        QTRY_COMPARE(view->property("count").toInt(), 0);
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
        QCOMPARE(model.tracks(true).size(), 3);
        model.toggleSelected("1");
        QCOMPARE(model.selectedCount(), 2);
        QCOMPARE(model.tracks(true).first().id, QString("2"));
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
        const auto track = model.tracks().first();
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
        QSettings prefs(dir.filePath("prefs.ini"), QSettings::IniFormat);
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
        QSettings prefs(dir.filePath("prefs.ini"), QSettings::IniFormat);
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
        QSettings prefs(dir.filePath("prefs.ini"), QSettings::IniFormat);
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
            QVERIFY(query.exec("SELECT track_snapshot FROM playback_history"));
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
            QVERIFY(query.exec("SELECT track_snapshot FROM playback_history"));
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
        const auto exported = catalog.track(expected.id);
        QVERIFY(exported);
        QCOMPARE(exported->toDomain(), expected.toDomain());
        QCOMPARE(catalog.tracks().first().toDomain(), expected.toDomain());
        QCOMPARE(catalog.tracks(true).first().toDomain(), expected.toDomain());
        // Exporting does not replace the cached page's sorting/filter/selection.
        QCOMPARE(catalog.query(), QString("Metadata"));
        QCOMPARE(catalog.sortField(), 2);
        QCOMPARE(catalog.selectedCount(), 1);
        catalog.replaceCards(
            {QJsonObject{{"id", 1}, {"creator", "Creator"}, {"duration_ms", 1200}}});
        QVERIFY(catalog.data(catalog.index(0), SongCatalogModel::ArtistLinks).toList().isEmpty());
        QVERIFY(catalog.tracks().isEmpty());
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
            const auto track = model.track(key);
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
                 {"ALTER TABLE playback_tracks DROP COLUMN catalog_metadata",
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
        QSettings prefs(dir.filePath("prefs.ini"), QSettings::IniFormat);
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
        const auto tracks = service.styleSongs()->tracks();
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
        const auto ranks = service.rankingSongs()->tracks();
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
    void exploreProjectionsTabIdentityAndRetiredRequests()
    {
        QTemporaryDir dir;
        QSettings prefs(dir.filePath("prefs.ini"), QSettings::IniFormat);
        prefs.setValue("playback/volume", 0.);
        ApiClient api(DeviceIdentity::temporary());
        PlaybackService playback(api, prefs, nullptr, dir.filePath("playback.sqlite"));
        RouteController router;
        ExploreService service(api, playback, router, false);
        service.registerRoutes(router);
        QVERIFY(router.navigate("explore"));
        const auto instance = router.current()->instanceId();
        ExploreServiceTestAccess::complete(
            service, ExploreService::Categories,
            QJsonArray{QJsonObject{
                {"id", "1"},
                {"name", "场景"},
                {"children", QJsonArray{QJsonObject{{"id", "587"}, {"name", "学习"}}}}}});
        QCOMPARE(service.category(), QString("587"));
        QCOMPARE(service.categoryLabel(), QString("场景 - 学习"));
        ExploreServiceTestAccess::pending(service, ExploreService::Playlists, 711);
        service.selectTab(2);
        QCOMPARE(router.current()->instanceId(), instance);
        QCOMPARE(service.tab(), 2);
        ExploreServiceTestAccess::retired(service, 711, {{"id", "stale"}});
        QCOMPARE(service.cards()->count(), 0);
        ExploreServiceTestAccess::complete(
            service, ExploreService::Albums,
            QJsonObject{
                {"regions",
                 QJsonObject{{"chn", QJsonArray{QJsonObject{{"id", "12"},
                                                            {"title", "中文 Album"},
                                                            {"creator", "Artist"},
                                                            {"published_at", "2026-10-16"},
                                                            {"song_count", 10}}}},
                             {"eur", QJsonArray{QJsonObject{{"id", "13"}, {"title", "Other"}}}}}}});
        QCOMPARE(service.cards()->count(), 2);
        QCOMPARE(service.cards()->item(0)["extraText"].toString(), QString("Artist • 2026-10-16"));
        service.selectRegion("eur");
        QCOMPARE(service.cards()->count(), 1);
        service.selectTab(4);
        ExploreServiceTestAccess::complete(
            service, ExploreService::Artists,
            QJsonObject{
                {"groups",
                 QJsonArray{
                     QJsonObject{{"label", "热门"},
                                 {"artists", QJsonArray{QJsonObject{
                                                 {"id", "20"},
                                                 {"title", "歌手"},
                                                 {"metadata", QJsonObject{{"song_count", 3},
                                                                          {"album_count", 2}}}}}}},
                     QJsonObject{
                         {"label", "A"},
                         {"artists", QJsonArray{QJsonObject{
                                         {"id", "21"},
                                         {"title", "Artist"},
                                         {"metadata", QJsonObject{{"fans_count", 15000}}}}}}}}},
                {"sex_filters", QJsonArray{QJsonObject{{"id", "0"}, {"label", "全部"}}}},
                {"type_filters", QJsonArray{QJsonObject{{"id", "0:0"}, {"label", "全部"}}}}});
        QCOMPARE(service.artistLetter(), QString("热门"));
        QCOMPARE(service.cards()->count(), 1);
        QCOMPARE(service.cards()->item(0)["extraText"].toString(), QString("3 歌曲 • 2 专辑"));
        service.selectLetter("A");
        QCOMPARE(service.cards()->item(0)["extraText"].toString(), QString("1.5万 粉丝"));
        service.selectLetter("全部");
        QCOMPARE(service.cards()->count(), 2);
        service.selectTab(3);
        ExploreServiceTestAccess::complete(service, ExploreService::NewSongs,
                                           QJsonObject{{"songs", QJsonArray{domainSong(51)}}});
        QCOMPARE(service.newSongs()->count(), 1);
        ExploreServiceTestAccess::pending(service, ExploreService::NewSongs, 712);
        api.advanceSession();
        ExploreServiceTestAccess::retired(service, 712, {{"songs", QJsonArray{domainSong(99)}}});
        QCOMPARE(service.newSongs()->count(), 0);
        QCOMPARE(service.cards()->count(), 0);
        service.shutdown();
        QSignalSpy drained(&playback, &PlaybackService::closed);
        playback.shutdown();
        QTRY_COMPARE_WITH_TIMEOUT(drained.size(), 1, 15000);
        api.shutdown();
    }
    void exploreSpacingAndScrollingHeadersStayBelowPinnedContent()
    {
        QTemporaryDir dir;
        QSettings prefs(dir.filePath("prefs.ini"), QSettings::IniFormat);
        prefs.setValue("playback/volume", 0.);
        ApiClient api(DeviceIdentity::temporary());
        PlaybackService playback(api, prefs, nullptr, dir.filePath("playback.sqlite"));
        ThemeStore theme(prefs);
        theme.setMotionEnabled(false);
        ShellSettings shell(prefs);
        shell.acceptUserAgreement(); // These scenarios start after first-run consent.
        RouteController router;
        ExploreService service(api, playback, router, false);
        service.registerRoutes(router);
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
                    scale: testWindow.settings.uiScale; transformOrigin: Item.TopLeft
                    RouteHost { anchors.fill: parent; router: testWindow.router; settings: testWindow.settings }
                }
            })",
                          QUrl("qrc:/explore-spacing-clip-test.qml"));
        QScopedPointer<QObject> root(component.createWithInitialProperties(
            {{"router", QVariant::fromValue(&router)}, {"settings", QVariant::fromValue(&shell)}}));
        QVERIFY2(root, qPrintable(component.errorString()));
        auto *window = qobject_cast<QQuickWindow *>(root.data());
        QVERIFY(window);
        QVERIFY(router.navigate("explore"));
        QJsonArray cards, songs;
        for (int i = 1; i <= 1000; ++i) {
            cards.append(QJsonObject{{"id", QString::number(i)},
                                     {"title", "卡片 Card " + QString::number(i)},
                                     {"cover_url", ""},
                                     {"creator", "Artist 歌手"}});
            songs.append(domainSong(i));
        }
        // Typed domain presentation inputs; no invented upstream wire sample.
        ExploreServiceTestAccess::complete(
            service, ExploreService::Categories,
            QJsonArray{QJsonObject{
                {"id", "1"},
                {"name", "分类"},
                {"children", QJsonArray{QJsonObject{{"id", "587"}, {"name", "学习"}}}}}});
        ExploreServiceTestAccess::complete(service, ExploreService::Playlists, cards);
        ExploreServiceTestAccess::complete(service, ExploreService::Albums,
                                           QJsonObject{{"regions", QJsonObject{{"chn", cards}}}});
        ExploreServiceTestAccess::complete(
            service, ExploreService::Artists,
            QJsonObject{
                {"groups", QJsonArray{QJsonObject{{"label", "热门"}, {"artists", cards}}}},
                {"sex_filters", QJsonArray{QJsonObject{{"id", "0"}, {"label", "全部"}}}},
                {"type_filters", QJsonArray{QJsonObject{{"id", "0:0"}, {"label", "全部"}}}}});
        ExploreServiceTestAccess::complete(
            service, ExploreService::RankCatalog,
            QJsonArray{QJsonObject{{"id", 8888}, {"title", "TOP500"}}});
        ExploreServiceTestAccess::complete(service, ExploreService::RankSongs,
                                           QJsonObject{{"songs", songs}});
        ExploreServiceTestAccess::complete(service, ExploreService::NewSongs,
                                           QJsonObject{{"songs", songs}});
        const auto artifacts = qEnvironmentVariable("ECHO_EXPLORE_ARTIFACTS");
        if (!artifacts.isEmpty())
            QVERIFY(QDir().mkpath(artifacts));
        QTRY_VERIFY(findVisual(window->contentItem(), "explorePage"));
        auto *page = findVisual(window->contentItem(), "explorePage");
        QStringList emptyStateLeaks;
        for (bool dark : {false, true})
            for (double scale : {1., 1.25}) {
                theme.setMode(dark ? ThemeStore::Dark : ThemeStore::Light);
                shell.resetZoom();
                if (scale > 1.)
                    for (int i = 0; i < 5; ++i)
                        shell.zoomIn();
                for (int tab = 0; tab < 5; ++tab) {
                    service.selectTab(tab);
                    QTest::qWait(80);
                    const bool song = tab == 1 || tab == 3;
                    auto *scope = song ? findVisual(page, "songCatalogView")
                                       : findVisual(page, "exploreCardsViewport");
                    QVERIFY(scope);
                    auto *view = findVisual(scope, song       ? "discoverySongs"
                                                   : tab == 4 ? "exploreArtistDirectory"
                                                              : "exploreCards");
                    QVERIFY(view);
                    const auto *empty =
                        findVisual(view, song ? "songCatalogEmptyState" : "exploreEmptyState");
                    if (empty && empty->isVisible())
                        emptyStateLeaks.append(
                            QString("tab %1: populated footer visible at height %2")
                                .arg(tab)
                                .arg(empty->height()));
                    view->setProperty("contentY", view->property("originY").toReal() -
                                                      view->property("topMargin").toReal());
                    QTest::qWait(30);
                    auto *filterScope = song ? scope : findVisual(scope, "exploreCardFilters");
                    QVERIFY(filterScope);
                    auto *filters = findVisual(filterScope, "exploreFilterRow");
                    auto *control = findVisual(filterScope, song       ? "explorePlayAll"
                                                            : tab == 4 ? "exploreSexSelector"
                                                                       : "exploreCategorySelector");
                    QVERIFY(filters && control && control->isVisible());
                    const auto controlBox =
                        control->mapRectToItem(filters, control->boundingRect());
                    QCOMPARE(filters->height(), 60.);
                    QCOMPARE(control->height(), 36.);
                    QVERIFY(qAbs(controlBox.top() - 12) < .01);
                    QVERIFY(qAbs(filters->height() - controlBox.bottom() - 12) < .01);
                    auto *navigationScope = song ? scope : findVisual(scope, "exploreCardsHeader");
                    QVERIFY(navigationScope);
                    auto *tabs = findVisual(navigationScope, "exploreNavigationTabs");
                    QVERIFY(tabs);
                    const auto tabGap = control->mapToItem(page, QPointF{}).y() -
                                        tabs->mapToItem(page, QPointF(0, tabs->height())).y();
                    QVERIFY2(qAbs(tabGap - 12) < .01, qPrintable(QString::number(tabGap)));
                    if (song) {
                        auto *search = findVisual(scope, "discoverySongFilter");
                        auto *row = findVisual(scope, "songCatalogToolbarRow");
                        QVERIFY(search && row);
                        QCOMPARE(row->height(), 46.);
                        const auto gap =
                            search->mapToItem(page, QPointF{}).y() -
                            control->mapToItem(page, QPointF(0, control->height())).y();
                        QVERIFY2(qAbs(gap - 12) < .01, qPrintable(QString::number(gap)));
                    } else {
                        auto *header = findVisual(scope, "exploreCardsHeader");
                        auto *flowHeader = findVisual(scope, "exploreCardFilters");
                        QVERIFY(header && flowHeader);
                        if (tab == 4) {
                            auto *letters = findVisual(view, "exploreArtistLetters");
                            QVERIFY(letters && letters->isVisible());
                            const auto gap =
                                letters->mapToItem(page, QPointF{}).y() -
                                control->mapToItem(page, QPointF(0, control->height())).y();
                            QVERIFY(qAbs(gap - 12) < .01);
                        }
                        for (int offset : {16, 48, 96, 128, 240, 900}) {
                            QVERIFY(QMetaObject::invokeMethod(scope, "scrollTo",
                                                              Q_ARG(double, double(offset))));
                            QTest::qWait(30);
                            const auto frame = window->grabWindow();
                            QVERIFY(!frame.isNull());
                            auto *flowClip = findVisual(scope, "pageFlowHeaderClip");
                            QVERIFY(flowClip && flowClip->clip());
                            QCOMPARE(flowClip->y(), 96.);
                            if (QGuiApplication::platformName() != "offscreen") {
                                // Compare actual native/RHI pixels, keeping
                                // inline-header origin and extent unchanged.
                                // Offscreen software's fractional tab edge
                                // changes when sibling paint is suppressed;
                                // its geometry checks are not pixel acceptance.
                                flowHeader->setOpacity(0);
                                view->setOpacity(0);
                                QTest::qWait(20);
                                const auto clean = window->grabWindow();
                                const auto rect = header->mapRectToScene(header->boundingRect());
                                const auto ratio = frame.width() / qreal(window->width());
                                // Scrollbar paint lies outside the header's 40px inset.
                                const int inset = qCeil(40 * scale * ratio);
                                const QRect region(inset, 0, frame.width() - inset * 2,
                                                   qFloor(rect.bottom() * ratio));
                                QCOMPARE(frame.copy(region), clean.copy(region));
                                flowHeader->setOpacity(1);
                                view->setOpacity(1);
                            }
                            if (!artifacts.isEmpty())
                                QVERIFY(frame.save(artifacts + QString("/tab%1-%2-%3-scroll%4.png")
                                                                   .arg(tab)
                                                                   .arg(dark ? "dark" : "light")
                                                                   .arg(scale)
                                                                   .arg(offset)));
                        }
                        const auto delegates = tab == 4 ? countVisual(view, "artistAvatarCard") +
                                                              countVisual(view, "artistNameEntry")
                                                        : countVisual(view, "contentDelegate");
                        QVERIFY(delegates > 0 && delegates < 140);
                    }
                }
            }
        QVERIFY2(emptyStateLeaks.isEmpty(), qPrintable(emptyStateLeaks.join('\n')));
        // Exercise the actual page footer across model resets, including a
        // featured-only artist result whose name-slice count is zero.
        for (int tab : {0, 2, 4, 1, 3}) {
            service.selectTab(tab);
            const bool song = tab == 1 || tab == 3;
            auto *collection =
                song ? (tab == 1 ? service.rankSongs() : service.newSongs()) : service.cards();
            collection->clear();
            const auto name = song ? "songCatalogEmptyState" : "exploreEmptyState";
            QTRY_VERIFY(findVisual(page, song       ? "discoverySongs"
                                         : tab == 4 ? "exploreArtistDirectory"
                                                    : "exploreCards"));
            auto *stateView = findVisual(page, song       ? "discoverySongs"
                                               : tab == 4 ? "exploreArtistDirectory"
                                                          : "exploreCards");
            QTRY_VERIFY(findVisual(stateView, name));
            QTRY_VERIFY(findVisual(stateView, name)->isVisible());
            QVERIFY(findVisual(stateView, name)->height() > 0);
            if (song)
                collection->replaceSongs({domainSong(1)});
            else
                collection->replaceCards({cards.first()});
            QTRY_VERIFY(!findVisual(stateView, name) || !findVisual(stateView, name)->isVisible());
            if (song) {
                collection->setQuery("No matching song 不存在");
                QTRY_VERIFY(findVisual(stateView, name)->isVisible());
                collection->setQuery({});
                QTRY_VERIFY(!findVisual(stateView, name)->isVisible());
                auto *catalog = findVisual(page, "songCatalogView");
                QVERIFY(catalog);
                catalog->setProperty("errorMessage", "加载失败 Retry");
                QTRY_VERIFY(findVisual(stateView, name)->isVisible());
                catalog->setProperty("loading", true);
                QTRY_VERIFY(!findVisual(stateView, name)->isVisible());
                catalog->setProperty("loading", false);
                QTRY_VERIFY(findVisual(stateView, name)->isVisible());
                catalog->setProperty("errorMessage", "");
                QTRY_VERIFY(!findVisual(stateView, name)->isVisible());
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
    void nativeExplorePage()
    {
        if (!qEnvironmentVariableIsSet("ECHO_EXPLORE_LIVE_INPUT"))
            QSKIP("Opt-in current-account read-only Explore wire/UI check");
        const std::string input((std::istreambuf_iterator<char>(std::cin)),
                                std::istreambuf_iterator<char>());
        const auto identity = QJsonDocument::fromJson(QByteArray::fromStdString(input)).object();
        ApiClient api(DeviceIdentity{identity["mid"].toString(), identity["dfid"].toString(),
                                     identity["guid"].toString(), identity["mac"].toString(),
                                     identity["device_name"].toString(), "-", "-"});
        QTemporaryDir dir;
        QSettings prefs(dir.filePath("prefs.ini"), QSettings::IniFormat);
        prefs.setValue("playback/volume", 0.);
        NetworkPreferences network(prefs, api);
        QVERIFY(network.transportAvailable());
        const auto previous = QNetworkProxy::applicationProxy();
        const auto restore = qScopeGuard([&] { QNetworkProxy::setApplicationProxy(previous); });
        network.installApplicationTransport();
        QVERIFY(api.setAccount(quint64(identity["userid"].toInteger()),
                               identity["token"].toString(), identity["t1"].toString()));
        PlaybackService playback(api, prefs, nullptr, dir.filePath("playback.sqlite"), &network);
        ThemeStore theme(prefs);
        ShellSettings shell(prefs);
        shell.acceptUserAgreement(); // These scenarios start after first-run consent.
        RouteController router;
        ExploreService service(api, playback, router, true);
        service.registerRoutes(router);
        QQmlEngine engine;
        theme.attachToEngine(engine);
        QStringList errors;
        connect(&engine, &QQmlEngine::warnings, this, [&](const auto &warnings) {
            for (const auto &w : warnings)
                errors.append(w.toString());
        });
        QQmlComponent component(&engine);
        component.setData(R"(
            import QtQuick
            import QtQuick.Window
            import EchoMusic.Shell
            import EchoMusic.Theme
            import EchoMusic.Discovery
            Window {
                id: testWindow
                required property RouteController router
                required property ShellSettings settings
                visible: true; width: 1150; height: 800
                Rectangle { anchors.fill: parent; color: Theme.main }
                Item {width: parent.width/settings.uiScale; height: parent.height/settings.uiScale; scale: settings.uiScale; transformOrigin: Item.TopLeft
                    RouteHost { anchors.fill: parent; router: testWindow.router; settings: testWindow.settings }
                }
            })",
                          QUrl("qrc:/explore-runtime-test.qml"));
        QScopedPointer<QObject> root(component.createWithInitialProperties(
            {{"router", QVariant::fromValue(&router)}, {"settings", QVariant::fromValue(&shell)}}));
        QVERIFY2(root, qPrintable(component.errorString()));
        auto *window = qobject_cast<QQuickWindow *>(root.data());
        QVERIFY(window);
        window->requestActivate();
        if (QGuiApplication::platformName() != "offscreen")
            QVERIFY(QTest::qWaitForWindowActive(window));
        const auto artifacts = qEnvironmentVariable("ECHO_EXPLORE_ARTIFACTS");
        if (!artifacts.isEmpty())
            QVERIFY(QDir().mkpath(artifacts));
        QVERIFY(router.navigate("explore"));
        QTRY_VERIFY_WITH_TIMEOUT(
            ExploreServiceTestAccess::state(service, ExploreService::Categories) !=
                ExploreService::Loading,
            45000);
        QVERIFY(!service.categories().isEmpty());
        QVERIFY(service.category() != "0");
        QTRY_VERIFY_WITH_TIMEOUT(!service.loading(), 45000);
        QVERIFY2(service.errorMessage().isEmpty(), qPrintable(service.errorMessage()));
        QVERIFY(service.cards()->count() > 0);
        for (int tab = 0; tab < 5; ++tab) {
            service.selectTab(tab);
            QTRY_VERIFY_WITH_TIMEOUT(!service.loading(), 45000);
            QVERIFY2(service.errorMessage().isEmpty(), qPrintable(service.errorMessage()));
            QVERIFY(service.tab() == tab);
            if (tab == 1)
                QVERIFY(service.rankSongs()->count() > 0);
            else if (tab == 3) {
                QCOMPARE(service.newSongs()->count(), 30);
                QVERIFY(!service.newSongs()->tracks().first().catalogMetadata.isEmpty());
            } else
                QVERIFY(service.cards()->count() > 0);
            QTRY_VERIFY(findVisual(window->contentItem(), "explorePage"));
            for (bool dark : {false, true})
                for (int zoom : {100, 125}) {
                    theme.setMode(dark ? ThemeStore::Dark : ThemeStore::Light);
                    shell.resetZoom();
                    if (zoom == 125)
                        for (int i = 0; i < 5; ++i)
                            shell.zoomIn();
                    QTest::qWait(150);
                    if (!artifacts.isEmpty())
                        QVERIFY(
                            window->grabWindow().save(artifacts + QString("/tab-%1-%2-%3.png")
                                                                      .arg(tab)
                                                                      .arg(dark ? "dark" : "light")
                                                                      .arg(zoom)));
                }
        }
        auto *picker = findVisualOwnerObject(window->contentItem(), "explorePicker");
        QVERIFY(picker);
        picker->setProperty("kind", "sex");
        QVERIFY(QMetaObject::invokeMethod(picker, "open"));
        QTRY_VERIFY(picker->property("visible").toBool());
        QTest::qWait(150);
        if (!artifacts.isEmpty())
            QVERIFY(window->grabWindow().save(artifacts + "/artist-picker.png"));
        QVERIFY(QMetaObject::invokeMethod(picker, "close"));
        service.selectTab(3);
        QTest::qWait(150);
        auto *explore = findVisual(window->contentItem(), "explorePage");
        QVERIFY(explore);
        QVERIFY(QMetaObject::invokeMethod(explore, "batchRequested"));
        QPointer<QObject> batch =
            findVisualOwnerObject(window->contentItem(), "discoveryBatchDrawer");
        QVERIFY(batch);
        QTRY_VERIFY(batch->property("visible").toBool());
        service.selectTab(4);
        QTRY_VERIFY(batch.isNull());
        window->setWidth(650);
        QTest::qWait(150);
        if (!artifacts.isEmpty())
            QVERIFY(window->grabWindow().save(artifacts + "/artist-narrow.png"));
        QVector<QRectF> firstRow;
        auto collect = [&](auto &&self, QQuickItem *item) -> void {
            if (item->objectName() == "artistAvatarCard" && item->isVisible()) {
                const auto bounds =
                    item->mapRectToItem(window->contentItem(), item->boundingRect());
                if (bounds.top() > 0 && bounds.top() < window->height())
                    firstRow.append(bounds);
            }
            for (auto *child : item->childItems())
                self(self, child);
        };
        collect(collect, window->contentItem());
        QVERIFY(!firstRow.isEmpty());
        std::sort(firstRow.begin(), firstRow.end(),
                  [](const auto &a, const auto &b) { return a.top() < b.top(); });
        const auto rowY = firstRow.first().top();
        int rowCount = 0;
        qreal lastRight = 0;
        for (const auto &bounds : firstRow)
            if (qAbs(bounds.top() - rowY) < 2) {
                ++rowCount;
                lastRight = qMax(lastRight, bounds.right());
            }
        QCOMPARE(rowCount, 2);
        QVERIFY(qAbs(lastRight - (window->width() - 40 * shell.uiScale())) < 2);
        window->setWidth(1150);
        service.selectLetter("全部");
        QVERIFY(service.cards()->count() > 1000);
        QTest::qWait(150);
        auto *grid = findVisual(window->contentItem(), "exploreArtistDirectory");
        QVERIFY(grid);
        const auto delegates =
            countVisual(grid, "artistAvatarCard") + countVisual(grid, "artistNameEntry");
        QVERIFY2(delegates > 0 && delegates < 120, qPrintable(QString::number(delegates)));
        qInfo("Explore artist directory: %d rows, %d materialized cards", service.cards()->count(),
              delegates);
        service.selectSex("1");
        service.selectArtistType("1:0");
        QTRY_VERIFY_WITH_TIMEOUT(!service.loading(), 45000);
        QVERIFY2(service.errorMessage().isEmpty(), qPrintable(service.errorMessage()));
        service.selectTab(2);
        service.selectRegion("eur");
        QTRY_VERIFY_WITH_TIMEOUT(!service.loading(), 45000);
        QVERIFY2(service.errorMessage().isEmpty(), qPrintable(service.errorMessage()));
        QCOMPARE(service.cards()->count(), 3);
        // Hidden/active route toggles and selectors must not retain old popups or callbacks.
        QVERIFY2(errors.isEmpty(), qPrintable(errors.join('\n')));
        service.shutdown();
        root.reset();
        QSignalSpy drained(&playback, &PlaybackService::closed);
        playback.shutdown();
        QTRY_COMPARE_WITH_TIMEOUT(drained.size(), 1, 15000);
        api.shutdown();
    }
    void themedDateInkCentersInNativePixels()
    {
        QTemporaryDir dir;
        QSettings prefs(dir.filePath("prefs.ini"), QSettings::IniFormat);
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
        QSettings prefs(dir.filePath("prefs.ini"), QSettings::IniFormat);
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
                            QCOMPARE(rows.size(), intro ? 5 : 4);
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
    void nativeDiscoveryPages()
    {
        if (!qEnvironmentVariableIsSet("ECHO_DISCOVERY_LIVE_UI"))
            QSKIP("Opt-in real native discovery requests and Cocoa capture");
        QTemporaryDir dir;
        QSettings prefs(dir.filePath("prefs.ini"), QSettings::IniFormat);
        prefs.setValue("playback/volume", 0.);
        ApiClient api(DeviceIdentity::temporary());
        AccountService account(api);
        PlaybackService playback(api, prefs, nullptr, dir.filePath("playback.sqlite"));
        ThemeStore theme(prefs);
        ShellSettings shell(prefs);
        shell.acceptUserAgreement(); // These scenarios start after first-run consent.
        RouteController router;
        DiscoveryService service(api, playback, account);
        DiscoveryRouteCoordinator routes(service, router, true);
        QQmlEngine engine;
        theme.attachToEngine(engine);
        QStringList errors;
        connect(&engine, &QQmlEngine::warnings, this, [&](const auto &warnings) {
            for (const auto &w : warnings)
                errors.append(w.toString());
        });
        QQmlComponent component(&engine);
        component.setData(R"(
            import QtQuick
            import QtQuick.Window
            import EchoMusic.Shell
            import EchoMusic.Theme
            import EchoMusic.Discovery
            Window {
                id: testWindow
                required property RouteController router
                required property ShellSettings settings
                property real uiScale: settings.uiScale
                visible:true;width:1150;height:800
                Rectangle {anchors.fill:parent;color:Theme.main}
                Item {width:parent.width/settings.uiScale;height:parent.height/settings.uiScale;scale:settings.uiScale;transformOrigin:Item.TopLeft
                    RouteHost {anchors.fill:parent;router:testWindow.router;settings:testWindow.settings}
                }
            })",
                          QUrl("qrc:/discovery-runtime-test.qml"));
        QScopedPointer<QObject> root(component.createWithInitialProperties(
            {{"router", QVariant::fromValue(&router)}, {"settings", QVariant::fromValue(&shell)}}));
        QVERIFY2(root, qPrintable(component.errorString()));
        auto *window = qobject_cast<QQuickWindow *>(root.data());
        QVERIFY(window);
        window->requestActivate();
        if (QGuiApplication::platformName() != "offscreen")
            QVERIFY(QTest::qWaitForWindowActive(window));
        const auto artifacts = qEnvironmentVariable("ECHO_DISCOVERY_ARTIFACTS");
        if (!artifacts.isEmpty())
            QVERIFY(QDir().mkpath(artifacts));
        QVERIFY(router.navigate("home"));
        QTRY_VERIFY_WITH_TIMEOUT(
            service.state(DiscoveryService::Style) != DiscoveryService::Loading, 45000);
        QVERIFY2(service.state(DiscoveryService::Style) == DiscoveryService::Ready,
                 qPrintable(service.error(DiscoveryService::Style)));
        QTRY_COMPARE_WITH_TIMEOUT(service.state(DiscoveryService::Playlists),
                                  int(DiscoveryService::Ready), 45000);
        QTRY_COMPARE_WITH_TIMEOUT(service.state(DiscoveryService::Editors),
                                  int(DiscoveryService::Ready), 45000);
        QVERIFY(service.styleSongs()->count() > 0);
        QVERIFY(service.playlists()->count() > 0);
        QVERIFY(!service.styleSongs()->tracks().first().catalogMetadata.isEmpty());
        service.selectCategory("11292");
        QTRY_COMPARE_WITH_TIMEOUT(service.state(DiscoveryService::Playlists),
                                  int(DiscoveryService::Ready), 45000);
        QVERIFY(service.playlists()->count() > 0);
        service.selectCategory("0");
        QTRY_COMPARE_WITH_TIMEOUT(service.state(DiscoveryService::Playlists),
                                  int(DiscoveryService::Ready), 45000);
        service.toggleTag("S15");
        QTRY_VERIFY_WITH_TIMEOUT(
            service.state(DiscoveryService::Style) != DiscoveryService::Loading, 45000);
        QVERIFY2(service.state(DiscoveryService::Style) == DiscoveryService::Ready,
                 qPrintable(service.error(DiscoveryService::Style)));
        QVERIFY(service.styleSongs()->count() > 0);
        for (bool dark : {false, true})
            for (int zoom : {100, 125}) {
                theme.setMode(dark ? ThemeStore::Dark : ThemeStore::Light);
                shell.resetZoom();
                if (zoom == 125)
                    for (int i = 0; i < 5; ++i)
                        shell.zoomIn();
                for (const auto *page : {"home", "recommend-songs", "ranking"}) {
                    QVERIFY(router.navigate(page));
                    if (QString(page) == "recommend-songs")
                        QTRY_COMPARE_WITH_TIMEOUT(service.state(DiscoveryService::Daily),
                                                  int(DiscoveryService::Ready), 45000);
                    if (QString(page) == "ranking")
                        QTRY_COMPARE_WITH_TIMEOUT(service.state(DiscoveryService::RankSongs),
                                                  int(DiscoveryService::Ready), 45000);
                    QTRY_VERIFY(findVisual(window->contentItem(),
                                           QString(page) == "home"      ? "homePage"
                                           : QString(page) == "ranking" ? "rankingPage"
                                                                        : "dailyRecommendPage"));
                    if (QString(page) != "home") {
                        auto *catalog = QString(page) == "ranking" ? service.rankingSongs()
                                                                   : service.dailySongs();
                        const auto total = catalog->count();
                        catalog->setQuery("NO-MATCH-不可能出现的筛选词");
                        QCOMPARE(catalog->count(), 0);
                        catalog->setQuery({});
                        QCOMPARE(catalog->count(), total);
                        for (int i = 0; i < 3; ++i)
                            catalog->toggleSort(2);
                        QCOMPARE(catalog->sortDirection(), 0);
                        catalog->selectAll(true);
                        QCOMPARE(catalog->selectedCount(), catalog->totalCount());
                        catalog->clearSelection();
                    }
                    QTest::qWait(200);
                    QCOMPARE(countVisual(window->contentItem(), "songOriginalTag"), 0);
                    if (!artifacts.isEmpty())
                        QVERIFY(
                            window->grabWindow().save(artifacts + QString("/%1-%2-%3.png")
                                                                      .arg(page)
                                                                      .arg(dark ? "dark" : "light")
                                                                      .arg(zoom)));
                }
            }
        // A large domain catalog is deliberate view stress, never a fake HTTP
        // response. Only viewport rows plus bounded prefetch may instantiate.
        QVERIFY(router.navigate("home"));
        QTRY_VERIFY(findVisual(window->contentItem(), "homeView"));
        QJsonArray large;
        for (int i = 1; i <= 10000; ++i)
            large.append(domainSong(i));
        service.styleSongs()->replaceSongs(large);
        QTest::qWait(200);
        const auto delegates = countVisual(window->contentItem(), "homeSongCard");
        QVERIFY(delegates > 0 && delegates < 150);
        qInfo("Home10000 catalog: %d materialized song cards", delegates);
        QVERIFY2(errors.isEmpty(), qPrintable(errors.join('\n')));
        service.shutdown();
        root.reset();
        QSignalSpy drained(&playback, &PlaybackService::closed);
        playback.shutdown();
        QTRY_COMPARE_WITH_TIMEOUT(drained.size(), 1, 15000);
        api.shutdown();
    }
};
QTEST_MAIN(DiscoveryTest)
#include "DiscoveryTest.moc"
