#include "ThemeStore.h"
#include "SceneGraphConfiguration.h"
#include <QSGTexture>
#include <QFile>
#include <QBuffer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QQuickGraphicsConfiguration>
#include <QSGRendererInterface>
#include <QTemporaryDir>
#include <QtTest>
#include <QtGui/rhi/qrhi.h>
#include <cmath>
#include <memory>
#include <vector>

using namespace EchoMusic;

namespace
{
QImage frame(QQuickWindow &window)
{
    QSignalSpy frames(&window, &QQuickWindow::frameSwapped);
    if (!QTest::qWaitFor([&] {
            window.update();
            return frames.size() >= 2;
        }))
        return {};
    return window.grabWindow();
}
qreal coverage(const QRectF &box, qreal radius, int x, int y)
{
    int inside = 0;
    constexpr int grid = 16;
    for (int j = 0; j < grid; ++j)
        for (int i = 0; i < grid; ++i) {
            const QPointF p(x + (i + .5) / grid, y + (j + .5) / grid);
            const qreal dx =
                std::max({box.left() + radius - p.x(), 0., p.x() - box.right() + radius});
            const qreal dy =
                std::max({box.top() + radius - p.y(), 0., p.y() - box.bottom() + radius});
            inside += box.contains(p) && dx * dx + dy * dy <= radius * radius;
        }
    return qreal(inside) / (grid * grid);
}
} // namespace

class CoverRenderingTest final : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase()
    {
        configureSceneGraph();
    }
    void scrollReturnDoesNotReplayArtworkReveal()
    {
        QTemporaryDir directory;
        SettingsStore settings(directory.filePath("settings.ini"));
        ThemeStore theme(settings);
        theme.setMotionEnabled(true);
        QImage artwork(128, 128, QImage::Format_RGB32);
        artwork.fill(QColor("#20c060"));
        QByteArray png;
        QBuffer encoded(&png);
        QVERIFY(encoded.open(QIODevice::WriteOnly) && artwork.save(&encoded, "PNG"));
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        int requests = 0;
        connect(&server, &QTcpServer::newConnection, &server, [&] {
            auto *socket = server.nextPendingConnection();
            connect(socket, &QTcpSocket::readyRead, socket, [&, socket] {
                auto request = socket->property("request").toByteArray() + socket->readAll();
                socket->setProperty("request", request);
                if (!request.contains("\r\n\r\n") || socket->property("served").toBool())
                    return;
                socket->setProperty("served", true);
                ++requests;
                socket->write("HTTP/1.1 200 OK\r\nContent-Type: image/png\r\nCache-Control: "
                              "public, max-age=3600\r\nContent-Length: " +
                              QByteArray::number(png.size()) + "\r\nConnection: close\r\n\r\n" +
                              png);
                socket->disconnectFromHost();
            });
            connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        });
        QQmlEngine engine;
        theme.attachToEngine(engine);
        QSignalSpy warnings(&engine, &QQmlEngine::warnings);
        QQmlComponent component(&engine);
        component.setData(R"(
import QtQuick
import EchoMusic.Controls
Window {
 id: host
 visible: true; width: 240; height: 240; color: "black"
 required property url artwork
 EchoCover { objectName: "probe"; anchors.centerIn: parent; width: 96; height: 96; source: host.artwork }
})",
                          QUrl("qrc:/cover-scroll-return.qml"));
        QScopedPointer<QObject> root(component.createWithInitialProperties(
            {{"artwork",
              QUrl(QString("http://127.0.0.1:%1/cover.png").arg(server.serverPort()))}}));
        QVERIFY2(root, qPrintable(component.errorString()));
        auto *window = qobject_cast<QQuickWindow *>(root.data());
        QVERIFY(window && QTest::qWaitForWindowExposed(window));
        auto *cover = root->findChild<QQuickItem *>("probe");
        QVERIFY(cover);
        QTRY_COMPARE(cover->property("status").toInt(), 1);
        QTRY_COMPARE(cover->property("revealOpacity").toReal(), 1.);
        QCOMPARE(requests, 1);
        for (int cycle = 0; cycle < 6; ++cycle) {
            cover->setProperty("resourceActive", false);
            auto *image = cover->findChild<QQuickItem *>("coverImage");
            QTRY_VERIFY(image->property("source").toUrl().isEmpty());
            QVERIFY(!frame(*window).isNull());
            cover->setProperty("resourceActive", true);
            QTRY_COMPARE(cover->property("status").toInt(), 1);
            qInfo() << "cover return" << cycle << "HTTP requests" << requests << "opacity"
                    << cover->property("revealOpacity");
            QCOMPARE(requests, 1);
            QCOMPARE(cover->property("revealOpacity").toReal(), 1.);
            const auto pixels = frame(*window);
            QVERIFY(!pixels.isNull());
            QVERIFY(pixels.pixelColor(pixels.width() / 2, pixels.height() / 2).green() > 180);
        }
        // A reused delegate must still transition a genuinely new, slow URL.
        root->setProperty(
            "artwork", QUrl(QString("http://127.0.0.1:%1/another.png").arg(server.serverPort())));
        QTRY_COMPARE(requests, 2);
        QTRY_COMPARE(cover->property("status").toInt(), 1);
        QVERIFY(cover->property("revealOpacity").toReal() < 1.);
        QTRY_COMPARE(cover->property("revealOpacity").toReal(), 1.);
        QCOMPARE(warnings.size(), 0);
    }
    void imageAtlasBudgetAndLargeImageFallback()
    {
        if (QGuiApplication::platformName() == "offscreen")
            QSKIP("Texture allocation requires native RHI");
        QQuickWindow window;
        window.resize(1100, 750);
        struct Result
        {
            QMutex mutex;
            bool ready = false, smallAtlased = false, largeAtlased = true;
            QSize atlasSize, largeSize;
        };
        auto result = std::make_shared<Result>();
        const auto connection = connect(
            &window, &QQuickWindow::beforeRendering, &window,
            [result, &window] {
                QMutexLocker lock(&result->mutex);
                if (result->ready)
                    return;
                auto *rhi = static_cast<QRhi *>(window.rendererInterface()->getResource(
                    &window, QSGRendererInterface::RhiResource));
                auto *swapchain =
                    static_cast<QRhiSwapChain *>(window.rendererInterface()->getResource(
                        &window, QSGRendererInterface::RhiSwapchainResource));
                if (!rhi || !swapchain)
                    return;
                QImage small(64, 64, QImage::Format_RGB32), large(1100, 1100, QImage::Format_RGB32);
                small.fill(Qt::red);
                large.fill(Qt::blue);
                std::unique_ptr<QSGTexture> first(
                    window.createTextureFromImage(small, QQuickWindow::TextureCanUseAtlas));
                std::unique_ptr<QSGTexture> second(
                    window.createTextureFromImage(large, QQuickWindow::TextureCanUseAtlas));
                if (!first || !second)
                    return;
                auto *updates = rhi->nextResourceUpdateBatch();
                first->commitTextureOperations(rhi, updates);
                second->commitTextureOperations(rhi, updates);
                swapchain->currentFrameCommandBuffer()->resourceUpdate(updates);
                result->smallAtlased = first->isAtlasTexture();
                result->largeAtlased = second->isAtlasTexture();
                if (first->rhiTexture())
                    result->atlasSize = first->rhiTexture()->pixelSize();
                if (second->rhiTexture())
                    result->largeSize = second->rhiTexture()->pixelSize();
                result->ready = true;
            },
            Qt::DirectConnection);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QTRY_VERIFY(([&] {
            QMutexLocker lock(&result->mutex);
            window.update();
            return result->ready;
        })());
        disconnect(connection);
        QMutexLocker lock(&result->mutex);
        QCOMPARE(result->atlasSize, QSize(2048, 1024));
        QVERIFY(result->smallAtlased);
        QVERIFY(!result->largeAtlased);
        QCOMPARE(result->largeSize, QSize(1100, 1100));
    }
    void cropAlphaAndRoundedPixels_data()
    {
        QTest::addColumn<qreal>("zoom");
        QTest::addColumn<QSize>("sourceSize");
        QTest::addColumn<bool>("circular");
        for (qreal zoom : {1., 1.25, 1.5})
            for (const QSize size : {QSize(160, 80), QSize(80, 160), QSize(128, 128)})
                for (bool circular : {false, true})
                    QTest::newRow(qPrintable(QString("%1-%2x%3-%4")
                                                 .arg(zoom)
                                                 .arg(size.width())
                                                 .arg(size.height())
                                                 .arg(circular)))
                        << zoom << size << circular;
    }
    void cropAlphaAndRoundedPixels()
    {
        if (QGuiApplication::platformName() == "offscreen")
            QSKIP("Real cover pixels require a native RHI window");
        QFETCH(qreal, zoom);
        QFETCH(QSize, sourceSize);
        QFETCH(bool, circular);
        QTemporaryDir directory;
        QImage artwork(sourceSize, QImage::Format_ARGB32);
        // Opaque white edges isolate rounded coverage; interior quadrants
        // distinguish crop, orientation and premultiplied transparent pixels.
        artwork.fill(Qt::white);
        for (int y = sourceSize.height() / 4; y < sourceSize.height() * 3 / 4; ++y)
            for (int x = sourceSize.width() / 4; x < sourceSize.width() * 3 / 4; ++x)
                artwork.setPixelColor(
                    x, y,
                    x < sourceSize.width() / 2
                        ? (y < sourceSize.height() / 2 ? QColor("red") : QColor("blue"))
                        : (y < sourceSize.height() / 2 ? QColor(0, 255, 0, 128)
                                                       : QColor(0, 0, 0, 0)));
        const auto path = directory.filePath("pattern.png");
        QVERIFY(artwork.save(path));
        SettingsStore settings(directory.filePath("settings.ini"));
        ThemeStore theme(settings);
        theme.setMotionEnabled(false);
        QQmlEngine engine;
        theme.attachToEngine(engine);
        QSignalSpy warnings(&engine, &QQmlEngine::warnings);
        QQmlComponent component(&engine);
        component.setData(R"(
import QtQuick
import EchoMusic.Controls
Item {
 width: 240; height: 220
 property real zoom: 1
 property bool circular: false
 property url artwork
 EchoCover {
  objectName: "probe"; x: 24.13; y: 24.27; width: 80; height: 64
  scale: parent.zoom; transformOrigin: Item.TopLeft
  circular: parent.circular; source: parent.artwork; placeholderColor: "black"
 }
})",
                          QUrl());
        QScopedPointer<QObject> object(component.create());
        QVERIFY2(object, qPrintable(component.errorString()));
        auto *host = qobject_cast<QQuickItem *>(object.data());
        QVERIFY(host);
        QQuickWindow window;
        window.setColor(Qt::black);
        window.resize(240, 220);
        host->setParentItem(window.contentItem());
        host->setProperty("zoom", zoom);
        host->setProperty("circular", circular);
        host->setProperty("artwork", QUrl::fromLocalFile(path));
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QVERIFY(window.rendererInterface()->graphicsApi() != QSGRendererInterface::Software);
        auto *cover = host->findChild<QQuickItem *>("probe");
        QVERIFY(cover);
        QTRY_COMPARE(cover->property("status").toInt(), 1);
        QImage pixels = frame(window);
        QVERIFY(!pixels.isNull());
        auto *aligned = cover->findChild<QQuickItem *>("coverAlignedSurface");
        QVERIFY(aligned);
        const qreal dpr = pixels.width() / qreal(window.width());
        const QRectF scene = aligned->mapRectToScene(aligned->boundingRect());
        const QRectF box(scene.topLeft() * dpr, scene.size() * dpr);
        const qreal crop = std::max(aligned->width() / sourceSize.width(),
                                    aligned->height() / sourceSize.height());
        for (qreal v : {.375, .625})
            for (qreal u : {.375, .625}) {
                const QPoint sample(qFloor(box.left() + box.width() * u),
                                    qFloor(box.top() + box.height() * v));
                const qreal localX = (sample.x() + .5 - box.left()) / (zoom * dpr);
                const qreal localY = (sample.y() + .5 - box.top()) / (zoom * dpr);
                const int x = qBound(
                    0, qFloor((localX - aligned->width() / 2) / crop + sourceSize.width() / 2.),
                    sourceSize.width() - 1);
                const int y = qBound(
                    0, qFloor((localY - aligned->height() / 2) / crop + sourceSize.height() / 2.),
                    sourceSize.height() - 1);
                const QColor source = artwork.pixelColor(x, y), actual = pixels.pixelColor(sample);
                for (int channel = 0; channel < 3; ++channel) {
                    const int expected = qRound((channel == 0   ? source.red()
                                                 : channel == 1 ? source.green()
                                                                : source.blue()) *
                                                source.alphaF());
                    const int value = channel == 0   ? actual.red()
                                      : channel == 1 ? actual.green()
                                                     : actual.blue();
                    QVERIFY2(
                        qAbs(value - expected) <= 3,
                        qPrintable(QString("crop/alpha channel %1: actual %2 expected %3 at %4,%5")
                                       .arg(channel)
                                       .arg(value)
                                       .arg(expected)
                                       .arg(x)
                                       .arg(y)));
                }
            }
        // Use an opaque white source to measure every partial boundary pixel,
        // independently of source interpolation and transparent quadrants.
        artwork.fill(Qt::white);
        const auto whitePath = directory.filePath("white.png");
        QVERIFY(artwork.save(whitePath));
        host->setProperty("artwork", QUrl::fromLocalFile(whitePath));
        QTRY_COMPARE(cover->property("status").toInt(), 1);
        pixels = frame(window);
        const qreal radius =
            std::min(cover->property("cornerRadius").toReal(), aligned->height() / 2) * zoom * dpr;
        qreal error = 0, peak = 0;
        int count = 0;
        for (int y = qFloor(box.top()); y < qCeil(box.bottom()); ++y)
            for (int x = qFloor(box.left()); x < qCeil(box.right()); ++x) {
                const qreal expected = coverage(box, radius, x, y);
                if (expected <= 0 || expected >= 1)
                    continue;
                const qreal delta = qAbs(pixels.pixelColor(x, y).redF() - expected);
                error += delta;
                peak = std::max(peak, delta);
                ++count;
            }
        QVERIFY(count > 8);
        qInfo() << "rounded coverage mean/peak" << error / count << peak;
        QVERIFY(error / count < .035);
        QVERIFY(peak < .085);
        const auto captureDirectory = qEnvironmentVariable("ECHO_COVER_CAPTURES");
        if (!captureDirectory.isEmpty()) {
            QDir().mkpath(captureDirectory);
            QVERIFY(pixels.save(captureDirectory + '/' +
                                QString::fromLatin1(QTest::currentDataTag()) + ".png"));
        }
        QPointer<QQuickItem> material = cover->findChild<QQuickItem *>("coverMaskEffect");
        QVERIFY(material);
        cover->setProperty("resourceActive", false);
        QTRY_VERIFY(material.isNull());
        auto *image = cover->findChild<QQuickItem *>("coverImage");
        QVERIFY(image);
        QTRY_VERIFY(image->property("source").toUrl().isEmpty());
        QCOMPARE(warnings.size(), 0);
    }
    void resourceProbe()
    {
        const auto output = qEnvironmentVariable("ECHO_COVER_PROBE_READY");
        if (output.isEmpty())
            QSKIP("Opt-in native resource probe");
        QTemporaryDir directory;
        QImage artwork(256, 256, QImage::Format_RGB32);
        for (int y = 0; y < artwork.height(); ++y)
            for (int x = 0; x < artwork.width(); ++x)
                artwork.setPixelColor(
                    x, y, QColor((x / 5 % 2) * 255, (y / 7 % 2) * 255, ((x + y) / 11 % 2) * 255));
        const auto path = directory.filePath("artwork.png");
        QVERIFY(artwork.save(path));
        SettingsStore settings(directory.filePath("settings.ini"));
        ThemeStore theme(settings);
        theme.setMotionEnabled(false);
        QQmlEngine engine;
        theme.attachToEngine(engine);
        const auto legacy = qEnvironmentVariable("ECHO_COVER_PROBE_LEGACY");
        QQmlComponent component(&engine);
        const auto direct = qEnvironmentVariable("ECHO_COVER_PROBE_DIRECT");
        if (legacy.isEmpty() && direct.isEmpty())
            component.loadUrl(QUrl("qrc:/qt/qml/EchoMusic/Controls/EchoCover.qml"));
        else {
            const auto sourcePath = legacy.isEmpty() ? direct : legacy;
            QFile file(sourcePath);
            QVERIFY(file.open(QIODevice::ReadOnly));
            auto source = file.readAll();
            source.replace("import QtQuick\n", "import QtQuick\nimport EchoMusic.Controls\n");
            component.setData(source, QUrl::fromLocalFile(sourcePath));
        }
        QQuickWindow window;
        window.setColor(Qt::black);
        window.resize(1100, 750);
        auto graphics = window.graphicsConfiguration();
        graphics.setTimestamps(true);
        window.setGraphicsConfiguration(graphics);
        struct Timings
        {
            QMutex mutex;
            QList<qreal> submitMs, gpuMs;
            QElapsedTimer clock;
        };
        const auto timing = std::make_shared<Timings>();
        const auto before = connect(
            &window, &QQuickWindow::beforeRendering, &window, [timing] { timing->clock.start(); },
            Qt::DirectConnection);
        const auto after = connect(
            &window, &QQuickWindow::afterRendering, &window,
            [timing, &window] {
                const qreal elapsed = timing->clock.nsecsElapsed() / 1000000.;
                auto *swapchain =
                    static_cast<QRhiSwapChain *>(window.rendererInterface()->getResource(
                        &window, QSGRendererInterface::RhiSwapchainResource));
                const qreal gpu =
                    swapchain
                        ? swapchain->currentFrameCommandBuffer()->lastCompletedGpuTime() * 1000
                        : 0;
                QMutexLocker lock(&timing->mutex);
                timing->submitMs.append(elapsed);
                if (gpu > 0 && std::isfinite(gpu))
                    timing->gpuMs.append(gpu);
            },
            Qt::DirectConnection);
        int count = qEnvironmentVariableIntValue("ECHO_COVER_PROBE_COUNT");
        if (!count)
            count = 64;
        int size = qEnvironmentVariableIntValue("ECHO_COVER_PROBE_SIZE");
        if (!size)
            size = 80;
        std::vector<std::unique_ptr<QObject>> objects;
        for (int i = 0; i < count; ++i) {
            auto object = std::unique_ptr<QObject>(
                component.createWithInitialProperties({{"source", QUrl::fromLocalFile(path)}}));
            QVERIFY2(object, qPrintable(component.errorString()));
            auto *item = qobject_cast<QQuickItem *>(object.get());
            QVERIFY(item);
            item->setSize(QSizeF(size, size));
            item->setPosition(QPointF((i % 8) * (size + 4), (i / 8) * (size + 4)));
            item->setParentItem(window.contentItem());
            objects.push_back(std::move(object));
        }
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        for (const auto &object : objects)
            QTRY_COMPARE(object->property("status").toInt(), 1);
        QVERIFY(!frame(window).isNull());
        QTimer redraw;
        redraw.setInterval(16);
        connect(&redraw, &QTimer::timeout, &window, [&] { window.update(); });
        redraw.start();
        QTest::qWait(1500);
        {
            QMutexLocker lock(&timing->mutex);
            timing->submitMs.clear();
            timing->gpuMs.clear();
        }
        QFile ready(output);
        QVERIFY(ready.open(QIODevice::WriteOnly));
        ready.write(QJsonDocument(QJsonObject{{"pid", QCoreApplication::applicationPid()},
                                              {"count", count},
                                              {"size", size},
                                              {"dpr", window.effectiveDevicePixelRatio()},
                                              {"legacy", !legacy.isEmpty()}})
                        .toJson());
        ready.close();
        QTest::qWait(10000);
        redraw.stop();
        disconnect(before);
        disconnect(after);
        QMutexLocker lock(&timing->mutex);
        const auto summary = [](QList<qreal> values) {
            std::sort(values.begin(), values.end());
            return QJsonObject{
                {"samples", values.size()},
                {"p50_ms", values.isEmpty() ? 0. : values[values.size() / 2]},
                {"p95_ms", values.isEmpty()
                               ? 0.
                               : values[qMin(values.size() - 1, qsizetype(values.size() * .95))]}};
        };
        QFile timingFile(output + ".timings.json");
        QVERIFY(timingFile.open(QIODevice::WriteOnly));
        timingFile.write(QJsonDocument(QJsonObject{{"render_submission", summary(timing->submitMs)},
                                                   {"gpu", summary(timing->gpuMs)}})
                             .toJson());
    }
};
QTEST_MAIN(CoverRenderingTest)
#include "CoverRenderingTest.moc"
