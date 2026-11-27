#include "FloatingSurfacePixelsTest.h"
#include "EchoBackdropRoot.h"
#include "SettingsStore.h"
#include "ThemeStore.h"
#include <QDir>
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickRenderControl>
#include <QQuickRenderTarget>
#include <QQuickStyle>
#include <QTemporaryDir>
#include <QtQuick/private/qquickwindow_p.h>
#include <rhi/qrhi.h>
#include <QtTest>
#include <algorithm>
#include <memory>
namespace
{
// Floating surfaces observe window visibility. An embedded RenderControl owner can be
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
class SurfaceTarget
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
        const QSize logical(480, 360);
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
    ~SurfaceTarget()
    {
        window.setVisible(false);
        window.setRenderTarget({});
        control.invalidate();
    }
};
} // namespace

void FloatingSurfacePixelsTest::frostReplacesSharpBackdrop_data()
{
    QTest::addColumn<qreal>("backgroundAlpha");
    QTest::addColumn<bool>("modal");
    QTest::addColumn<qreal>("ratio");
    QTest::addColumn<bool>("dark");
    for (const qreal alpha : {0., .4, 1.})
        for (const bool modal : {false, true})
            for (const qreal ratio : {1., 1.25})
                for (const bool dark : {false, true})
                    QTest::newRow(qPrintable(QString("alpha%1-modal%2-dpr%3-dark%4")
                                                 .arg(alpha)
                                                 .arg(modal)
                                                 .arg(ratio)
                                                 .arg(dark)))
                        << alpha << modal << ratio << dark;
}
void FloatingSurfacePixelsTest::frostReplacesSharpBackdrop()
{
    QFETCH(qreal, backgroundAlpha);
    QFETCH(bool, modal);
    QFETCH(qreal, ratio);
    QFETCH(bool, dark);
    QTemporaryDir directory;
    EchoMusic::SettingsStore settings(directory.filePath("settings.ini"));
    EchoMusic::ThemeStore theme(settings);
    theme.setMode(dark ? EchoMusic::ThemeStore::Dark : EchoMusic::ThemeStore::Light);
    SurfaceTarget scene;
    QVERIFY(scene.create(ratio));
    scene.window.setColor(theme.shell());
    QQmlEngine engine;
    theme.attachToEngine(engine);
    QSignalSpy warnings(&engine, &QQmlEngine::warnings);
    QQmlComponent component(&engine);
    component.setData(R"(
import QtQuick
import EchoMusic.Controls
import EchoMusic.Theme
Item {
    id: host
    width: 480; height: 360
    property real backgroundAlpha: 0
    property bool modal: false
    property bool dark: true
    property color stripeColor: dark ? "white" : "black"
    property alias surface: surface
    EchoBackdropRoot {
        anchors.fill: parent
        Rectangle { anchors.fill: parent; color: Qt.rgba(host.dark ? .12 : .88, host.dark ? .12 : .88, host.dark ? .12 : .88, host.backgroundAlpha) }
        Repeater {
            model: 100
            Rectangle { required property int index; x: 120 + index * 2; y: 110; width: 1; height: 70; color: host.stripeColor }
        }
        Text { x: 110; y: 205; text: "Background text behind glass"; color: host.dark ? "white" : "black"; font.pixelSize: 18 }
    }
    EchoFloatingSurface {
        id: surface
        x: 70; y: 50; width: 330; height: 260
        color: host.dark ? "#303030" : "#eeeeee"; radius: 10; border.width: 0
        frostedEnabled: true; shadowEnabled: false; dialogShadow: host.modal
    }
})",
                      QUrl());
    QScopedPointer<QObject> object(component.createWithInitialProperties(
        {{"backgroundAlpha", backgroundAlpha}, {"modal", modal}, {"dark", dark}}));
    QVERIFY2(object, qPrintable(component.errorString()));
    auto *root = qobject_cast<QQuickItem *>(object.data());
    QVERIFY(root);
    root->setParentItem(scene.window.contentItem());
    auto *surface = root->property("surface").value<QQuickItem *>();
    QVERIFY(surface);
    for (int frame = 0; frame < 6; ++frame) {
        QCoreApplication::processEvents();
        scene.read();
    }
    QVERIFY(surface->property("hasBackdrop").toBool());
    const auto image = scene.read();
    QVERIFY(!image.isNull());
    // A blur must remove 1px stripes, not overlay a low-frequency halo
    // while the unblurred original remains visible through capture alpha.
    int peakDifference = 0;
    for (int x = qRound(180 * ratio); x < qRound(260 * ratio); ++x) {
        const int y = qRound(145 * ratio);
        // Second differences reject the slow blurred color gradient and
        // retain the alternating sharp strokes of the original scene.
        const int curvature = qGray(image.pixel(x, y)) - 2 * qGray(image.pixel(x + 1, y)) +
                              qGray(image.pixel(x + 2, y));
        peakDifference = std::max(peakDifference, std::abs(curvature) / 2);
    }
    const auto captures = qEnvironmentVariable("ECHO_FROST_CAPTURE_DIR");
    if (!captures.isEmpty()) {
        QVERIFY(QDir().mkpath(captures));
        QVERIFY(image.save(captures + QString("/alpha%1-modal%2-dpr%3-dark%4.png")
                                          .arg(backgroundAlpha)
                                          .arg(modal)
                                          .arg(ratio)
                                          .arg(dark)));
    }
    qInfo() << "Sharp backdrop residual" << peakDifference;
    QVERIFY2(peakDifference <= 2,
             qPrintable(QString("Unblurred stripe contrast remains: %1").arg(peakDifference)));
    int contrast = 0;
    const int baseline = qGray(image.pixel(qRound(210 * ratio), qRound(285 * ratio)));
    for (int y = qRound(90 * ratio); y < qRound(250 * ratio); ++y)
        for (int x = qRound(110 * ratio); x < qRound(330 * ratio); ++x)
            contrast = std::max(contrast, std::abs(qGray(image.pixel(x, y)) - baseline));
    QVERIFY2(contrast > 5,
             "The captured scene must still contribute blurred light, not a flat opaque fill");
    root->setProperty("stripeColor", QColor("#4488ff"));
    for (int frame = 0; frame < 6; ++frame) {
        QCoreApplication::processEvents();
        scene.read();
    }
    QVERIFY(scene.read() != image);
    root->setProperty("stripeColor", QColor(dark ? Qt::white : Qt::black));
    for (int frame = 0; frame < 6; ++frame) {
        QCoreApplication::processEvents();
        scene.read();
    }
    QCOMPARE(scene.read(), image);
    surface->setVisible(false);
    QCoreApplication::processEvents();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    const auto plain = scene.read();
    QVERIFY(!surface->property("hasBackdrop").toBool());
    QVERIFY(surface->findChildren<EchoMusic::EchoBackdropSampler *>().isEmpty());
    for (const QPointF point : {QPointF(60, 145), QPointF(70, 50), QPointF(405, 145)}) {
        const auto pixel = (point * ratio).toPoint();
        QCOMPARE(image.pixelColor(pixel), plain.pixelColor(pixel));
    }
    surface->setVisible(true);
    for (int frame = 0; frame < 6; ++frame) {
        QCoreApplication::processEvents();
        scene.read();
    }
    QVERIFY(surface->property("hasBackdrop").toBool());
    QCOMPARE(scene.read(), image);
    QVERIFY(warnings.isEmpty());
    QVERIFY(!scene.window.handle());
}
int main(int argc, char **argv)
{
    QQuickWindow::setSceneGraphBackend("rhi");
    QGuiApplication app(argc, argv);
    QQuickStyle::setStyle("Basic");
    FloatingSurfacePixelsTest test;
    return QTest::qExec(&test, argc, argv);
}
