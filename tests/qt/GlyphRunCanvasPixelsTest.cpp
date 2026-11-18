#include "GlyphRun.h"
#include <QDir>
#include <QGuiApplication>
#include <QQuickRenderControl>
#include <QQuickRenderTarget>
#include <QtQuick/private/qquickwindow_p.h>
#include <QtTest>
#include <rhi/qrhi.h>

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
