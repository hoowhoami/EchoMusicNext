#include "AccountService.h"
#include "DiscoveryRouteCoordinator.h"
#include "DiscoveryService.h"
#include "ExploreService.h"
#include "EchoUiPartRegistry.h"
#include "PlaybackService.h"
#include "RouteController.h"
#include "SceneGraphConfiguration.h"
#include "SessionDatabase.h"
#include "SettingsStore.h"
#include "ShellSettings.h"
#include "SongMetadataService.h"
#include "ThemeStore.h"
#include "UiTypography.h"
#include <QJsonArray>
#include <QQuickImageProvider>
#include <QQuickRenderControl>
#include <QQuickGraphicsConfiguration>
#include <QQuickRenderTarget>
#include <QQuickStyle>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQmlIncubationController>
#include <rhi/qrhi_platform.h>
#include <objc/message.h>
#include <objc/runtime.h>
#include <QTemporaryDir>
#include <QtQuick/private/qquickwindow_p.h>
#include <QtQuick/private/qsgdefaultrendercontext_p.h>
#include <QtQuick/private/qsgrhidistancefieldglyphcache_p.h>
#include <QRawFont>
#include <rhi/qrhi.h>
#include <QtTest>
#include <mach/mach.h>
#include <algorithm>
#include <atomic>
using namespace EchoMusic;
namespace EchoMusic
{
class DiscoveryServiceTestAccess
{
public:
    static void complete(DiscoveryService &s, DiscoveryService::Part part, const QJsonValue &data)
    {
        constexpr quint64 id = 99001;
        s.m_pending.insert(id, {part, s.m_api.sessionRevision()});
        s.succeeded(id, QJsonDocument(data.toArray()).toJson());
    }
};
class ExploreServiceTestAccess
{
public:
    static void complete(ExploreService &s, ExploreService::Part part, const QJsonValue &data)
    {
        constexpr quint64 id = 99002;
        s.m_pending.insert(id, {part, s.m_api.sessionRevision()});
        s.accept(id, QJsonDocument(data.toObject()).toJson());
    }
};
} // namespace EchoMusic
namespace
{
// An embedded renderer exercises Metal without creating/activating a native
// desktop window. Visibility still follows the production resource gates.
class EmbeddedState : public QQuickWindowPrivate
{
public:
    void setVisible(bool v) override
    {
        if (visible == v)
            return;
        visible = v;
        visibility = v ? QWindow::Windowed : QWindow::Hidden;
        emit q_func() -> visibleChanged(v);
        emit q_func() -> visibilityChanged(visibility);
    }
};
class EmbeddedWindow : public QQuickWindow
{
public:
    explicit EmbeddedWindow(QQuickRenderControl *control)
        : QQuickWindow(*new EmbeddedState, control)
    {
    }
};
struct Scene
{
    QQuickRenderControl control;
    EmbeddedWindow window{&control};
    std::unique_ptr<QRhiTexture> color;
    std::unique_ptr<QRhiRenderBuffer> depth;
    std::unique_ptr<QRhiTextureRenderTarget> target;
    std::unique_ptr<QRhiRenderPassDescriptor> pass;
    bool create()
    {
        const QSize logical(1440, 900), pixels = logical * 2;
        window.resize(logical);
        window.contentItem()->setSize(logical);
        QQuickGraphicsConfiguration graphics;
        graphics.setTimestamps(true);
        window.setGraphicsConfiguration(graphics);
        if (!control.initialize())
            return false;
        auto *rhi = control.rhi();
        color.reset(rhi->newTexture(QRhiTexture::RGBA8, pixels, 1, QRhiTexture::RenderTarget));
        depth.reset(rhi->newRenderBuffer(QRhiRenderBuffer::DepthStencil, pixels, 1));
        if (!color->create() || !depth->create())
            return false;
        target.reset(rhi->newTextureRenderTarget({QRhiColorAttachment(color.get()), depth.get()}));
        pass.reset(target->newCompatibleRenderPassDescriptor());
        target->setRenderPassDescriptor(pass.get());
        if (!target->create())
            return false;
        auto output = QQuickRenderTarget::fromRhiRenderTarget(target.get());
        output.setDevicePixelRatio(2);
        window.setRenderTarget(output);
        window.setVisible(true);
        return !window.handle();
    }
    qint64 frame()
    {
        QElapsedTimer clock;
        clock.start();
        control.polishItems();
        control.beginFrame();
        control.sync();
        control.render();
        control.endFrame();
        return clock.nsecsElapsed() / 1000;
    }
    ~Scene()
    {
        window.setVisible(false);
        window.setRenderTarget({});
        control.invalidate();
    }
};
class Images final : public QQuickImageProvider
{
public:
    explicit Images(int delay = 0) : QQuickImageProvider(Image), delayMs(delay) {}
    const int delayMs;
    std::atomic_int calls{0}, maxSide{0};
    std::atomic<qint64> pixelsDecoded{0};
    QImage requestImage(const QString &id, QSize *size, const QSize &requested) override
    {
        if (delayMs > 0)
            QTest::qSleep(delayMs);
        const QSize pixels = requested.expandedTo(QSize(32, 32));
        *size = pixels;
        ++calls;
        pixelsDecoded += qint64(pixels.width()) * pixels.height();
        int previous = maxSide.load();
        const int side = std::max(pixels.width(), pixels.height());
        while (side > previous && !maxSide.compare_exchange_weak(previous, side)) {
        }
        QImage image(pixels, QImage::Format_ARGB32_Premultiplied);
        for (int y = 0; y < pixels.height(); ++y) {
            auto *line = reinterpret_cast<QRgb *>(image.scanLine(y));
            for (int x = 0; x < pixels.width(); ++x)
                line[x] = qRgb((x * 3 + id.size() * 5) % 256, (y * 2) % 256, (x + y) % 256);
        }
        return image;
    }
};
QQuickItem *find(QQuickItem *root, const QString &name)
{
    if (root->objectName() == name)
        return root;
    for (auto *child : root->childItems())
        if (auto *hit = find(child, name))
            return hit;
    return nullptr;
}
QJsonObject usage(QObject *root, QObject *engine)
{
    QSet<QObject *> seen;
    int covers = 0, activeCovers = 0, panels = 0, loaders = 0;
    const auto visit = [&](auto &&self, QObject *o) -> void {
        if (!o || seen.contains(o))
            return;
        seen.insert(o);
        if (o->objectName() == "coverSource") {
            ++covers;
            activeCovers += o->property("active").toBool();
        }
        panels += o->objectName().startsWith("recommendationPanel-");
        loaders += o->inherits("QQuickLoader");
        for (auto *child : o->children())
            self(self, child);
        if (auto *item = qobject_cast<QQuickItem *>(o))
            for (auto *child : item->childItems())
                self(self, child);
    };
    visit(visit, root);
    visit(visit, engine);
    task_vm_info_data_t info{};
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    task_info(mach_task_self(), TASK_VM_INFO, reinterpret_cast<task_info_t>(&info), &count);
    return {{"footprint", qint64(info.phys_footprint)},
            {"rss", qint64(info.resident_size)},
            {"objects", seen.size()},
            {"covers", covers},
            {"activeCovers", activeCovers},
            {"panels", panels},
            {"loaders", loaders}};
}
qint64 metalBytes(Scene &scene)
{
    const auto *native =
        static_cast<const QRhiMetalNativeHandles *>(scene.control.rhi()->nativeHandles());
    return reinterpret_cast<unsigned long (*)(id, SEL)>(objc_msgSend)(
        reinterpret_cast<id>(native->dev), sel_registerName("currentAllocatedSize"));
}
} // namespace
class HomeResourceTest final : public QObject
{
    Q_OBJECT
private slots:
    void cjkGlyphCacheChurnIsBounded()
    {
        QTemporaryDir dir;
        SettingsStore prefs(dir.filePath("prefs.ini"));
        ThemeStore theme(prefs);
        QQmlEngine engine;
        theme.attachToEngine(engine);
        Scene scene;
        QVERIFY(scene.create());
        QFont font("PingFang SC");
        font.setPixelSize(13);
        const auto rawFont = QRawFont::fromFont(font);
        QVERIFY(rawFont.isValid());
        QQmlComponent component(&engine);
        component.setData(R"(
import QtQuick
Rectangle {
 width: 1440; height: 900; color: "white"
 required property font sampleFont
 property string churnText: ""
 Text { x: 20; y: 20; text: "音乐推荐 ABCxyz 0123"; font: parent.sampleFont; color: "black" }
 Text { x: 20; y: 80; text: parent.churnText; font: parent.sampleFont; color: "black" }
})",
                          QUrl("qrc:/glyph-cache-churn.qml"));
        std::unique_ptr<QObject> root(
            component.createWithInitialProperties({{"sampleFont", font}}));
        QVERIFY2(root, qPrintable(component.errorString()));
        qobject_cast<QQuickItem *>(root.get())->setParentItem(scene.window.contentItem());
        for (int i = 0; i < 3; ++i) {
            QCoreApplication::processEvents();
            scene.frame();
        }
        auto *context = static_cast<QSGDefaultRenderContext *>(
            QQuickWindowPrivate::get(&scene.window)->context);
        QVERIFY(context);
        auto *cache = static_cast<QSGRhiDistanceFieldGlyphCache *>(
            context->distanceFieldGlyphCache(rawFont, -1));
        QVERIFY(cache);
        const auto capture = [&]() {
            auto *rhi = scene.control.rhi();
            QRhiReadbackResult result;
            auto *updates = rhi->nextResourceUpdateBatch();
            updates->readBackTexture(QRhiReadbackDescription(scene.color.get()), &result);
            QRhiCommandBuffer *commands = nullptr;
            if (rhi->beginOffscreenFrame(&commands) != QRhi::FrameOpSuccess)
                return QByteArray();
            commands->resourceUpdate(updates);
            rhi->endOffscreenFrame();
            rhi->finish();
            return result.data;
        };
        // Allow Qt's pending glyph uploads to reach a complete rendered frame.
        QTest::qWait(30);
        QCoreApplication::processEvents();
        scene.frame();
        scene.frame();
        const auto before = capture();
        QVERIFY(!before.isEmpty());
        int darkPixels = 0;
        for (qsizetype i = 0; i + 3 < before.size(); i += 4)
            darkPixels += uchar(before[i]) < 128 && uchar(before[i + 3]) > 128;
        QVERIFY(darkPixels > 100); // The comparison includes actual rendered text.
        QSet<const QSGDistanceFieldGlyphCache::Texture *> textures;
        int maximumSide = 0;
        qint64 maximumTurn = 0;
        int populatedGlyphs = 0;
        for (int batch = 0; batch < 32; ++batch) {
            QString text;
            for (int i = 0; i < 40; ++i)
                text += QChar(0x4e00 + batch * 40 + i);
            const auto glyphs = rawFont.glyphIndexesForString(text);
            QElapsedTimer clock;
            clock.start();
            root->setProperty("churnText", text);
            QCoreApplication::processEvents();
            scene.frame();
            scene.frame();
            maximumTurn = std::max(maximumTurn, clock.nsecsElapsed() / 1000);
            for (auto glyph : glyphs) {
                const auto *texture = cache->glyphTexture(glyph);
                if (texture && texture->texture) {
                    textures.insert(texture);
                    ++populatedGlyphs;
                }
            }
            for (const auto *texture : textures)
                if (texture->texture) {
                    const auto size = texture->texture->pixelSize();
                    maximumSide = std::max({maximumSide, size.width(), size.height()});
                }
        }
        root->setProperty("churnText", QString());
        QCoreApplication::processEvents();
        scene.frame();
        scene.frame();
        const auto after = capture();
        QCOMPARE(after.size(), before.size());
        int maximumDelta = 0, changedChannels = 0;
        bool sameDarkCoverage = true;
        for (qsizetype i = 0; i < before.size(); ++i) {
            maximumDelta =
                std::max(maximumDelta, std::abs(int(uchar(before[i])) - int(uchar(after[i]))));
            changedChannels += before[i] != after[i];
            if (i % 4 == 0)
                sameDarkCoverage =
                    sameDarkCoverage && (uchar(before[i]) < 128) == (uchar(after[i]) < 128);
        }
        // Relocated atlas UVs may round one 8-bit channel by one level on the
        // GPU. Require unchanged stroke coverage and at most that quantization.
        qInfo().noquote()
            << "glyph-churn maxSide" << maximumSide << "maxTurnUs" << maximumTurn
            << "changedChannels" << changedChannels << "maximumDelta" << maximumDelta
            << "metalBytes" << metalBytes(scene)
            << QJsonDocument(usage(root.get(), &engine)).toJson(QJsonDocument::Compact);
        QVERIFY(populatedGlyphs >= 1200); // Inspect the cache actually populated by QML Text.
        QVERIFY(sameDarkCoverage);
        QVERIFY(maximumDelta <= 1);
        QVERIFY2(maximumSide <= 2048, "Sparse distance-field atlas grew to the GPU texture limit");
        root.reset();
    }
    void specialBackgroundsKeepDecodeBudgets();
    void textRendererResourceComparison_data()
    {
        QTest::addColumn<int>("renderer");
        QTest::addColumn<int>("fontSize");
        QTest::addColumn<double>("zoom");
        QTest::newRow("qt-13") << 0 << 13 << 1.;
        QTest::newRow("curve-13") << 2 << 13 << 1.;
        QTest::newRow("qt-26-zoom") << 0 << 26 << 1.25;
        QTest::newRow("curve-26-zoom") << 2 << 26 << 1.25;
    }
    void textRendererResourceComparison()
    {
        QFETCH(int, renderer);
        QFETCH(int, fontSize);
        QFETCH(double, zoom);
        QQmlEngine engine;
        Scene scene;
        QVERIFY(scene.create());
        QQmlComponent component(&engine);
        component.setData(R"(
import QtQuick
Rectangle {
 id: root; width: 1440; height: 900; color: "white"
 required property int renderer
 required property int fontSize
 required property real zoom
 property string churn: ""
 Item {
  scale: root.zoom
  Repeater {
   model: 80
   Text {
    required property int index
    x: 12 + (index % 5) * 280 / root.zoom
    y: 12 + Math.floor(index / 5) * 54 / root.zoom
    text: root.churn.slice(index * 16, (index + 1) * 16)
    font.family: "PingFang SC"; font.pixelSize: root.fontSize
    renderType: root.renderer; color: "black"; textFormat: Text.PlainText
   }
  }
 }
})",
                          QUrl("qrc:/text-renderer-comparison.qml"));
        std::unique_ptr<QObject> root(component.createWithInitialProperties(
            {{"renderer", renderer}, {"fontSize", fontSize}, {"zoom", zoom}}));
        QVERIFY2(root, qPrintable(component.errorString()));
        qobject_cast<QQuickItem *>(root.get())->setParentItem(scene.window.contentItem());
        QList<qint64> cpuTurns;
        QList<double> gpuFrames;
        auto *rhi = scene.control.rhi();
        for (int batch = 0; batch < 32; ++batch) {
            QString text;
            for (int i = 0; i < 1280; ++i)
                text += QChar(0x4e00 + (i + batch * 40) % 2400);
            QElapsedTimer clock;
            clock.start();
            root->setProperty("churn", text);
            QCoreApplication::processEvents();
            scene.frame();
            cpuTurns.append(clock.nsecsElapsed() / 1000);
            // Completion makes this a GPU-duration measurement, not a CPU
            // submission estimate. No platform window or swapchain is created.
            rhi->finish();
            if (const auto seconds = scene.control.commandBuffer()->lastCompletedGpuTime();
                seconds > 0)
                gpuFrames.append(seconds * 1000.);
        }
        std::sort(cpuTurns.begin(), cpuTurns.end());
        std::sort(gpuFrames.begin(), gpuFrames.end());
        qInfo().noquote()
            << "text-renderer" << renderer << "font" << fontSize << "zoom" << zoom << "cpuMedianUs"
            << cpuTurns[cpuTurns.size() / 2] << "cpuMaxUs" << cpuTurns.back() << "gpuMedianMs"
            << (gpuFrames.isEmpty() ? -1. : gpuFrames[gpuFrames.size() / 2]) << "gpuMaxMs"
            << (gpuFrames.isEmpty() ? -1. : gpuFrames.back()) << "metalBytes" << metalBytes(scene)
            << QJsonDocument(usage(root.get(), &engine)).toJson(QJsonDocument::Compact);
        QVERIFY(!gpuFrames.isEmpty());
        root.reset();
    }
    void playlistSquareRetiresOffscreenResources_data()
    {
        QTest::addColumn<bool>("encoded");
        QTest::addColumn<int>("imageDelay");
        QTest::newRow("generated") << false << 0;
        QTest::newRow("encoded-png") << true << 0;
        QTest::newRow("delayed") << false << 50;
    }
    void playlistSquareRetiresOffscreenResources()
    {
        QFETCH(bool, encoded);
        QFETCH(int, imageDelay);
        QTemporaryDir dir;
        const auto artworkPath = dir.filePath("cover.png");
        if (encoded) {
            QImage artwork(1600, 1600, QImage::Format_ARGB32_Premultiplied);
            for (int y = 0; y < artwork.height(); ++y)
                for (int x = 0; x < artwork.width(); ++x)
                    artwork.setPixel(x, y, qRgb(x % 256, y % 256, (x + y) % 256));
            QVERIFY(artwork.save(artworkPath));
        }
        SettingsStore prefs(dir.filePath("prefs.ini"));
        ApiClient api(DeviceIdentity::temporary());
        AccountService account(api);
        PlaybackService playback(api, prefs, nullptr, dir.filePath("playback.sqlite"));
        ThemeStore theme(prefs);
        theme.setMotionEnabled(true);
        ShellSettings settings(prefs);
        settings.acceptUserAgreement();
        RouteController router;
        DiscoveryService discovery(api, playback, account);
        DiscoveryRouteCoordinator routes(discovery, router, false);
        ExploreService service(api, playback, router, false);
        service.setDiscovery(&discovery);
        service.registerRoutes(router);
        QQmlEngine engine;
        QQmlIncubationController incubation;
        engine.setIncubationController(&incubation);
        theme.attachToEngine(engine);
        auto *images = new Images(imageDelay);
        engine.addImageProvider("home", images);
        QSignalSpy warnings(&engine, &QQmlEngine::warnings);
        Scene scene;
        QVERIFY(scene.create());
        QQmlComponent component(&engine);
        component.setData(R"(
import QtQuick
import EchoMusic.Shell
import EchoMusic.Theme
Rectangle {
 id: host; width: 1440; height: 900; color: Theme.main
 required property RouteController router
 required property ShellSettings settings
 RouteHost { anchors.fill: parent; router: host.router; settings: host.settings }
})",
                          QUrl("qrc:/playlist-resource-test.qml"));
        std::unique_ptr<QObject> root(
            component.createWithInitialProperties({{"router", QVariant::fromValue(&router)},
                                                   {"settings", QVariant::fromValue(&settings)}}));
        QVERIFY2(root, qPrintable(component.errorString()));
        qobject_cast<QQuickItem *>(root.get())->setParentItem(scene.window.contentItem());
        QVERIFY(router.navigate("explore"));
        QJsonArray cards;
        for (int i = 0; i < 96; ++i)
            cards.append(QJsonObject{
                {"id", QString::number(i + 1)},
                {"title", QString("歌单 %1 – ").arg(i) +
                              QStringLiteral("聆听今天的心情与每一段未曾抵达的远方").repeated(12)},
                {"cover_url",
                 encoded ? QUrl::fromLocalFile(artworkPath).toString() + QString("?id=%1").arg(i)
                         : QString("image://home/playlist-%1").arg(i)}});
        DiscoveryServiceTestAccess::complete(discovery, DiscoveryService::Playlists, cards);
        DiscoveryServiceTestAccess::complete(discovery, DiscoveryService::Editors, cards);
        ExploreServiceTestAccess::complete(service, ExploreService::Albums,
                                           QJsonObject{{"regions", QJsonObject{{"chn", cards}}}});
        auto pump = [&](int ms) {
            QElapsedTimer timer;
            timer.start();
            qint64 maximum = 0;
            do {
                QElapsedTimer work;
                work.start();
                QCoreApplication::processEvents();
                incubation.incubateFor(2);
                scene.frame();
                maximum = std::max(maximum, work.nsecsElapsed() / 1000);
                QTest::qWait(2);
            } while (timer.elapsed() < ms);
            return maximum;
        };
        pump(500);
        auto *view = find(scene.window.contentItem(), "playlistSquareView");
        QVERIFY(view);
        for (int pass = 0; pass < 6; ++pass) {
            const double end = view->property("contentHeight").toDouble() - view->height();
            qint64 maxTurn = 0, maxCommand = 0;
            for (int step = 0; step <= 36; ++step) {
                QElapsedTimer command;
                command.start();
                view->setProperty("contentY", end * (pass % 2 ? 36 - step : step) / 36.);
                maxCommand = std::max(maxCommand, command.nsecsElapsed() / 1000);
                maxTurn = std::max(maxTurn, pump(35));
            }
            QPointer<QQuickItem> oldView = view;
            QVERIFY(router.navigate("artist-recommendations"));
            pump(200);
            QVERIFY(oldView.isNull());
            const auto away = usage(root.get(), &engine);
            QCOMPARE(away["activeCovers"].toInt(), 0);
            QElapsedTimer navigation;
            navigation.start();
            router.back();
            const auto navigationUs = navigation.nsecsElapsed() / 1000;
            pump(250);
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            view = find(scene.window.contentItem(), "playlistSquareView");
            QVERIFY(view);
            const auto retained = usage(root.get(), &engine);
            QVERIFY(retained["activeCovers"].toInt() <= 96);
            QVERIFY(retained["objects"].toInt() < 15000);
            qInfo().noquote() << "playlist-resource pass" << pass << "maxTurnUs" << maxTurn
                              << "maxCommandUs" << maxCommand << "navigationUs" << navigationUs
                              << "metalBytes" << metalBytes(scene)
                              << QJsonDocument(retained).toJson(QJsonDocument::Compact);
        }
        // 1440px / six columns at DPR2 needs at most a 768px provider raster.
        // The unpositioned/synchronous grid requested transient 3200px images.
        QVERIFY2(images->maxSide.load() <= 1024, "Transient grid geometry inflated cover decoding");
        qInfo() << "playlist-decoding calls" << images->calls.load() << "maxSide"
                << images->maxSide.load() << "pixels" << images->pixelsDecoded.load();
        root.reset();
        pump(100);
        const auto retired = usage(nullptr, &engine);
        qInfo().noquote() << "playlist-resource retired"
                          << QJsonDocument(retired).toJson(QJsonDocument::Compact);
        QCOMPARE(retired["covers"].toInt(), 0);
        QCOMPARE(retired["loaders"].toInt(), 0);
        QVERIFY(!scene.window.handle());
        for (const auto &warning : warnings)
            for (const auto &error : warning.first().value<QList<QQmlError>>())
                qInfo().noquote() << "qml-warning" << error.toString();
        QCOMPARE(warnings.size(), 0);
        discovery.shutdown();
        service.shutdown();
        QSignalSpy drained(&playback, &PlaybackService::closed);
        playback.shutdown();
        QTRY_COMPARE(drained.size(), 1);
        api.shutdown();
    }
    void homeRetiresOffscreenResources_data()
    {
        QTest::addColumn<bool>("indexing");
        QTest::newRow("plain") << false;
        QTest::newRow("indexed") << true;
    }
    void homeRetiresOffscreenResources()
    {
        QFETCH(bool, indexing);
        QTemporaryDir dir;
        SettingsStore prefs(dir.filePath("prefs.ini"));
        prefs.setValue("playback/volume", 0.);
        const QStringList keys{"daily", "style", "3001", "3004", "3005", "3006",
                               "3014",  "3101",  "3102", "3103", "new",  "3107"};
        prefs.setValue("discovery/cards", keys);
        prefs.setValue("discovery/cardsDefaultsVersion", 1);
        ApiClient api(DeviceIdentity::temporary());
        AccountService account(api);
        PlaybackService playback(api, prefs, nullptr, dir.filePath("playback.sqlite"));
        SessionDatabase database(dir.filePath("session"));
        SongMetadataService metadata(api);
        metadata.enableSqlIndex(database);
        playback.attachMetadata(metadata);
        ThemeStore theme(prefs);
        theme.setMotionEnabled(false);
        theme.setFloatingFrosted(false);
        ShellSettings settings(prefs);
        settings.acceptUserAgreement();
        RouteController router;
        DiscoveryService service(api, playback, account);
        service.setPreferences(&prefs);
        DiscoveryRouteCoordinator routes(service, router, false);
        QQmlEngine engine;
        QQmlIncubationController incubation;
        engine.setIncubationController(&incubation);
        theme.attachToEngine(engine);
        auto *images = new Images;
        engine.addImageProvider("home", images);
        QSignalSpy warnings(&engine, &QQmlEngine::warnings);
        Scene scene;
        QVERIFY(scene.create());
        QQmlComponent component(&engine);
        component.setData(R"(
import QtQuick
import EchoMusic.Shell
import EchoMusic.Theme
Rectangle {
 id: host; width: 1440; height: 900; color: Theme.main
 required property RouteController router
 required property ShellSettings settings
 RouteHost { anchors.fill: parent; router: host.router; settings: host.settings }
})",
                          QUrl("qrc:/home-resource-test.qml"));
        std::unique_ptr<QObject> root(
            component.createWithInitialProperties({{"router", QVariant::fromValue(&router)},
                                                   {"settings", QVariant::fromValue(&settings)}}));
        QVERIFY2(root, qPrintable(component.errorString()));
        qobject_cast<QQuickItem *>(root.get())->setParentItem(scene.window.contentItem());
        auto *registry = EchoUiPartRegistry::forEngine(engine);
        QObject consumer;
        if (indexing)
            registry->acquireStructure(&consumer);
        QVERIFY(router.navigate("home"));
        for (const auto &key : keys) {
            QJsonArray songs;
            for (int i = 0; i < 256; ++i) {
                const auto id = key + QString::number(i);
                songs.append(QJsonObject{
                    {"id", id},
                    {"hash", QString(32, 'a')},
                    {"title", QString("歌曲 %1 - 带有足够长度的歌曲名字").arg(i)},
                    {"artists", QJsonArray{QJsonObject{{"id", 1}, {"name", "Artist 歌手"}}}},
                    {"cover_url", "image://home/" + id},
                    {"metadata",
                     QJsonObject{
                         {"recommendation",
                          QJsonObject{
                              {"detail", "音乐里的故事，与今天的自己相遇。"},
                              {"curator", QJsonObject{{"name", "音乐推荐人"},
                                                      {"avatar_url", "image://home/avatar"}}}}}}}});
            }
            QJsonArray groups;
            if (key == "3102" || key == "3103")
                for (int i = 0; i < 64; ++i)
                    groups.append(QJsonObject{
                        {"title", QString("主题 %1").arg(i)},
                        {"description", "国风 DNA 动了，单曲循环到上头"},
                        {"cover_url", QString("image://home/group-%1").arg(i)},
                        {"preview", QJsonArray{QJsonObject{{"id", "1"},
                                                           {"title", "春庭雪"},
                                                           {"artist", "邓寓君（等什么君）"}},
                                               QJsonObject{{"id", "2"},
                                                           {"title", "关山酒"},
                                                           {"artist", "邓寓君（等什么君）"}},
                                               QJsonObject{{"id", "3"},
                                                           {"title", "出山"},
                                                           {"artist", "花粥 / 王胜娚"}}}}});
            service.recommendationCard(key)->accept({{"songs", songs}, {"groups", groups}});
        }
        QString phase = "initial";
        auto pump = [&](int ms) {
            QElapsedTimer timer;
            timer.start();
            qint64 maximum = 0;
            do {
                QElapsedTimer work;
                work.start();
                QCoreApplication::processEvents();
                const auto eventsUs = work.nsecsElapsed() / 1000;
                incubation.incubateFor(2);
                const auto incubationUs = work.nsecsElapsed() / 1000 - eventsUs;
                const auto renderingUs = scene.frame();
                const auto totalUs = work.nsecsElapsed() / 1000;
                maximum = std::max(maximum, totalUs);
                if (totalUs > 24000)
                    qInfo() << "home-slow-turn" << phase << "eventsUs" << eventsUs << "incubationUs"
                            << incubationUs << "renderingUs" << renderingUs;
                QTest::qWait(2);
            } while (timer.elapsed() < ms);
            return maximum;
        };
        pump(500);
        auto *view = find(scene.window.contentItem(), "recommendationCardsView");
        QVERIFY(view);
        qInfo().noquote()
            << "home-resource initial"
            << "metalBytes" << metalBytes(scene)
            << QJsonDocument(usage(root.get(), &engine)).toJson(QJsonDocument::Compact);
        for (int pass = 0; pass < 8; ++pass) {
            qint64 maxFrame = 0;
            qint64 maxScrollCommand = 0, maxSlideCommand = 0;
            for (int j = 0; j < keys.size(); ++j) {
                int index = pass % 2 ? keys.size() - 1 - j : j;
                QElapsedTimer command;
                command.start();
                QVERIFY(QMetaObject::invokeMethod(view, "positionViewAtIndex", Q_ARG(int, index),
                                                  Q_ARG(int, 0)));
                maxScrollCommand = std::max(maxScrollCommand, command.nsecsElapsed() / 1000);
                phase = QString("scroll-%1-%2").arg(pass).arg(index);
                maxFrame = std::max(maxFrame, pump(60));
                for (const auto &key : {"3004", "3006", "3102", "3103"})
                    if (auto *carousel =
                            find(scene.window.contentItem(),
                                 "recommendation" +
                                     QString(key[0] == '3' && key[1] == '1' ? "Group" : "Feature") +
                                     "Carousel-" + key)) {
                        theme.setMotionEnabled(true);
                        command.restart();
                        QVERIFY(QMetaObject::invokeMethod(carousel, "step", Q_ARG(int, 1)));
                        maxSlideCommand = std::max(maxSlideCommand, command.nsecsElapsed() / 1000);
                        phase = QString("slide-%1-%2").arg(pass).arg(key);
                        maxFrame = std::max(maxFrame, pump(280));
                        theme.setMotionEnabled(false);
                    }
            }
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            qInfo().noquote()
                << "home-resource pass" << pass << "maxFrameUs" << maxFrame << "maxScrollCommandUs"
                << maxScrollCommand << "maxSlideCommandUs" << maxSlideCommand << "metalBytes"
                << metalBytes(scene)
                << QJsonDocument(usage(root.get(), &engine)).toJson(QJsonDocument::Compact);
            QPointer<QQuickItem> previous = view;
            auto *source = service.recommendationCard("3001")->collection();
            QVERIFY(router.navigate("recommendation-detail", {}, "3001"));
            phase = "detail";
            pump(500);
            QVERIFY(previous.isNull());
            QElapsedTimer navigation;
            navigation.start();
            router.back();
            const auto navigationUs = navigation.nsecsElapsed() / 1000;
            phase = "return";
            pump(200);
            view = find(scene.window.contentItem(), "recommendationCardsView");
            QVERIFY(view);
            QCOMPARE(service.recommendationCard("3001")->collection(), source);
            const auto retained = usage(root.get(), &engine);
            QVERIFY(retained["activeCovers"].toInt() <= 64);
            QVERIFY(retained["panels"].toInt() <= 8);
            QVERIFY(retained["objects"].toInteger() < 10000);
            qInfo().noquote()
                << "home-resource returned" << pass << "navigationUs" << navigationUs
                << "metalBytes" << metalBytes(scene)
                << QJsonDocument(usage(root.get(), &engine)).toJson(QJsonDocument::Compact);
        }
        registry->releaseStructure(&consumer);
        root.reset();
        pump(100);
        QVERIFY(!scene.window.handle());
        qInfo().noquote() << "home-resource retired"
                          << QJsonDocument(usage(nullptr, &engine)).toJson(QJsonDocument::Compact);
        const auto retired = usage(nullptr, &engine);
        QCOMPARE(retired["activeCovers"].toInt(), 0);
        QCOMPARE(retired["covers"].toInt(), 0);
        QCOMPARE(retired["panels"].toInt(), 0);
        QCOMPARE(retired["loaders"].toInt(), 0);
        for (const auto &warning : warnings)
            for (const auto &error : warning.first().value<QList<QQmlError>>())
                qInfo().noquote() << "qml-warning" << error.toString();
        QCOMPARE(warnings.size(), 0);
        service.shutdown();
        QSignalSpy drained(&playback, &PlaybackService::closed);
        playback.shutdown();
        QTRY_COMPARE(drained.size(), 1);
        api.shutdown();
    }
};
void HomeResourceTest::specialBackgroundsKeepDecodeBudgets()
{
    QTemporaryDir dir;
    SettingsStore prefs(dir.filePath("prefs.ini"));
    ThemeStore theme(prefs);
    QQmlEngine engine;
    theme.attachToEngine(engine);
    auto *images = new Images;
    engine.addImageProvider("budgets", images);
    Scene scene;
    QVERIFY(scene.create());
    QSignalSpy warnings(&engine, &QQmlEngine::warnings);
    QQmlComponent component(&engine);
    component.setData(R"(
import QtQuick
import EchoMusic.Discovery
import EchoMusic.Shell
import EchoMusic.Playback
Item {
 width: 1440; height: 900
 DiscoverFlowArtwork {
  width: 1440; height: 900; imageUrl: "image://budgets/discover"
  materialOpacity: 1; scrim: Qt.vector3d(0, 0, 0)
 }
 MiniLyricBackdrop {
  width: 360; height: 300; coverSource: "image://budgets/mini"; baseColor: "white"
 }
 BackgroundArtwork {
  coverSource: "image://budgets/playback"; phase: 0
  targetSize: Qt.size(64, 64); renderDpr: 2
 }
})",
                      QUrl("qrc:/special-background-budget.qml"));
    std::unique_ptr<QObject> root(component.create());
    QVERIFY2(root, qPrintable(component.errorString()));
    qobject_cast<QQuickItem *>(root.get())->setParentItem(scene.window.contentItem());
    const QStringList names{"discoverFlowArtwork", "miniLyricBackdropCover",
                            "playbackBackgroundCover"};
    const QList<int> budgets{800, 512, 32};
    for (int index = 0; index < names.size(); ++index) {
        const auto name = names[index];
        const int budget = budgets[index];
        auto *image = root->findChild<QObject *>(name);
        QVERIFY(image);
        QTRY_COMPARE(image->property("status").toInt(), 1);
        scene.frame();
        const auto size = image->property("sourceSize").toSize();
        QVERIFY2(size.width() > 0 && size.height() > 0 && size.width() <= budget &&
                     size.height() <= budget,
                 qPrintable(QString("%1 decoded %2x%3, budget %4")
                                .arg(name)
                                .arg(size.width())
                                .arg(size.height())
                                .arg(budget)));
    }
    QVERIFY(images->calls.load() >= 3);
    QCOMPARE(warnings.size(), 0);
    root.reset();
    scene.frame();
}
int main(int argc, char **argv)
{
    configureSceneGraph();
    QQuickWindow::setSceneGraphBackend("rhi");
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Metal);
    QGuiApplication app(argc, argv);
    configureUiTypography();
    QQuickStyle::setStyle("Basic");
    HomeResourceTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "HomeResourceTest.moc"
