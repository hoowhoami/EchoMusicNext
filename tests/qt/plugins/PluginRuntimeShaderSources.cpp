#include "PluginRuntimeGraphicsFixture.h"
#include "FloatTexture.h"
#include <QQmlComponent>
#include <QQuickWindow>
#include <QtEndian>

namespace
{
QString packed(QJSEngine &engine, float red = -.5f)
{
    return engine
        .evaluate(QStringLiteral(R"((() => {
        const buffer = new ArrayBuffer(16);
        new Float32Array(buffer).set([%1, 2, .25, 1]);
        return String.fromCharCode(...new Uint16Array(buffer));
    })())")
                      .arg(red))
        .toString();
}
bool near(const QColor &actual, float r, float g, float b)
{
    return qAbs(actual.red() - qRound(r * 255)) <= 2 &&
           qAbs(actual.green() - qRound(g * 255)) <= 2 &&
           qAbs(actual.blue() - qRound(b * 255)) <= 2 && actual.alpha() == 255;
}
} // namespace

void PluginRuntimeTest::graphicsFloatTextureValidationAndRetirement()
{
    GraphicsFixture f;
    QVERIFY(f.gpu);
    const auto memory = f.gpu->memoryBudget();
    // A QQuickItem window detach also happens from its base destructor. Keep
    // both windows alive and verify external notifications while repeatedly
    // destroying the derived texture (ASan covers its internal member lifetime).
    QQuickWindow firstWindow, secondWindow;
    for (int i = 0; i < 64; ++i) {
        QPointer<FloatTexture> dying = new FloatTexture;
        dying->setGraphics(f.gpu);
        int detached = 0;
        connect(dying, &QQuickItem::windowChanged, &firstWindow,
                [&](QQuickWindow *window) { detached += window == nullptr; });
        dying->setParentItem(firstWindow.contentItem());
        dying->setParentItem(secondWindow.contentItem());
        const int before = detached;
        QSignalSpy destroyed(dying, &QObject::destroyed);
        delete dying.data();
        QVERIFY(dying.isNull());
        QCOMPARE(detached, before + 1);
        QCOMPARE(destroyed.count(), 1);
    }
    QQuickItem parent;
    parent.setSize({16, 16});
    FloatTexture source(&parent);
    source.setSize({1, 1});
    QCOMPARE(source.textureWidth(), 0);
    QCOMPARE(source.textureHeight(), 0);
    QVERIFY(!source.uploadPacked(1, 1, packed(f.host.engine)));
    source.setGraphics(f.gpu);
    QVERIFY(source.upload(1, 1, f.host.engine.evaluate("new Float32Array([-.5,2,.25,1]).buffer")));
    QVERIFY2(source.uploadPacked(1, 1, packed(f.host.engine)), qPrintable(source.errorString()));
    QCOMPARE(source.sourceBytes(), 16);
    QCOMPARE(memory->native.used.load(), 16);
    QVERIFY(!source.uploadPacked(8193, 1, packed(f.host.engine)));
    QVERIFY(!source.uploadPacked(INT_MAX, INT_MAX, packed(f.host.engine)));
    QVERIFY(!source.uploadPacked(1, 0, packed(f.host.engine)));
    QVERIFY(!source.uploadPacked(1, 1, QString("short")));
    QVERIFY(!source.upload(1, 1, f.host.engine.evaluate("new Float32Array(4)")));
    QVERIFY(!source.upload(1, 1, f.host.engine.evaluate("new ArrayBuffer(15)")));
    QVERIFY(
        !source.upload(1, 1, f.host.engine.evaluate("new Float32Array([NaN, 0, 0, 1]).buffer")));
    QCOMPARE(source.sourceBytes(), 16); // Rejected input keeps the last accepted data.
    QVERIFY2(source.uploadPacked(1, 1, packed(f.host.engine)), qPrintable(source.errorString()));
    QCOMPARE(memory->native.used.load(), 16); // Identical upload consumes no second slot.
    QCOMPARE(memory->floatTextures.load(), 1);
    source.setVisible(false);
    QTRY_COMPARE(source.sourceBytes(), 0);
    QCOMPARE(source.textureWidth(), 0);
    QCOMPARE(source.textureHeight(), 0);
    QCOMPARE(memory->native.used.load(), 0);
    QVERIFY(!source.uploadPacked(1, 1, packed(f.host.engine)));
    source.setVisible(true);
    QVERIFY2(source.uploadPacked(1, 1, packed(f.host.engine)), qPrintable(source.errorString()));
    source.setActive(false);
    QCOMPARE(source.sourceBytes(), 0);
    std::vector<std::unique_ptr<FloatTexture>> textures;
    for (int i = 0; i < 256; ++i) {
        auto texture = std::make_unique<FloatTexture>(&parent);
        texture->setSize({1, 1});
        texture->setGraphics(f.gpu);
        QVERIFY(texture->uploadPacked(1, 1, packed(f.host.engine)));
        textures.push_back(std::move(texture));
    }
    source.setActive(true);
    QVERIFY(!source.uploadPacked(1, 1, packed(f.host.engine)));
    QCOMPARE(memory->floatTextures.load(), 256);
    textures.clear();
    QCOMPARE(memory->floatTextures.load(), 0);
    source.setActive(true);
    QVERIFY2(source.uploadPacked(1, 1, packed(f.host.engine)), qPrintable(source.errorString()));
    QVERIFY(QMetaObject::invokeMethod(&f.host.api, "sessionChanged", Q_ARG(quint64, 1)));
    QTRY_COMPARE(source.sourceBytes(), 0);
    QCOMPARE(memory->native.used.load(), 0);
    QVERIFY2(source.uploadPacked(1, 1, packed(f.host.engine)), qPrintable(source.errorString()));
    QVERIFY(f.host.runtime.setEnabled(f.scope->installationId(), false));
    QTRY_COMPARE(source.sourceBytes(), 0);
    QVERIFY(!source.uploadPacked(1, 1, packed(f.host.engine)));
    QCOMPARE(memory->floatTextures.load(), 0);
}

void PluginRuntimeTest::graphicsNativeShaderPixelsAndRecovery()
{
    if (!qEnvironmentVariableIsSet("ECHO_TEST_NATIVE_GPU"))
        QSKIP("Set ECHO_TEST_NATIVE_GPU for real ShaderEffect pixels and scene graph lifecycle");
    GraphicsFixture f;
    QVERIFY(f.gpu);
    const auto memory = f.gpu->memoryBudget();
    QQmlComponent component(&f.host.engine);
    component.setData(R"(
import QtQuick
import EchoMusic.PluginSdk
Rectangle {
    width: 128; height: 64; color: "#334d66"
    property alias effect: effect
    property alias floatData: data
    NativeShaderEffect {
        id: effect; width: 64; height: 64
        property vector4d ink: Qt.vector4d(.1, .2, .3, .5)
        fragmentShader: "qrc:/test-gpu/shaders/gpu/quick-color.frag.qsb"
    }
    FloatTexture { id: data; width: 1; height: 1 }
    ShaderEffect {
        x: 64; width: 64; height: 64
        property var source: data
        fragmentShader: "qrc:/test-gpu/shaders/gpu/quick-float.frag.qsb"
    }
})",
                      QUrl("qrc:/shader-source-fixture.qml"));
    std::unique_ptr<QObject> object(component.create());
    QVERIFY2(object, qPrintable(component.errorString()));
    auto *root = qobject_cast<QQuickItem *>(object.get());
    auto *source = qobject_cast<FloatTexture *>(root->property("floatData").value<QObject *>());
    auto *effect = qobject_cast<QQuickItem *>(root->property("effect").value<QObject *>());
    QVERIFY(source && effect);
    QQuickWindow window;
    window.setColor(Qt::transparent);
    window.setPersistentGraphics(false);
    window.setPersistentSceneGraph(false);
    window.resize(128, 64);
    root->setParentItem(window.contentItem());
    source->setGraphics(f.gpu);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QVERIFY(source->uploadPacked(1, 1, packed(f.host.engine)));
    auto pixel = [&](int x) {
        const auto frame = window.grabWindow();
        return frame.pixelColor(qRound(x * frame.devicePixelRatio()),
                                qRound(32 * frame.devicePixelRatio()));
    };
    QTRY_VERIFY_WITH_TIMEOUT(near(pixel(96), .25, .5, .25), 10000);
    QTRY_COMPARE(source->textureUploads(), 1);
    QTRY_COMPARE(source->cacheBytes(), 16);
    const QColor background("#334d66");
    auto verifyBlend = [&](const QString &mode, QVector4D ink) {
        effect->setProperty("composition", mode);
        effect->setProperty("ink", QVariant::fromValue(ink));
        const auto blend = [&](float s, float d) {
            if (mode == "max")
                return std::max(s, d);
            if (mode == "exclusion")
                return s * (1.f - d) + d * (1.f - s);
            return s + d * (mode == "additive" ? 1.f : mode == "screen" ? 1.f - s : 1.f - ink.w());
        };
        const auto r = blend(ink.x(), background.redF());
        const auto g = blend(ink.y(), background.greenF());
        const auto b = blend(ink.z(), background.blueF());
        return QTest::qWaitFor([&] { return near(pixel(32), r, g, b); }, 5000);
    };
    for (int cycle = 0; cycle < 3; ++cycle)
        for (const auto &mode : {"source-over", "additive", "screen", "max", "exclusion"})
            QVERIFY2(verifyBlend(mode, cycle % 2 ? QVector4D(.3, .1, .05, .5)
                                                 : QVector4D(.1, .2, .3, .5)),
                     mode);
    const auto heldMode = effect->property("composition");
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(".*Composition must be.*"));
    effect->setProperty("composition", "invalid");
    QCOMPARE(effect->property("composition"), heldMode);
    root->setProperty("color", QColor("#66334d66"));
    effect->setProperty("ink", QVariant::fromValue(QVector4D(.1, .2, .3, .5)));
    for (const auto &mode : {QString("source-over"), QString("additive"), QString("screen"),
                             QString("max"), QString("exclusion")}) {
        effect->setProperty("composition", mode);
        const float alpha = mode == "additive" ? .9f : mode == "max" ? .5f : .7f;
        const auto blend = [&](float s, float d) {
            if (mode == "max")
                return std::max(s, d * .4f) / alpha;
            if (mode == "exclusion")
                return (s * (1.f - d * .4f) + d * .4f * (1.f - s)) / alpha;
            return (s + d * .4f *
                            (mode == "additive" ? 1.f
                             : mode == "screen" ? 1.f - s
                                                : .5f)) /
                   alpha;
        };
        QTRY_VERIFY(qAbs(pixel(32).alpha() - qRound(alpha * 255)) <= 2);
        const auto actual = pixel(32);
        QVERIFY(qAbs(actual.red() - qRound(blend(.1, background.redF()) * 255)) <= 3);
        QVERIFY(qAbs(actual.green() - qRound(blend(.2, background.greenF()) * 255)) <= 3);
        QVERIFY(qAbs(actual.blue() - qRound(blend(.3, background.blueF()) * 255)) <= 3);
    }
    root->setProperty("color", background);
    QVERIFY(source->uploadPacked(1, 1, packed(f.host.engine)));
    pixel(96);
    QCOMPARE(source->textureUploads(), 1);
    QVERIFY(source->uploadPacked(1, 1, packed(f.host.engine, .5f)));
    QTRY_VERIFY(near(pixel(96), .75, .5, .25));
    QTRY_COMPARE(source->textureUploads(), 2);
    QTRY_COMPARE(memory->native.used.load(), 16);
    int frames = 0;
    int firstReady = -1;
    bool keptAllocationReady = true;
    source->clear();
    auto streaming = connect(&window, &QQuickWindow::afterAnimating, source, [&] {
        if (frames >= 12)
            return;
        if (source->ready() && firstReady < 0)
            firstReady = frames;
        const bool alreadyReady = source->ready();
        source->uploadPacked(1, 1, packed(f.host.engine, frames % 2 ? .5f : -.5f));
        if (alreadyReady && !source->ready())
            keptAllocationReady = false;
        ++frames;
        window.update();
    });
    window.update();
    QTRY_COMPARE(frames, 12);
    disconnect(streaming);
    QTRY_VERIFY(near(pixel(96), .75, .5, .25));
    QVERIFY(firstReady > 0 && firstReady < 12);
    QVERIFY(keptAllocationReady);
    QTRY_COMPARE(source->cacheBytes(), 16);
    root->setOpacity(0);
    QTRY_COMPARE(source->sourceBytes(), 0);
    QTRY_COMPARE(memory->native.used.load(), 0);
    QVERIFY(!source->uploadPacked(1, 1, packed(f.host.engine)));
    root->setOpacity(1);
    QVERIFY(source->uploadPacked(1, 1, packed(f.host.engine)));
    QTRY_VERIFY(near(pixel(96), .25, .5, .25));
    source->setX(500);
    // A data-only source cell need not intersect the viewport; its sampler's
    // enclosing renderer still does. Moving the renderer away retires it.
    QCOMPARE(source->sourceBytes(), 16);
    QVERIFY(source->uploadPacked(1, 1, packed(f.host.engine)));
    QTRY_VERIFY(near(pixel(96), .25, .5, .25));
    root->setX(500);
    QTRY_COMPARE(source->sourceBytes(), 0);
    QTRY_COMPARE(memory->native.used.load(), 0);
    QVERIFY(!source->uploadPacked(1, 1, packed(f.host.engine)));
    root->setX(0);
    source->setX(0);
    QVERIFY(source->uploadPacked(1, 1, packed(f.host.engine)));
    QTRY_VERIFY(near(pixel(96), .25, .5, .25));
    window.setOpacity(0);
    QTRY_COMPARE(source->sourceBytes(), 0);
    QTRY_COMPARE(memory->native.used.load(), 0);
    QVERIFY(!source->uploadPacked(1, 1, packed(f.host.engine)));
    window.setOpacity(1);
    QVERIFY(source->uploadPacked(1, 1, packed(f.host.engine)));
    QTRY_VERIFY(near(pixel(96), .25, .5, .25));
    window.showMinimized();
    QTRY_COMPARE(source->sourceBytes(), 0);
    QTRY_COMPARE(memory->native.used.load(), 0);
    QVERIFY(!source->uploadPacked(1, 1, packed(f.host.engine)));
    window.showNormal();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QVERIFY(source->uploadPacked(1, 1, packed(f.host.engine)));
    QTRY_VERIFY(near(pixel(96), .25, .5, .25));
    window.hide();
    window.releaseResources();
    QTRY_COMPARE(source->sourceBytes(), 0);
    QTRY_COMPARE(source->cacheBytes(), 0);
    QTRY_COMPARE(memory->native.used.load(), 0);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QVERIFY(source->uploadPacked(1, 1, packed(f.host.engine)));
    QTRY_VERIFY(near(pixel(96), .25, .5, .25));
    QVERIFY(verifyBlend("screen", QVector4D(.1, .2, .3, .5)));
    QVERIFY(QMetaObject::invokeMethod(&f.host.api, "sessionChanged", Q_ARG(quint64, 1)));
    QTRY_COMPARE(source->sourceBytes(), 0);
    QTRY_COMPARE(memory->native.used.load(), 0);
    QVERIFY(source->uploadPacked(1, 1, packed(f.host.engine)));
    QTRY_VERIFY(near(pixel(96), .25, .5, .25));
    // Retire while a sampler still references the provider, then destroy the window.
    QVERIFY(f.host.runtime.setEnabled(f.scope->installationId(), false));
    QTRY_COMPARE(memory->native.used.load(), 0);
    QTRY_COMPARE(memory->floatTextures.load(), 0);
    object.reset();
    window.hide();
    window.releaseResources();
}

void PluginRuntimeTest::graphicsActiveQmlLifecycleAndAccount()
{
    {
        GraphicsFixture enabled;
        QVERIFY(enabled.gpu && enabled.gpu->active());
        QPointer<EchoPluginGraphics> current = enabled.gpu;
        QSignalSpy state(enabled.gpu, &EchoPluginGraphics::activeChanged);
        QVERIFY(enabled.host.runtime.setEnabled(enabled.scope->installationId(), false));
        QTRY_VERIFY(!current || !current->active());
        QCOMPARE(state.count(), 1);
    }
    {
        Host host;
        host.runtime.importDirectory(
            package(host.temporary.path(), "export function activate(c) {}"));
        QTRY_COMPARE(host.runtime.state(host.installation()), PluginRuntime::Active);
        auto *gpu = host.runtime.context(host.installation())->graphics();
        QVERIFY(!gpu->active());
        QCOMPARE(gpu->property("active").toBool(), false);
        QSignalSpy state(gpu, &EchoPluginGraphics::activeChanged);
        QVERIFY(!gpu->createBuffer({{"size", 16}, {"usage", QStringList{"uniform"}}}));
        QCOMPARE(state.count(), 0); // Errors are not lifecycle changes.
    }
    GraphicsFixture f;
    QVERIFY(f.gpu && f.gpu->active());
    const auto property =
        f.gpu->metaObject()->property(f.gpu->metaObject()->indexOfProperty("active"));
    QVERIFY(!property.isWritable());
    QCOMPARE(property.notifySignal().name(), QByteArray("activeChanged"));
    QVERIFY(!f.gpu->capabilities()["available"].toBool()); // No surface/device is required.
    QQmlComponent component(&f.host.engine);
    component.setData(R"(
import QtQml
import EchoMusic.PluginSdk
QtObject {
    required property EchoPluginGraphics graphics
    readonly property bool observed: graphics.active
})",
                      QUrl("qrc:/graphics-active.qml"));
    std::unique_ptr<QObject> mirror(
        component.createWithInitialProperties({{"graphics", QVariant::fromValue(f.gpu)}}));
    QVERIFY2(mirror, qPrintable(component.errorString()));
    QCOMPARE(mirror->property("observed").toBool(), true);
    QSignalSpy binding(mirror.get(), SIGNAL(observedChanged()));
    QSignalSpy rootState(f.gpu, &EchoPluginGraphics::activeChanged);
    auto owner = std::make_unique<QObject>();
    auto *child = f.scope->createPresentation(owner.get(), "graphics.lifecycle");
    QVERIFY(child && child->graphics()->active());
    QSignalSpy childState(child->graphics(), &EchoPluginGraphics::activeChanged);
    auto *old = child->graphics()->createBuffer({{"size", 16}, {"usage", QStringList{"uniform"}}});
    QVERIFY(old);
    QPointer<EchoGpuResource> oldHandle = old;
    QVERIFY(QMetaObject::invokeMethod(&f.host.api, "sessionChanged", Q_ARG(quint64, 1)));
    QCOMPARE(rootState.count(), 2); // Root facade closes and reopens for the new account.
    QCOMPARE(binding.count(), 2);
    QCOMPARE(mirror->property("observed").toBool(), true);
    QCOMPARE(childState.count(), 1);
    QVERIFY(!child->graphics()->active());
    QVERIFY(!oldHandle || !oldHandle->active());
    QVERIFY(!child->graphics()->createBuffer({{"size", 16}, {"usage", QStringList{"uniform"}}}));
    QVERIFY(f.gpu->createBuffer({{"size", 16}, {"usage", QStringList{"uniform"}}}));
    auto secondOwner = std::make_unique<QObject>();
    QPointer<EchoPluginContext> second =
        f.scope->createPresentation(secondOwner.get(), "graphics.close");
    QVERIFY(second && second->graphics()->active());
    QSignalSpy secondState(second->graphics(), &EchoPluginGraphics::activeChanged);
    secondOwner.reset();
    QTRY_VERIFY(!second || !second->graphics()->active());
    QCOMPARE(secondState.count(), 1);
    owner.reset();
    FloatTexture source;
    source.setSize({1, 1});
    source.setGraphics(f.gpu);
    QVERIFY(source.uploadPacked(1, 1, packed(f.host.engine)));
    f.gpu->shutdown();
    QCOMPARE(source.sourceBytes(), 0);
    QCOMPARE(rootState.count(), 3);
    QCOMPARE(binding.count(), 3);
    QCOMPARE(mirror->property("observed").toBool(), false);
    f.gpu->shutdown();
    QCOMPARE(rootState.count(), 3);
    QVERIFY(f.host.runtime.setEnabled(f.scope->installationId(), false));
    QCOMPARE(rootState.count(), 3);
}

void PluginRuntimeTest::graphicsVertexFloatTexturePixels()
{
    if (!qEnvironmentVariableIsSet("ECHO_TEST_NATIVE_GPU"))
        QSKIP("Native vertex-texture sampling requires an exposed RHI window");
    GraphicsFixture f;
    QVERIFY(f.gpu);
    QQmlComponent component(&f.host.engine);
    component.setData(R"(
import QtQuick
import EchoMusic.PluginSdk
Item {
    width:64; height:64
    property alias pixels: pixels
    FloatTexture {id:pixels; width:1; height:1}
    NativeShaderEffect {
        anchors.fill:parent
        property var samples:pixels
        property vector4d ink:Qt.vector4d(.1,.2,.3,1)
        vertexShader:"qrc:/test-gpu/shaders/gpu/quick-data.vert.qsb"
        fragmentShader:"qrc:/test-gpu/shaders/gpu/quick-color.frag.qsb"
        mesh:GridMesh {resolution:Qt.size(1,7)}
    }
})",
                      QUrl("qrc:/vertex-float.qml"));
    std::unique_ptr<QObject> object(component.create());
    QVERIFY2(object, qPrintable(component.errorString()));
    auto *root = qobject_cast<QQuickItem *>(object.get());
    auto *pixels = qobject_cast<FloatTexture *>(root->property("pixels").value<QObject *>());
    QVERIFY(pixels);
    pixels->setGraphics(f.gpu);
    QQuickWindow window;
    window.resize(64, 64);
    root->setParentItem(window.contentItem());
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    const auto data = f.host.engine
                          .evaluate(R"((()=>{
        const values=new Float32Array(24);
        for(let i=0;i<6;i++) values.set([-.5,-2,.25,1],i*4);
        values.set([.75,2,.25,1],20);
        return String.fromCharCode(...new Uint16Array(values.buffer));
    })())")
                          .toString();
    QVERIFY2(pixels->uploadPacked(3, 2, data), qPrintable(pixels->errorString()));
    auto actual = [&] {
        const auto image = window.grabWindow();
        return image.pixelColor(image.width() / 2, image.height() / 2);
    };
    QTRY_VERIFY_WITH_TIMEOUT(near(actual(), .1, .2, .3), 5000);
    QTRY_VERIFY(pixels->ready());
    QCOMPARE(pixels->textureUploads(), 1);
    object.reset();
}

void PluginRuntimeTest::graphicsNativePipelineBlendModes()
{
    GraphicsFixture fixture;
    QVERIFY(fixture.gpu);
    auto *gpu = fixture.gpu;
    const auto layout = QVariantMap{
        {"stride", 8},
        {"attributes",
         QVariantList{QVariantMap{{"location", 0}, {"offset", 0}, {"format", "float2"}}}}};
    auto definition = QVariantMap{{"vertexShader", "fixture.vert.qsb"},
                                  {"fragmentShader", "blend.frag.qsb"},
                                  {"vertexLayout", layout},
                                  {"topology", "triangle-strip"},
                                  {"blend", "invalid"}};
    QVERIFY(!gpu->createRenderPipeline(definition));
    definition["blend"] = "none";
    definition["blends"] = QVariantList{"invalid"};
    QVERIFY(!gpu->createRenderPipeline(definition));
    definition.remove("blends");
    if (!qEnvironmentVariableIsSet("ECHO_TEST_NATIVE_GPU"))
        QSKIP("Native blend equations require actual GPU pixel readback");
    auto *target = gpu->createTexture(
        {{"width", 2}, {"height", 2}, {"usage", QStringList{"render-target", "readback"}}});
    auto *vertices = fixture.vertices();
    auto *ink = gpu->createBuffer({{"size", 16}, {"usage", QStringList{"uniform"}}},
                                  fixture.host.engine.evaluate("[.1,.2,.3,.5]"));
    QVERIFY(target && vertices && ink);
    QQuickWindow window;
    window.resize(120, 80);
    EchoGpuSurface surface;
    surface.setParentItem(window.contentItem());
    surface.setSize(window.size());
    surface.setGraphics(gpu);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QTRY_VERIFY(surface.status().value("available").toBool());
    for (const auto &mode : {QString("max"), QString("exclusion")}) {
        definition["blend"] = mode;
        auto *pipeline = gpu->createRenderPipeline(definition);
        QVERIFY(pipeline);
        for (bool reversed : {false, true}) {
            QVERIFY(gpu->writeBuffer(
                ink, fixture.host.engine.evaluate(reversed ? "[.4,.1,.2,.4]" : "[.1,.2,.3,.5]")));
            QVariantMap pass{
                {"type", "render"},
                {"pipeline", pipeline->id()},
                {"target", target->id()},
                {"clear", reversed ? QVariantList{.1, .2, .3, .5} : QVariantList{.4, .1, .2, .4}},
                {"bindings",
                 QVariantList{
                     QVariantMap{{"binding", 0}, {"kind", "uniform"}, {"resource", "frame"}},
                     QVariantMap{{"binding", 1}, {"kind", "uniform"}, {"resource", ink->id()}}}},
                {"draws",
                 QVariantList{QVariantMap{{"count", 4}, {"vertexBuffer", vertices->id()}}}}};
            QVERIFY(surface.submit({{"passes", QVariantList{pass}}}));
            QTest::qWait(50);
            auto *read = surface.readback(target);
            QVERIFY2(fixture.success(read),
                     read ? qPrintable(read->errorMessage()) : "no readback");
            const auto data = read->result().toMap()["data"].toByteArray();
            const QList<double> expected =
                mode == "max" ? QList<double>{.4, .2, .3, .5} : QList<double>{.42, .26, .38, .7};
            QCOMPARE(data.size(), 16);
            for (int i = 0; i < data.size(); ++i)
                QVERIFY2(qAbs(int(quint8(data[i])) - qRound(expected[i % 4] * 255)) <= 2,
                         qPrintable(mode + ": " + data.toHex()));
        }
    }
}

void PluginRuntimeTest::graphicsDepthTargetsMsaaAndRecovery()
{
    GraphicsFixture f;
    QVERIFY(f.gpu);
    auto *gpu = f.gpu;
    const QVariantMap depthDefinition{{"width", 24},
                                      {"height", 24},
                                      {"format", "depth32f"},
                                      {"usage", QStringList{"render-target", "sampled"}}};
    for (const auto &patch : {QVariantMap{{"mipmaps", true}}, QVariantMap{{"dimension", "cube"}},
                              QVariantMap{{"usage", QStringList{"storage"}}},
                              QVariantMap{{"usage", QStringList{"readback", "render-target"}}}}) {
        auto invalid = depthDefinition;
        for (auto it = patch.begin(); it != patch.end(); ++it)
            invalid[it.key()] = it.value();
        QVERIFY(!gpu->createTexture(invalid));
    }
    QVERIFY(!gpu->createTexture(depthDefinition, f.host.engine.evaluate("[1,0,0,1]")));
    auto *depth = gpu->createTexture(depthDefinition);
    auto *color = gpu->createTexture(
        {{"width", 24}, {"height", 24}, {"usage", QStringList{"render-target", "readback"}}});
    auto *view = gpu->createTexture(
        {{"width", 24}, {"height", 24}, {"usage", QStringList{"render-target", "readback"}}});
    auto *sampler = gpu->createSampler({{"minFilter", "nearest"}, {"magFilter", "nearest"}});
    auto *vertices =
        gpu->createBuffer({{"size", 48}, {"usage", QStringList{"vertex"}}},
                          f.host.engine.evaluate("[-1,1,-.5, 1,1,-.5, -1,-1,-.5, 1,-1,-.5]"));
    auto *farVertices =
        gpu->createBuffer({{"size", 48}, {"usage", QStringList{"vertex"}}},
                          f.host.engine.evaluate("[-1,1,.5, 1,1,.5, -1,-1,.5, 1,-1,.5]"));
    auto *ink = gpu->createBuffer({{"size", 16}, {"usage", QStringList{"uniform"}}},
                                  f.host.engine.evaluate("[.25,.5,.75,1]"));
    auto *red = gpu->createBuffer({{"size", 16}, {"usage", QStringList{"uniform"}}},
                                  f.host.engine.evaluate("[1,0,0,1]"));
    const QVariantMap layout{
        {"stride", 12},
        {"attributes",
         QVariantList{QVariantMap{{"location", 0}, {"offset", 0}, {"format", "float3"}}}}};
    auto pipelineDefinition = QVariantMap{{"vertexShader", "depth.vert.qsb"},
                                          {"fragmentShader", "blend.frag.qsb"},
                                          {"blend", "none"},
                                          {"topology", "triangle-strip"},
                                          {"depthTest", true},
                                          {"depthWrite", true},
                                          {"vertexLayout", layout}};
    auto *pipeline = gpu->createRenderPipeline(pipelineDefinition);
    const auto colorPipelineDefinition = pipelineDefinition;
    for (const auto &patch :
         {QVariantMap{{"depthCompare", "approximate"}}, QVariantMap{{"frontFace", "clockwise"}},
          QVariantMap{{"depthCompare", 1}}, QVariantMap{{"frontFace", true}}}) {
        auto invalidPipeline = colorPipelineDefinition;
        for (auto it = patch.begin(); it != patch.end(); ++it)
            invalidPipeline[it.key()] = it.value();
        QVERIFY(!gpu->createRenderPipeline(invalidPipeline));
    }
    pipelineDefinition["fragmentShader"] = "depth-only.frag.qsb";
    auto *depthOnly = gpu->createRenderPipeline(pipelineDefinition);
    pipelineDefinition["fragmentShader"] = "depth-view.frag.qsb";
    pipelineDefinition["depthTest"] = false;
    pipelineDefinition["depthWrite"] = false;
    auto *sample = gpu->createRenderPipeline(pipelineDefinition);
    QVERIFY(depth && color && view && sampler && vertices && farVertices && ink && red &&
            pipeline && depthOnly && sample);
    QVERIFY(!gpu->writeTexture(depth, f.host.engine.evaluate("[]")));
    const auto binding = [](int index, const QString &kind, const QString &id) {
        return QVariantMap{{"binding", index}, {"kind", kind}, {"resource", id}};
    };
    auto pass = QVariantMap{
        {"type", "render"},
        {"pipeline", pipeline->id()},
        {"target", color->id()},
        {"depthTarget", QVariantMap{{"texture", depth->id()}}},
        {"bindings",
         QVariantList{binding(0, "uniform", "frame"), binding(1, "uniform", ink->id())}},
        {"draws", QVariantList{QVariantMap{{"count", 4}, {"vertexBuffer", vertices->id()}}}}};
    QString error;
    QVERIFY2(gpu->validateFrame({{"passes", QVariantList{pass}}}, error), qPrintable(error));
    for (const auto &count : {QVariant(true), QVariant(3), QVariant(4.5), QVariant(16)}) {
        auto invalid = pass;
        invalid["samples"] = count;
        QVERIFY(!gpu->validateFrame({{"passes", QVariantList{invalid}}}, error));
    }
    auto invalid = pass;
    invalid["target"] = "surface";
    QVERIFY(!gpu->validateFrame({{"passes", QVariantList{invalid}}}, error));
    invalid = pass;
    invalid["target"] = depth->id();
    QVERIFY(!gpu->validateFrame({{"passes", QVariantList{invalid}}}, error));
    invalid = pass;
    invalid["depthTarget"] = QVariantMap{{"texture", color->id()}};
    QVERIFY(!gpu->validateFrame({{"passes", QVariantList{invalid}}}, error));
    invalid = pass;
    invalid["depthTarget"] = QVariantMap{{"texture", depth->id()}, {"level", 0}};
    QVERIFY(!gpu->validateFrame({{"passes", QVariantList{invalid}}}, error));
    invalid = pass;
    invalid["bindings"] =
        QVariantList{binding(0, "uniform", "frame"), binding(1, "texture", depth->id())};
    QVERIFY(!gpu->validateFrame({{"passes", QVariantList{invalid}}}, error));
    auto *smallDepth = gpu->createTexture({{"width", 12},
                                           {"height", 24},
                                           {"format", "depth32f"},
                                           {"usage", QStringList{"render-target"}}});
    QVERIFY(smallDepth);
    invalid = pass;
    invalid["depthTarget"] = QVariantMap{{"texture", smallDepth->id()}};
    QVERIFY(!gpu->validateFrame({{"passes", QVariantList{invalid}}}, error));
    if (!qEnvironmentVariableIsSet("ECHO_TEST_NATIVE_GPU"))
        QSKIP("Depth and MSAA require native GPU pixels");
    QQuickWindow window;
    window.resize(120, 80);
    EchoGpuSurface surface;
    surface.setParentItem(window.contentItem());
    surface.setSize(window.size());
    surface.setGraphics(gpu);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QTRY_VERIFY(surface.status().value("available").toBool());
    if (!surface.status().value("depthTextures").toBool())
        QSKIP("Device lacks depth texture sampling");
    auto depthBinding = binding(1, "texture", depth->id());
    depthBinding["sampler"] = sampler->id();
    auto readPass = QVariantMap{
        {"type", "render"},
        {"pipeline", sample->id()},
        {"target", view->id()},
        {"bindings", QVariantList{binding(0, "uniform", "frame"), depthBinding}},
        {"draws", QVariantList{QVariantMap{{"count", 4}, {"vertexBuffer", vertices->id()}}}}};
    // A never-written depth texture is initialized to the far plane before sampling.
    QVERIFY(surface.submit({{"passes", QVariantList{readPass}}}));
    QVERIFY(f.pixels(surface, view, {}, 255, 255, 255));
    // The second draw has the same depth, or a farther depth. This proves all
    // explicit comparisons as actual pixels while the first keeps the old Less default.
    const QStringList comparisons{"less",  "less-equal", "greater", "greater-equal",
                                  "equal", "not-equal",  "always",  "never"};
    for (const auto &comparison : comparisons) {
        auto definition = colorPipelineDefinition;
        definition["depthCompare"] = comparison;
        definition["depthWrite"] = false;
        auto *compared = gpu->createRenderPipeline(definition);
        QVERIFY(compared);
        for (bool farther : {false, true}) {
            auto second = pass;
            second["pipeline"] = compared->id();
            second["loadOp"] = "load";
            second["bindings"] =
                QVariantList{binding(0, "uniform", "frame"), binding(1, "uniform", red->id())};
            second["draws"] = QVariantList{QVariantMap{
                {"count", 4}, {"vertexBuffer", farther ? farVertices->id() : vertices->id()}}};
            const bool accepts = comparison == "always" || comparison == "greater-equal" ||
                                 (farther ? comparison == "greater" || comparison == "not-equal"
                                          : comparison == "less-equal" || comparison == "equal");
            QVERIFY(surface.submit({{"passes", QVariantList{pass, second}}}));
            QVERIFY2(f.pixels(surface, color, {}, accepts ? 255 : 64, accepts ? 0 : 128,
                              accepts ? 0 : 191),
                     qPrintable(comparison + (farther ? " far" : " equal")));
        }
    }
    // Changing front-face winding reverses back-face culling. Exactly one quad
    // must cover the target; both use the same geometry and native correction.
    QList<bool> windingVisible;
    for (const auto &front : {QString("ccw"), QString("cw")}) {
        auto definition = colorPipelineDefinition;
        definition["frontFace"] = front;
        definition["cull"] = "back";
        definition["depthTest"] = false;
        definition["depthWrite"] = false;
        auto *culled = gpu->createRenderPipeline(definition);
        QVERIFY(culled);
        auto draw = pass;
        draw["pipeline"] = culled->id();
        draw["clear"] = QVariantList{0, 0, 0, 1};
        QVERIFY(surface.submit({{"passes", QVariantList{draw}}}));
        auto *read = surface.readback(color);
        QVERIFY(f.success(read));
        const auto data = read->result().toMap()["data"].toByteArray();
        QVERIFY(data.size() == 24 * 24 * 4);
        const bool shown = quint8(data[0]) > 0;
        for (int i = 0; i < data.size(); i += 4) {
            QCOMPARE(quint8(data[i]), quint8(shown ? 64 : 0));
            QCOMPARE(quint8(data[i + 1]), quint8(shown ? 128 : 0));
            QCOMPARE(quint8(data[i + 2]), quint8(shown ? 191 : 0));
            QCOMPARE(quint8(data[i + 3]), quint8(255));
        }
        windingVisible.append(shown);
    }
    QVERIFY(windingVisible[0] != windingVisible[1]);
    const auto nativeCounts = surface.status().value("sampleCounts").value<QList<int>>();
    for (int samples : {1, 4}) {
        if (samples > 1 &&
            (!nativeCounts.contains(samples) || !surface.status().value("depthResolve").toBool()))
            continue;
        qInfo() << "Native depth/MSAA sample count" << samples;
        pass["samples"] = samples;
        auto farPass = pass;
        farPass["loadOp"] = "load";
        farPass["bindings"] =
            QVariantList{binding(0, "uniform", "frame"), binding(1, "uniform", red->id())};
        farPass["draws"] =
            QVariantList{QVariantMap{{"count", 4}, {"vertexBuffer", farVertices->id()}}};
        QVERIFY(surface.submit({{"passes", QVariantList{pass, farPass, readPass}}}));
        QVERIFY2(f.pixels(surface, color, {}, 64, 128, 191),
                 qPrintable(surface.status().value("reason").toString()));
        QVERIFY2(f.pixels(surface, view, {}, 64, 64, 64),
                 qPrintable(surface.status().value("reason").toString()));
        auto edgePass = pass;
        edgePass["draws"] =
            QVariantList{QVariantMap{{"count", 3}, {"vertexBuffer", vertices->id()}}};
        QVERIFY(surface.submit({{"passes", QVariantList{edgePass}}}));
        auto *edge = surface.readback(color);
        QVERIFY(f.success(edge));
        const auto edgeBytes = edge->result().toMap().value("data").toByteArray();
        bool partialCoverage = false;
        for (int offset = 0; offset < edgeBytes.size(); offset += 4) {
            const auto alpha = quint8(edgeBytes[offset + 3]);
            partialCoverage |= alpha > 0 && alpha < 255;
        }
        QCOMPARE(partialCoverage, samples > 1);
        auto only = pass;
        only.remove("target");
        only["colorTargets"] = QVariantList{};
        only["pipeline"] = depthOnly->id();
        only["bindings"] = QVariantList{binding(0, "uniform", "frame")};
        QVERIFY(surface.submit({{"passes", QVariantList{only, readPass}}}));
        QVERIFY(f.pixels(surface, view, {}, 64, 64, 64));
        window.hide();
        QTRY_COMPARE_WITH_TIMEOUT(gpu->memoryBudget()->native.used.load(), 0, 10000);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QVERIFY(surface.submit({{"passes", QVariantList{pass, readPass}}}));
        QVERIFY(f.pixels(surface, view, {}, 64, 64, 64));
    }
    QVERIFY(!surface.readback(depth)->errorMessage().isEmpty());
    // Resolving depth retires its multisample contents. An intervening pass cannot
    // turn a subsequent load into a valid continuation of the resolved sequence.
    if (nativeCounts.contains(4) && surface.status().value("depthResolve").toBool()) {
        auto load = pass;
        load["samples"] = 4;
        load["loadOp"] = "load";
        pass["samples"] = 4;
        QVERIFY(surface.submit({{"passes", QVariantList{pass, readPass, load}}}));
        QTRY_VERIFY(surface.status().value("reason").toString().contains("MSAA load"));
        QVERIFY(surface.submit({{"passes", QVariantList{pass, readPass}}}));
        QVERIFY(f.pixels(surface, view, {}, 64, 64, 64));
    }
    const auto memory = gpu->memoryBudget();
    QVERIFY(memory->native.used.load() > 0);
    QVERIFY(f.host.runtime.setEnabled(f.host.installation(), false));
    QTRY_COMPARE(f.host.runtime.state(f.host.installation()), PluginRuntime::Disabled);
    surface.setGraphics(nullptr);
    QTRY_COMPARE_WITH_TIMEOUT(memory->native.used.load(), 0, 10000);
}

void PluginRuntimeTest::graphicsDeclaredLimitsAndRetirement()
{
    constexpr qint64 mib = 1024ll * 1024;
    const QVariantMap largeTexture{{"width", 2048},
                                   {"height", 2048},
                                   {"usage", QStringList{"sampled", "render-target", "readback"}}};
    {
        GraphicsFixture f;
        QVERIFY(f.gpu);
        const auto limits = f.gpu->capabilities().value("limits").toMap();
        QCOMPARE(limits.value("logicalBytes").toLongLong(), 64 * mib);
        QCOMPARE(limits.value("nativeBytes").toLongLong(), 128 * mib);
        QCOMPARE(limits.value("renderTargetBytes").toLongLong(), 256 * mib);
        QCOMPARE(limits.value("maxCommands").toInt(), 32);
        QList<EchoGpuResource *> textures;
        for (int i = 0; i < 4; ++i) {
            auto *t = f.gpu->createTexture(largeTexture);
            QVERIFY(t);
            textures.append(t);
        }
        QVERIFY(!f.gpu->createTexture({{"width", 1}, {"height", 1}}));
        for (auto *t : textures)
            t->dispose();
        auto *pipeline = f.pipeline();
        auto *vertices = f.vertices();
        QVERIFY(pipeline && vertices);
        const QVariantMap pass{
            {"type", "render"},
            {"pipeline", pipeline->id()},
            {"bindings",
             QVariantList{QVariantMap{{"binding", 0}, {"kind", "uniform"}, {"resource", "frame"}}}},
            {"draws", QVariantList{QVariantMap{{"count", 4}, {"vertexBuffer", vertices->id()}}}}};
        QVariantList passes;
        for (int i = 0; i < 33; ++i)
            passes.append(pass);
        QString error;
        QVERIFY(!f.gpu->validateFrame({{"passes", passes}}, error));
        // Field validation is performed before activation, including unsupported values.
        auto manifest = QJsonObject::fromVariantMap(f.scope->package().manifest);
        const QJsonObject requested{{"logicalBytes", 192 * double(mib)},
                                    {"nativeBytes", 384 * double(mib)},
                                    {"renderTargetBytes", 640 * double(mib)},
                                    {"maxCommands", 73}};
        manifest["graphicsLimits"] = requested;
        QVERIFY(!PluginPackage::inspectManifest(QJsonDocument(manifest).toJson()).error.isEmpty());
        auto requirements = manifest["requires"].toObject();
        auto features = requirements["features"].toArray();
        features.append("graphics.limits");
        requirements["features"] = features;
        manifest["requires"] = requirements;
        QVERIFY(PluginPackage::inspectManifest(QJsonDocument(manifest).toJson()).error.isEmpty());
        for (const auto &invalid : {QJsonValue(true), QJsonValue(0), QJsonValue(-1),
                                    QJsonValue(1.5), QJsonValue(1024. * 1024 * 1024)}) {
            auto limits = requested;
            limits["logicalBytes"] = invalid;
            manifest["graphicsLimits"] = limits;
            QVERIFY(
                !PluginPackage::inspectManifest(QJsonDocument(manifest).toJson()).error.isEmpty());
        }
        auto missing = requested;
        missing.remove("nativeBytes");
        manifest["graphicsLimits"] = missing;
        QVERIFY(!PluginPackage::inspectManifest(QJsonDocument(manifest).toJson()).error.isEmpty());
        auto unknown = requested;
        unknown["other"] = 1;
        manifest["graphicsLimits"] = unknown;
        QVERIFY(!PluginPackage::inspectManifest(QJsonDocument(manifest).toJson()).error.isEmpty());
        manifest["graphicsLimits"] = requested;
        manifest["capabilities"] = QJsonArray{};
        QVERIFY(!PluginPackage::inspectManifest(QJsonDocument(manifest).toJson()).error.isEmpty());
    }
    GraphicsFixture f(QVariantMap{{"logicalBytes", 192 * mib},
                                  {"nativeBytes", 384 * mib},
                                  {"renderTargetBytes", 640 * mib},
                                  {"maxCommands", 73}});
    QVERIFY(f.gpu);
    const auto budget = f.gpu->memoryBudget();
    const auto limits = f.gpu->capabilities().value("limits").toMap();
    QCOMPARE(limits.value("logicalBytes").toLongLong(), 192 * mib);
    QCOMPARE(limits.value("nativeBytes").toLongLong(), 384 * mib);
    QCOMPARE(limits.value("renderTargetBytes").toLongLong(), 640 * mib);
    QCOMPARE(limits.value("maxCommands").toInt(), 73);
    QCOMPARE(budget->submissions.limit, 64 * mib);
    QCOMPARE(budget->readbacks.limit, 32 * mib);
    QList<EchoGpuResource *> textures;
    for (int i = 0; i < 12; ++i) {
        auto *t = f.gpu->createTexture(largeTexture);
        QVERIFY(t);
        textures.append(t);
    }
    QVERIFY(!f.gpu->createTexture({{"width", 1}, {"height", 1}}));
    // Keep 80MiB of GPU-only targets for actual native allocation; release the rest.
    while (textures.size() > 5)
        textures.takeLast()->dispose();
    auto *pipeline = f.pipeline();
    auto *vertices = f.vertices();
    auto *view = f.gpu->createTexture(
        {{"width", 8}, {"height", 8}, {"usage", QStringList{"render-target", "readback"}}});
    QVERIFY(pipeline && vertices && view);
    QVariantMap pass{
        {"type", "render"},
        {"pipeline", pipeline->id()},
        {"target", view->id()},
        {"bindings",
         QVariantList{QVariantMap{{"binding", 0}, {"kind", "uniform"}, {"resource", "frame"}}}},
        {"draws", QVariantList{QVariantMap{{"count", 4}, {"vertexBuffer", vertices->id()}}}}};
    QVariantList passes;
    for (int i = 0; i < 73; ++i)
        passes.append(pass);
    QString error;
    QVERIFY2(f.gpu->validateFrame({{"passes", passes}}, error), qPrintable(error));
    passes.append(pass);
    QVERIFY(!f.gpu->validateFrame({{"passes", passes}}, error));
    auto owner = std::make_unique<QObject>();
    auto *child = f.scope->createPresentation(owner.get(), "test.declared.limits");
    QCOMPARE(child->graphics()->memoryBudget(), budget);
    QCOMPARE(child->graphics()->capabilities().value("limits").toMap().value("maxCommands").toInt(),
             73);
    owner.reset();
    if (!qEnvironmentVariableIsSet("ECHO_TEST_NATIVE_GPU"))
        QSKIP("Declared limit allocation pixels require native GPU");
    QQuickWindow window;
    window.resize(120, 80);
    EchoGpuSurface surface;
    surface.setParentItem(window.contentItem());
    surface.setSize(window.size());
    surface.setGraphics(f.gpu);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QTRY_VERIFY(surface.status().value("available").toBool());
    passes = QVariantList{};
    for (int i = 0; i < 33; ++i)
        passes.append(pass);
    auto *operation = surface.enqueue({{"passes", passes}});
    QVERIFY(operation);
    QVERIFY(budget->submissions.used.load() > 0 && budget->submissions.used.load() < 64 * mib);
    QVERIFY2(f.success(operation), qPrintable(operation->errorMessage()));
    QVERIFY(f.pixels(surface, view, {}, 64, 128, 191));
    QVERIFY(f.gpu->descriptions().value(view->id()).data.isEmpty());
    QVERIFY(!f.gpu->writeTexture(view, f.host.engine.evaluate("new Uint8Array(4).buffer")));
    QVERIFY(f.gpu->descriptions().value(view->id()).data.isEmpty());
    QVERIFY(f.gpu->writeTexture(
        view,
        f.host.engine.evaluate(
            "const upload=new Uint8Array(8*8*4);"
            "for(let i=0;i<upload.length;i+=4){upload[i]=255;upload[i+3]=255;}upload.buffer")));
    QVERIFY(f.pixels(surface, view, {}, 255, 0, 0));
    QCOMPARE(f.gpu->descriptions().value(view->id()).data.size(), 8 * 8 * 4);
    QVERIFY(f.gpu->descriptions().value(textures.first()->id()).data.isEmpty());
    const auto assertZero = [&](EchoGpuResource *texture) {
        auto *read = surface.readback(texture, {{"width", 4}, {"height", 4}});
        if (!f.success(read))
            return false;
        const auto data = read->result().toMap().value("data").toByteArray();
        return data.size() == 64 && data.count(char(0)) == data.size();
    };
    // Native acquisition is lazy. Sampling each otherwise unused target proves
    // both its transparent initialization and an actual allocation over 64MiB.
    for (auto *texture : textures)
        QVERIFY(assertZero(texture));
    QVERIFY(budget->native.used.load() >= 80 * mib);
    auto paintLarge = pass;
    paintLarge["target"] = textures.first()->id();
    QVERIFY(f.success(surface.enqueue({{"passes", QVariantList{paintLarge}}})));
    // CPU upload snapshots still cannot retain 64MiB plus metadata.
    QList<EchoGpuResource *> buffers;
    for (int i = 0; i < 4; ++i) {
        auto *b =
            f.gpu->createBuffer({{"size", 16 * 1024 * 1024}, {"usage", QStringList{"vertex"}}});
        QVERIFY(b);
        buffers.append(b);
    }
    auto *oversized = surface.enqueue({{"passes", passes}});
    QVERIFY(oversized);
    QCOMPARE(oversized->state(), EchoPluginOperation::Failed);
    QVERIFY(oversized->errorMessage().contains("snapshot budget"));
    for (auto *b : buffers)
        b->dispose();
    QTRY_COMPARE(budget->submissions.used.load(), 0);
    if (surface.status().value("sampleCounts").value<QList<int>>().contains(4)) {
        QVariantList heavy;
        for (auto *texture : textures) {
            auto draw = pass;
            draw["target"] = texture->id();
            draw["samples"] = 4;
            heavy.append(draw);
        }
        // Five 16MiB textures plus five 4x attachments exceed the declared
        // 384MiB native limit, while remaining within the logical quota.
        auto *nativeExceeded = surface.enqueue({{"passes", heavy}});
        QVERIFY(nativeExceeded);
        f.keep(nativeExceeded);
        QTRY_VERIFY(nativeExceeded->finished());
        QCOMPARE(nativeExceeded->state(), EchoPluginOperation::Failed);
        QVERIFY2(nativeExceeded->errorMessage().contains("budget"),
                 qPrintable(nativeExceeded->errorMessage()));
    }
    window.hide();
    QTRY_COMPARE_WITH_TIMEOUT(budget->native.used.load(), 0, 10000);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QVERIFY(f.success(surface.enqueue({{"passes", passes}})));
    QVERIFY(f.pixels(surface, view, {}, 64, 128, 191));
    for (auto *texture : textures)
        QVERIFY(assertZero(texture)); // New allocations clear again after hide.
    QVERIFY(budget->native.used.load() >= 80 * mib);
    QVERIFY(QMetaObject::invokeMethod(&f.host.api, "sessionChanged", Q_ARG(quint64, 1)));
    for (auto *t : textures)
        QVERIFY(!t->active());
    QTRY_COMPARE_WITH_TIMEOUT(budget->native.used.load(), 0, 10000);
    QCOMPARE(f.gpu->memoryBudget(), budget);
    surface.setGraphics(nullptr);
    QVERIFY(f.host.runtime.setEnabled(f.host.installation(), false));
    QTRY_COMPARE(f.host.runtime.state(f.host.installation()), PluginRuntime::Disabled);
    QTRY_COMPARE_WITH_TIMEOUT(budget->native.used.load(), 0, 10000);
    QTRY_COMPARE(budget->targets.used.load(), 0);
    QTRY_COMPARE(budget->submissions.used.load(), 0);
}
