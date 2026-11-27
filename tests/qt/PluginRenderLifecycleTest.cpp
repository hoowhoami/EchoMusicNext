#include "PluginRenderLifecycleTest.h"
#include "FloatTexture.h"
#include "plugins/PluginRuntimeTestFixture.h"
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickRenderControl>
#include <QQuickRenderTarget>
#include <QSGTextureProvider>
#include <QtQuick/private/qquickitem_p.h>
#include <QtQuick/private/qquickwindow_p.h>
#include <QtQuick/private/qsgrhishadereffectnode_p.h>
#include <rhi/qrhi.h>
#include <QtTest>

namespace
{
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
class Target
{
public:
    QQuickRenderControl control;
    EmbeddedWindow window{&control};
    std::unique_ptr<QRhiTexture> color;
    std::unique_ptr<QRhiRenderBuffer> depth;
    std::unique_ptr<QRhiTextureRenderTarget> target;
    std::unique_ptr<QRhiRenderPassDescriptor> pass;
    bool create()
    {
        const QSize size(64, 64);
        window.resize(size);
        window.contentItem()->setSize(size);
        window.setColor(Qt::transparent);
        if (!control.initialize())
            return false;
        auto *rhi = control.rhi();
        color.reset(rhi->newTexture(QRhiTexture::RGBA8, size, 1,
                                    QRhiTexture::RenderTarget | QRhiTexture::UsedAsTransferSource));
        depth.reset(rhi->newRenderBuffer(QRhiRenderBuffer::DepthStencil, size, 1));
        if (!color->create() || !depth->create())
            return false;
        target.reset(rhi->newTextureRenderTarget({QRhiColorAttachment(color.get()), depth.get()}));
        pass.reset(target->newCompatibleRenderPassDescriptor());
        target->setRenderPassDescriptor(pass.get());
        if (!target->create())
            return false;
        window.setRenderTarget(QQuickRenderTarget::fromRhiRenderTarget(target.get()));
        window.setVisible(true);
        return !window.handle();
    }
    QImage read()
    {
        QCoreApplication::processEvents();
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
    ~Target()
    {
        window.setVisible(false);
        window.setRenderTarget({});
        control.invalidate();
    }
};
} // namespace

void PluginRenderLifecycleTest::retainedSceneGraphKeepsFloatTextureProvider()
{
    QQmlEngine engine;
    Target scene;
    QVERIFY(scene.create());
    QQmlComponent component(&engine);
    component.setData(R"(
import QtQuick
import EchoMusic.PluginSdk
Item {
    width: 64; height: 64
    FloatTexture { id: data; objectName: "data"; width: 1; height: 1 }
    ShaderEffect {
        objectName: "effect"; width: 64; height: 64
        property var source: data
        fragmentShader: "qrc:/test-render/lifecycle/quick-float.frag.qsb"
    }
})",
                      QUrl("qrc:/retained-provider.qml"));
    std::unique_ptr<QObject> object(component.create());
    QVERIFY2(object, qPrintable(component.errorString()));
    auto *root = qobject_cast<QQuickItem *>(object.get());
    auto *data = root->findChild<EchoMusic::FloatTexture *>("data");
    auto *effect = root->findChild<QQuickItem *>("effect");
    QVERIFY(data && effect);
    root->setParentItem(scene.window.contentItem());
    QTRY_VERIFY(!scene.read().isNull() && QQuickItemPrivate::get(effect)->paintNode);
    auto material = [&] {
        auto *node = static_cast<QSGGeometryNode *>(QQuickItemPrivate::get(effect)->paintNode);
        return static_cast<QSGRhiShaderEffectMaterial *>(node->material());
    };
    QPointer<QSGTextureProvider> provider = material()->m_textureProviders.at(1);
    QVERIFY(provider);
    for (int cycle = 0; cycle < 16; ++cycle) {
        // Exercise the render-thread stop boundary while Qt retains its nodes.
        // RenderControl keeps this test entirely offscreen, without a platform window.
        QVERIFY(QMetaObject::invokeMethod(&scene.window, "sceneGraphAboutToStop",
                                          Qt::DirectConnection));
        data->update();
        QVERIFY(!scene.read().isNull());
        QCOMPARE(material()->m_textureProviders.at(1), provider.data());
        QVERIFY2(provider, "Stopping presentation must not orphan a retained sampler");
    }
    scene.control.invalidate();
    QVERIFY(provider.isNull());
    QVERIFY(!scene.window.handle());
}
void PluginRenderLifecycleTest::nativeMaterialsKeepPixelsAcrossSourceAndShaderReplacement()
{
    QQmlEngine engine;
    Target scene;
    QVERIFY(scene.create());
    QQmlComponent component(&engine);
    component.setData(R"(
import QtQuick
import EchoMusic.PluginSdk
Rectangle {
    width: 64; height: 64; color: "#334d66"
    property string composition: "source-over"
    property bool sampled: false
    property color textureColor: "red"
    Rectangle { id: ink; width: 8; height: 8; color: parent.textureColor }
    ShaderEffectSource {
        id: capture; sourceItem: ink; hideSource: true
        textureSize: Qt.size(8, 8); visible: false
    }
    Repeater {
        model: 4
        NativeShaderEffect {
            required property int index
            x: index * 16; width: 16; height: 64
            composition: parent.composition
            property vector4d ink: Qt.vector4d(.1, .2, .3, .5)
            property var source: capture
            fragmentShader: parent.sampled
                ? "qrc:/test-render/lifecycle/quick-float.frag.qsb"
                : "qrc:/test-render/lifecycle/quick-color.frag.qsb"
        }
    }
})",
                      QUrl("qrc:/material-lifecycle.qml"));
    const QColor background("#334d66");
    const auto closeColor = [](QColor actual, float r, float g, float b) {
        return qAbs(actual.red() - qRound(r * 255)) <= 2 &&
               qAbs(actual.green() - qRound(g * 255)) <= 2 &&
               qAbs(actual.blue() - qRound(b * 255)) <= 2 && actual.alpha() == 255;
    };
    for (int cycle = 0; cycle < 16; ++cycle) {
        std::unique_ptr<QObject> object(component.create());
        QVERIFY2(object, qPrintable(component.errorString()));
        auto *root = qobject_cast<QQuickItem *>(object.get());
        root->setParentItem(scene.window.contentItem());
        for (const auto &mode : {"source-over", "screen", "additive", "max", "exclusion"}) {
            root->setProperty("composition", mode);
            const auto blend = [&](float source, float destination) {
                if (QByteArray(mode) == "max")
                    return std::max(source, destination);
                if (QByteArray(mode) == "exclusion")
                    return source * (1 - destination) + destination * (1 - source);
                return source + destination * (QByteArray(mode) == "additive" ? 1.f
                                               : QByteArray(mode) == "screen" ? 1 - source
                                                                              : .5f);
            };
            QImage image;
            QTRY_VERIFY(!(image = scene.read()).isNull() &&
                        closeColor(image.pixelColor(8, 32), blend(.1f, background.redF()),
                                   blend(.2f, background.greenF()),
                                   blend(.3f, background.blueF())));
            for (int x : {8, 24, 40, 56})
                QVERIFY(closeColor(image.pixelColor(x, 32), blend(.1f, background.redF()),
                                   blend(.2f, background.greenF()),
                                   blend(.3f, background.blueF())));
        }
        root->setProperty("composition", "source-over");
        root->setProperty("sampled", true);
        for (const auto &color : {QColor(Qt::red), QColor(Qt::blue)}) {
            root->setProperty("textureColor", color);
            QImage image;
            QTRY_VERIFY(
                !(image = scene.read()).isNull() &&
                closeColor(image.pixelColor(8, 32), (color.redF() + 1) / 2, 0, color.blueF()));
            for (int x : {8, 24, 40, 56})
                QVERIFY(
                    closeColor(image.pixelColor(x, 32), (color.redF() + 1) / 2, 0, color.blueF()));
        }
        object.reset();
        QVERIFY(!scene.read().isNull());
    }
    QVERIFY(!scene.window.handle());
}
void PluginRenderLifecycleTest::floatTextureUploadsRecoverAndDiscardRetiredReports()
{
    EchoMusic::PluginTest::Host host;
    const auto package = EchoMusic::PluginTest::package(
        host.temporary.path(), "export function activate(c) {}",
        {"lifecycle", "graphics.rhi", "graphics.floatTexture", "graphics.floatTexture.base64"},
        {"graphics"});
    host.runtime.importDirectory(package);
    QTRY_COMPARE(host.runtime.state(host.installation()), EchoMusic::PluginRuntime::Active);
    auto *graphics = host.runtime.context(host.installation())->graphics();
    const auto budget = graphics->memoryBudget();
    Target scene;
    QVERIFY(scene.create());
    QQmlComponent component(&host.engine);
    component.setData(R"(
import QtQuick
import EchoMusic.PluginSdk
Item {
    width: 64; height: 64
    FloatTexture { id: data; objectName: "data"; width: 1; height: 1 }
    NativeShaderEffect {
        width: 64; height: 64
        property var source: data
        fragmentShader: "qrc:/test-render/lifecycle/quick-float.frag.qsb"
    }
})",
                      QUrl("qrc:/float-upload-lifecycle.qml"));
    const auto encoded = [](int width, float red) {
        const float values[] = {red, 2.f, .25f, 1.f};
        QByteArray bytes;
        for (int pixel = 0; pixel < width; ++pixel)
            for (const float value : values) {
                quint32 bits;
                memcpy(&bits, &value, sizeof(bits));
                const auto offset = bytes.size();
                bytes.resize(offset + sizeof(bits));
                qToLittleEndian(bits, bytes.data() + offset);
            }
        return QString::fromLatin1(bytes.toBase64());
    };
    for (int cycle = 0; cycle < 16; ++cycle) {
        std::unique_ptr<QObject> object(component.create());
        QVERIFY2(object, qPrintable(component.errorString()));
        auto *root = qobject_cast<QQuickItem *>(object.get());
        auto *data = root->findChild<EchoMusic::FloatTexture *>("data");
        QVERIFY(data);
        root->setParentItem(scene.window.contentItem());
        data->setGraphics(graphics);
        for (const int width : {1, 2, 4, 1}) {
            const float red = width == 2 ? .5f : -.5f;
            QVERIFY(data->uploadBase64(width, 1, encoded(width, red)));
            QImage image;
            QTRY_VERIFY(!(image = scene.read()).isNull() && data->ready() &&
                        data->cacheBytes() == width * 16 &&
                        qAbs(image.pixelColor(32, 32).red() - (red > 0 ? 191 : 64)) <= 2);
            QCOMPARE(data->cacheBytes(), width * 16);
            QCOMPARE(budget->native.used.load(), width * 16);
            const auto uploads = data->textureUploads();
            QVERIFY(data->uploadBase64(width, 1, encoded(width, red)));
            QVERIFY(!scene.read().isNull());
            QCoreApplication::processEvents();
            QCOMPARE(data->textureUploads(), uploads);
        }
        QPointer<QSGTextureProvider> provider = data->textureProvider();
        QVERIFY(provider);
        QVERIFY(QMetaObject::invokeMethod(&scene.window, "sceneGraphAboutToStop",
                                          Qt::DirectConnection));
        data->update();
        QImage image;
        QTRY_VERIFY(!(image = scene.read()).isNull() &&
                    qAbs(image.pixelColor(32, 32).red() - 64) <= 2);
        QVERIFY(provider);
        QCOMPARE(data->textureProvider(), provider.data());

        // Submit a real upload but destroy the GUI owner before pumping its
        // completion report. The render allocation and report receiver outlive it.
        QVERIFY(data->uploadBase64(2, 1, encoded(2, .5f)));
        QVERIFY(!scene.read().isNull());
        object.reset();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QTRY_VERIFY(!scene.read().isNull() && budget->native.used.load() == 0);
        QCoreApplication::processEvents();
        QVERIFY(provider.isNull());
        QCOMPARE(budget->floatTextures.load(), 0);
    }
    QVERIFY(!scene.window.handle());
}

int main(int argc, char **argv)
{
    QQuickWindow::setSceneGraphBackend("rhi");
    QGuiApplication app(argc, argv);
    PluginRenderLifecycleTest test;
    return QTest::qExec(&test, argc, argv);
}
