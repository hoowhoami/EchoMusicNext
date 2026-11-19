#include "GlyphRun.h"
#include "GlyphRunCanvas_p.h"
#include <QTextLayout>
#include <QGlyphRun>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QQuickRenderControl>
#include <QQuickRenderTarget>
#include <QResource>
#include <QtQuick/private/qquickwindow_p.h>
#include <QtTest>
#include <rhi/qrhi.h>
#include <cmath>
#include <array>
#include <algorithm>

namespace
{
// GlyphRun observes window visibility. An embedded RenderControl owner can be
// visible without creating a platform window or presenting a swapchain.
class EmbeddedWindowState : public QQuickWindowPrivate
{
public:
    void setVisible(bool value) override
    {
        if (visible == value)
            return;
        visible = value;
        visibility = value ? QWindow::Windowed : QWindow::Hidden;
        emit q_func() -> visibleChanged(value);
        emit q_func() -> visibilityChanged(visibility);
    }
};
class EmbeddedWindow : public QQuickWindow
{
public:
    explicit EmbeddedWindow(QQuickRenderControl *control)
        : QQuickWindow(*new EmbeddedWindowState, control)
    {
    }
};
class GlyphTarget
{
public:
    QQuickRenderControl control;
    EmbeddedWindow window{&control};
    std::unique_ptr<QRhiTexture> color;
    std::unique_ptr<QRhiRenderBuffer> depth;
    std::unique_ptr<QRhiTextureRenderTarget> target;
    std::unique_ptr<QRhiRenderPassDescriptor> pass;
    bool create(qreal ratio)
    {
        const QSize logical(256, 144);
        const QSize pixels = logical * ratio;
        window.resize(logical);
        window.contentItem()->setSize(logical);
        window.setColor(Qt::transparent);
        if (!control.initialize())
            return false;
        auto *rhi = control.rhi();
        color.reset(rhi->newTexture(QRhiTexture::RGBA8, pixels, 1,
                                    QRhiTexture::RenderTarget | QRhiTexture::UsedAsTransferSource));
        depth.reset(rhi->newRenderBuffer(QRhiRenderBuffer::DepthStencil, pixels, 1));
        if (!color->create() || !depth->create())
            return false;
        target.reset(rhi->newTextureRenderTarget({QRhiColorAttachment(color.get()), depth.get()}));
        pass.reset(target->newCompatibleRenderPassDescriptor());
        target->setRenderPassDescriptor(pass.get());
        if (!target->create())
            return false;
        auto output = QQuickRenderTarget::fromRhiRenderTarget(target.get());
        output.setDevicePixelRatio(ratio);
        window.setRenderTarget(output);
        window.setVisible(true);
        return !window.handle();
    }
    QImage read()
    {
        control.polishItems();
        control.beginFrame();
        control.sync();
        control.render();
        QRhiReadbackResult result;
        auto *updates = control.rhi()->nextResourceUpdateBatch();
        updates->readBackTexture(color.get(), &result);
        control.commandBuffer()->resourceUpdate(updates);
        control.endFrame();
        if (result.data.isEmpty())
            return {};
        QImage image(reinterpret_cast<const uchar *>(result.data.constData()),
                     result.pixelSize.width(), result.pixelSize.height(),
                     QImage::Format_RGBA8888_Premultiplied);
        return control.rhi()->isYUpInFramebuffer() ? image.flipped(Qt::Vertical) : image.copy();
    }
    ~GlyphTarget()
    {
        window.setVisible(false);
        window.setRenderTarget({});
        control.invalidate();
    }
};
} // namespace

class GlyphRunCanvasPixelsTest : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase()
    {
        const auto computeReference = qEnvironmentVariable("ECHO_GLYPH_REFERENCE_COMPUTE_RCC");
        if (!computeReference.isEmpty()) {
            Q_CLEANUP_RESOURCE(echo_plugin_glyph_canvas_compute_shaders);
            QVERIFY(QResource::registerResource(computeReference));
            QFile selected(":/qt/qml/EchoMusic/PluginSdk/shaders/glyph-canvas/vertical.comp.qsb");
            QVERIFY(selected.open(QIODevice::ReadOnly));
            QCOMPARE(selected.readAll(), QByteArray("unsupported-compute-test"));
        }
        const auto reference = qEnvironmentVariable("ECHO_GLYPH_REFERENCE_RCC");
        if (reference.isEmpty())
            return;
        Q_CLEANUP_RESOURCE(echo_plugin_glyph_canvas_shaders);
        QVERIFY(QResource::registerResource(reference));
        QFile expected(qEnvironmentVariable("ECHO_GLYPH_REFERENCE_QSB"));
        QFile selected(":/qt/qml/EchoMusic/PluginSdk/shaders/glyph-canvas/blur.frag.qsb");
        QVERIFY(expected.open(QIODevice::ReadOnly));
        QVERIFY(selected.open(QIODevice::ReadOnly));
        QCOMPARE(selected.readAll(), expected.readAll());
    }
    void cleanupTestCase()
    {
        QTRY_COMPARE_WITH_TIMEOUT(EchoMusic::GlyphCanvas::Memory::total(), qint64(0), 5000);
    }
    void inkAndDenseSharedKernels()
    {
        GlyphTarget scene;
        QVERIFY(scene.create(1));
        auto *rhi = scene.control.rhi();
        qInfo() << "Grouped convolution device prerequisites" << rhi->backend()
                << rhi->isFeatureSupported(QRhi::Compute)
                << rhi->isTextureFormatSupported(QRhiTexture::RGBA16F,
                                                 QRhiTexture::RenderTarget |
                                                     QRhiTexture::UsedWithLoadStore)
                << rhi->isTextureFormatSupported(QRhiTexture::RGBA8,
                                                 QRhiTexture::RenderTarget |
                                                     QRhiTexture::UsedWithLoadStore)
                << rhi->isYUpInFramebuffer() << rhi->resourceLimit(QRhi::MaxThreadsPerThreadGroup)
                << rhi->resourceLimit(QRhi::MaxThreadGroupY);
        EchoMusic::GlyphRun glyph(scene.window.contentItem());
        glyph.setSize(scene.window.size());
        QFont font;
        font.setPixelSize(6);
        glyph.setFont(font);
        glyph.setMaximumRasterScale(2);
        glyph.setHaloSampling("canvas");
        glyph.setRuns(
            {QVariantMap{{"key", "ink"}, {"text", "I"}, {"maxBlur", 0}, {"maxHaloRadius", 40}}});
        QTRY_VERIFY2_WITH_TIMEOUT(!glyph.preparing() && !glyph.metrics().isEmpty(),
                                  qPrintable(glyph.errorString()), 5000);
        const auto pose = [](int i, bool halo) {
            QVariantMap value{{"key", "ink"},
                              {"instance", QString::number(i)},
                              {"x", 4. + (i % 32) * 7.25},
                              {"y", 12. + (i / 32) * 7.5},
                              {"color", QColor(Qt::white)}};
            if (halo)
                value["halos"] =
                    QVariantList{QVariantMap{{"radius", 1.}, {"color", QColor(Qt::white)}}};
            return value;
        };
        // Ink-only nodes still bind a valid kernel range, without a blur packet.
        glyph.setPoses({pose(0, false)});
        scene.read();
        auto image = scene.read();
        QVERIFY2(!image.isNull() && glyph.errorString().isEmpty(), qPrintable(glyph.errorString()));
        const auto visiblePixels = [](const QImage &input) {
            const auto rgba = input.convertToFormat(QImage::Format_ARGB32_Premultiplied);
            int count = 0;
            for (int y = 0; y < rgba.height(); ++y)
                for (int x = 0; x < rgba.width(); ++x)
                    count += qAlpha(rgba.pixel(x, y)) > 2;
            return count;
        };
        QVERIFY(visiblePixels(image) > 0);
        // Distinct positions require separate halo passes but share one kernel.
        QVariantList poses;
        for (int i = 0; i < 512; ++i)
            poses.append(pose(i, true));
        glyph.setPoses(poses);
        scene.read();
        image = scene.read();
        QVERIFY2(!image.isNull() && glyph.errorString().isEmpty(), qPrintable(glyph.errorString()));
        QVERIFY(visiblePixels(image) > 1000);
        glyph.setPoses({pose(0, false)});
        scene.read();
        image = scene.read();
        QVERIFY2(!image.isNull() && glyph.errorString().isEmpty(), qPrintable(glyph.errorString()));
        QVERIFY(visiblePixels(image) < 100);
        for (int cycle = 0; cycle < 3; ++cycle) {
            glyph.setVisible(false);
            scene.read();
            QTRY_COMPARE_WITH_TIMEOUT(glyph.gpuCacheBytes(), qint64(0), 5000);
            glyph.setVisible(true);
            QTRY_VERIFY_WITH_TIMEOUT(!glyph.preparing() && !glyph.metrics().isEmpty(), 5000);
            glyph.setPoses({pose(0, true)});
            scene.read();
            image = scene.read();
            QVERIFY2(!image.isNull() && glyph.errorString().isEmpty(),
                     qPrintable(glyph.errorString()));
            QVERIFY(visiblePixels(image) > 0);
        }
        QVERIFY(!scene.window.handle());
    }
    void atlasChannelsRemainIndependent()
    {
        GlyphTarget scene;
        QVERIFY(scene.create(1));
        EchoMusic::GlyphRun glyph(scene.window.contentItem());
        glyph.setSize(scene.window.size());
        QFont font("Arial");
        font.setPixelSize(400);
        glyph.setFont(font);
        glyph.setMaximumRasterScale(1);
        glyph.setHaloSampling("canvas");
        glyph.setRuns(
            {QVariantMap{{"key", "wide"}, {"text", "W"}, {"maxBlur", 0}, {"maxHaloRadius", 40}}});
        QTRY_VERIFY2_WITH_TIMEOUT(!glyph.preparing() && !glyph.metrics().isEmpty(),
                                  qPrintable(glyph.errorString()), 5000);
        QTextLayout layout("W", font);
        layout.beginLayout();
        auto line = layout.createLine();
        line.setLineWidth(10000);
        layout.endLayout();
        const auto bounds = layout.glyphRuns(0, 1).first().boundingRect();
        // QGlyphRun's complete native box, rather than its tight ink box,
        // determines these sigma4 fields (no pyramid,13px border and gutter).
        QCOMPARE(scene.window.effectiveDevicePixelRatio(), 1.);
        const int tileWidth = qFloor(bounds.width()) + 28;
        const int tileHeight = qFloor(bounds.height()) + 28;
        const int perChannel = (1024 / tileWidth) * (1024 / tileHeight);
        QVERIFY(tileWidth > 256 && perChannel > 0 && perChannel < 30);
        qInfo() << "Four-channel field lower bound" << tileWidth << tileHeight << perChannel;
        const int count = 3 * perChannel + 1;
        const std::array<QColor, 4> colors{Qt::red, Qt::green, Qt::blue, Qt::white};
        QVariantList poses;
        for (int i = 0; i < count; ++i)
            poses.append(QVariantMap{
                {"key", "wide"},
                {"instance", QString::number(i)},
                {"x", 128. - bounds.center().x() + i * .125},
                {"y", 72. - bounds.center().y() + i * .25},
                {"color", QColor(Qt::transparent)},
                {"halos", QVariantList{QVariantMap{
                              {"radius", 4.}, {"opacity", .125}, {"color", colors[i % 4]}}}}});
        for (const auto &name : {QString("fields-forward.png"), QString("fields-reversed.png")}) {
            glyph.setPoses(poses);
            scene.read();
            QCoreApplication::processEvents();
            QVERIFY2(glyph.errorString().isEmpty(), qPrintable(glyph.errorString()));
            const auto image = scene.read().convertToFormat(QImage::Format_ARGB32_Premultiplied);
            QVERIFY2(!image.isNull() && glyph.errorString().isEmpty(),
                     qPrintable(glyph.errorString()));
            int colored = 0;
            for (int y = 0; y < image.height(); ++y)
                for (int x = 0; x < image.width(); ++x)
                    colored += qRed(image.pixel(x, y)) > 2 && qGreen(image.pixel(x, y)) > 2 &&
                               qBlue(image.pixel(x, y)) > 2;
            QVERIFY(colored > 1000);
            const auto dump = qEnvironmentVariable("ECHO_GLYPH_PIXEL_DUMP");
            if (!dump.isEmpty()) {
                QVERIFY(QDir().mkpath(dump));
                QVERIFY(image.save(QDir(dump).filePath(name)));
            }
            const auto reference = qEnvironmentVariable("ECHO_GLYPH_FIELD_REFERENCE_IMAGES");
            if (!reference.isEmpty()) {
                const auto expected = QImage(QDir(reference).filePath(name))
                                          .convertToFormat(QImage::Format_ARGB32_Premultiplied);
                QCOMPARE(image.size(), expected.size());
                int maximum = 0;
                for (int y = 0; y < image.height(); ++y)
                    for (int x = 0; x < image.width(); ++x) {
                        const auto a = image.pixel(x, y), b = expected.pixel(x, y);
                        maximum =
                            std::max({maximum, qAbs(qRed(a) - qRed(b)), qAbs(qGreen(a) - qGreen(b)),
                                      qAbs(qBlue(a) - qBlue(b)), qAbs(qAlpha(a) - qAlpha(b))});
                    }
                qInfo() << name << "Maximum four-channel reference difference" << maximum;
                QVERIFY(maximum <= 2);
            }
            std::reverse(poses.begin(), poses.end());
        }
        QVERIFY(!scene.window.handle());
    }
    void kernelSampling_data()
    {
        QTest::addColumn<double>("radius");
        QTest::addColumn<double>("ratio");
        for (int window = 1; window <= 85; ++window)
            QTest::newRow(qPrintable(QString("window-%1").arg(window)))
                << window / (3 * std::sqrt(2 * M_PI) / 4) << 1.;
        for (const double radius : {18., 48., 95., 160.})
            for (const double ratio : {1.25, 1.5, 2.})
                QTest::newRow(qPrintable(QString("pyramid-%1-dpr-%2").arg(radius).arg(ratio)))
                    << radius << ratio;
    }
    void kernelSampling()
    {
        QFETCH(double, radius);
        QFETCH(double, ratio);
        GlyphTarget scene;
        const bool initialized = scene.create(ratio);
#ifndef Q_OS_MACOS
        if (!initialized && !scene.control.rhi())
            QSKIP("No offscreen RHI device available");
#endif
        QVERIFY(initialized);
        EchoMusic::GlyphRun glyph(scene.window.contentItem());
        glyph.setSize(scene.window.size());
        QFont font;
        font.setPixelSize(6);
        glyph.setFont(font);
        glyph.setMaximumRasterScale(2);
        glyph.setHaloSampling("canvas");
        glyph.setRuns(
            {QVariantMap{{"key", "thin"}, {"text", "Il"}, {"maxBlur", 0}, {"maxHaloRadius", 512}}});
        QTRY_VERIFY2_WITH_TIMEOUT(!glyph.preparing() && !glyph.metrics().isEmpty(),
                                  qPrintable(glyph.errorString()), 5000);
        const auto pose = [radius](double x, double y) {
            return QVariantMap{{"key", "thin"},
                               {"instance", QString::number(x)},
                               {"x", x},
                               {"y", y},
                               {"color", QColor(Qt::transparent)},
                               {"halos", QVariantList{QVariantMap{{"radius", radius},
                                                                  {"color", QColor(Qt::white)}}}}};
        };
        auto warm = pose(29.5, 31.25);
        warm["halos"] = QVariantList{QVariantMap{{"radius", std::min(160., radius * 1.8 + 3)},
                                                 {"color", QColor(Qt::white)}}};
        glyph.setPoses({warm, pose(217.75, -2.5)});
        scene.read();
        scene.read();
        glyph.setPoses({pose(127.25, 70.5), pose(-1.25, 142.75)});
        scene.read();
        const auto image = scene.read().convertToFormat(QImage::Format_ARGB32_Premultiplied);
        QVERIFY2(!image.isNull() && glyph.errorString().isEmpty(), qPrintable(glyph.errorString()));
        const auto name =
            QString("kernel-%1.png").arg(QString::fromLatin1(QTest::currentDataTag()));
        const auto dump = qEnvironmentVariable("ECHO_GLYPH_PIXEL_DUMP");
        if (!dump.isEmpty()) {
            QVERIFY(QDir().mkpath(dump));
            QVERIFY(image.save(QDir(dump).filePath(name)));
        }
        const auto reference = qEnvironmentVariable("ECHO_GLYPH_REFERENCE_IMAGES");
        if (!reference.isEmpty()) {
            const auto expected = QImage(QDir(reference).filePath(name))
                                      .convertToFormat(QImage::Format_ARGB32_Premultiplied);
            QCOMPARE(image.size(), expected.size());
            int maximum = 0;
            for (int y = 0; y < image.height(); ++y)
                for (int x = 0; x < image.width(); ++x) {
                    const auto a = image.pixel(x, y), b = expected.pixel(x, y);
                    maximum =
                        std::max({maximum, qAbs(qRed(a) - qRed(b)), qAbs(qGreen(a) - qGreen(b)),
                                  qAbs(qBlue(a) - qBlue(b)), qAbs(qAlpha(a) - qAlpha(b))});
                }
            qInfo() << "Maximum reference channel difference" << maximum;
            QVERIFY(maximum <= 2);
        }
        QVERIFY(!scene.window.handle());
    }
    void clippedAndSharedHalos_data()
    {
        QTest::addColumn<double>("ratio");
        QTest::addColumn<double>("radius");
        QTest::addColumn<double>("rotation");
        QTest::newRow("box") << 1. << 4. << 0.;
        QTest::newRow("reduced-storage") << 1. << 12. << 0.;
        QTest::newRow("outline-pyramid") << 1.5 << 48. << 0.;
        QTest::newRow("rotated") << 1.25 << 18. << 17.;
    }
    void clippedAndSharedHalos()
    {
        QFETCH(double, ratio);
        QFETCH(double, radius);
        QFETCH(double, rotation);
        GlyphTarget scene;
        const bool initialized = scene.create(ratio);
#ifndef Q_OS_MACOS
        if (!initialized && !scene.control.rhi())
            QSKIP("No offscreen RHI device available");
#endif
        QVERIFY(initialized);
        EchoMusic::GlyphRun glyph(scene.window.contentItem());
        glyph.setSize(scene.window.size());
        glyph.setTransformOrigin(QQuickItem::TopLeft);
        glyph.setRotation(rotation);
        QFont font;
        font.setPixelSize(36);
        glyph.setFont(font);
        glyph.setMaximumRasterScale(2);
        glyph.setHaloSampling("canvas");
        glyph.setRuns({QVariantMap{
            {"key", "ink"}, {"text", "office MMM"}, {"maxBlur", 0}, {"maxHaloRadius", 160}}});
        QTRY_VERIFY2_WITH_TIMEOUT(!glyph.preparing() && !glyph.metrics().isEmpty(),
                                  qPrintable(glyph.errorString()), 5000);
        const auto paint = [radius](const QString &id, QColor color) {
            return QVariantMap{
                {"key", "ink"},
                {"instance", id},
                {"x", 24.},
                {"y", 44.},
                {"color", QColor(Qt::transparent)},
                {"halos", QVariantList{QVariantMap{{"radius", radius}, {"color", color}}}}};
        };
        auto red = paint("left", Qt::red), blue = paint("right", Qt::blue);
        const auto capture = [&](QVariantList poses) {
            glyph.setPoses(poses);
            return scene.read().convertToFormat(QImage::Format_ARGB32_Premultiplied);
        };
        // The first pass prepares curve resources for the render target.
        capture({red});
        const auto whole = capture({red});
        QVERIFY2(!whole.isNull() && glyph.errorString().isEmpty(), qPrintable(glyph.errorString()));
        red["clip"] = QVariantMap{{"x", 18.5}, {"y", -20.}, {"width", 56.}, {"height", 100.}};
        blue["clip"] = QVariantMap{{"x", 50.5}, {"y", -20.}, {"width", 66.}, {"height", 100.}};
        const auto first = capture({red}), second = capture({blue});
        const auto combined = capture({red, blue});
        const auto reversed = capture({blue, red});
        for (const auto *image : {&first, &second, &combined, &reversed})
            QCOMPARE(image->size(), whole.size());
        const auto dump = qEnvironmentVariable("ECHO_GLYPH_PIXEL_DUMP");
        if (!dump.isEmpty()) {
            const QDir output(dump);
            QVERIFY(output.mkpath("."));
            const QString prefix = QString::fromLatin1(QTest::currentDataTag()) + "-";
            for (const auto &[name, image] :
                 std::initializer_list<std::pair<QString, QImage>>{{"full", whole},
                                                                   {"left", first},
                                                                   {"right", second},
                                                                   {"combined", combined},
                                                                   {"reversed", reversed}})
                QVERIFY(image.save(output.filePath(prefix + name + ".png")));
        }
        int covered = 0, overlapping = 0, maxError = 0, insideError = 0, outsideError = 0;
        for (int y = 0; y < whole.height(); ++y) {
            for (int x = 0; x < whole.width(); ++x) {
                const auto a = first.pixel(x, y), b = second.pixel(x, y);
                const auto both = combined.pixel(x, y), reverse = reversed.pixel(x, y);
                const auto local = glyph.mapFromScene(QPointF((x + .5) / ratio, (y + .5) / ratio));
                const double localX = local.x() - 24., localY = local.y() - 44.;
                if (localX > 20 && localX < 73 && localY > -18 && localY < 78) {
                    insideError = qMax(insideError, qAbs(qRed(whole.pixel(x, y)) - qRed(a)));
                    covered += qRed(a) > 1;
                }
                if (localX < 16 || localX > 77)
                    outsideError = qMax(outsideError, qRed(a));
                overlapping += qAlpha(a) > 2 && qAlpha(b) > 2;
                maxError =
                    std::max({maxError, qAbs(qBlue(both) - qBlue(b)),
                              qAbs(qRed(both) - qRound(qRed(a) * (1 - qAlpha(b) / 255.))),
                              qAbs(qRed(reverse) - qRed(a)),
                              qAbs(qBlue(reverse) - qRound(qBlue(b) * (1 - qAlpha(a) / 255.)))});
            }
        }
        QVERIFY2(covered > 100 && overlapping > 30, "Expected visible ink and overlapping halos");
        qInfo() << "Maximum channel differences inside/outside/overlap" << insideError
                << outsideError << maxError;
        QVERIFY(insideError <= 2 && outsideError <= 2 && maxError <= 2);
        QVERIFY(!scene.window.handle());
    }
};
int main(int argc, char **argv)
{
    QQuickWindow::setSceneGraphBackend("rhi");
    QGuiApplication app(argc, argv);
    GlyphRunCanvasPixelsTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "GlyphRunCanvasPixelsTest.moc"
