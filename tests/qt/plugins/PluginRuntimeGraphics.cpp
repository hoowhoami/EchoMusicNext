#include "PluginRuntimeTest.h"
#include "PluginRuntimeTestFixture.h"
#include "EchoGpuSurface.h"
#include <qfloat16.h>
#include <cstring>
namespace
{
EchoPluginContext *graphicsScope(Host &host)
{
    const auto source = package(host.temporary.path(), "export function activate(c) {}",
                                {"lifecycle", "operations", "graphics.rhi", "graphics.compute",
                                 "graphics.hdr", "graphics.readback"},
                                {"graphics"});
    for (const auto &name : {"fixture.vert", "fixture.frag", "fixture.comp"}) {
        QFile shader(":/test-gpu/shaders/gpu/" + QString(name) + ".qsb");
        if (!shader.open(QIODevice::ReadOnly))
            return nullptr;
        write(source + '/' + name + ".qsb", shader.readAll());
    }
    host.runtime.importDirectory(source);
    QElapsedTimer elapsed;
    elapsed.start();
    while (host.runtime.state(host.installation()) != PluginRuntime::Active &&
           elapsed.elapsed() < 5000)
        QTest::qWait(10);
    return host.runtime.context(host.installation());
}
QVariantMap binding(int n, const QString &kind, const QString &resource)
{
    return {{"binding", n}, {"kind", kind}, {"resource", resource}};
}
} // namespace
void PluginRuntimeTest::graphicsValidationOwnershipAndAccount()
{
    Host host;
    auto *scope = graphicsScope(host);
    QVERIFY(scope);
    auto *gpu = scope->graphics();
    QVERIFY(!gpu->createBuffer({{"size", 7}, {"usage", QStringList{"uniform"}}}));
    QVERIFY(!gpu->createBuffer({{"size", 16}, {"usage", QStringList{"uniform", "storage"}}}));
    QVERIFY(!gpu->createRenderPipeline(
        {{"vertexShader", "../fixture.vert.qsb"}, {"fragmentShader", "fixture.frag.qsb"}}));
    QVERIFY(!gpu->createComputePipeline({{"computeShader", "fixture.frag.qsb"}}));
    QPointer<EchoGpuResource> buffer =
        gpu->createBuffer({{"size", 16}, {"usage", QStringList{"uniform"}}},
                          host.engine.evaluate("new Float32Array([.25,.5,.75,1]).buffer"));
    QVERIFY(buffer);
    QVERIFY(!gpu->writeBuffer(buffer, host.engine.evaluate("[1]"), 16));
    QVERIFY(!gpu->writeBuffer(buffer, host.engine.evaluate("[Infinity]")));
    auto owner = std::make_unique<QObject>();
    auto *child = scope->createPresentation(owner.get(), "test.gpu");
    QVERIFY(child);
    QVERIFY(!child->graphics()->writeBuffer(buffer, host.engine.evaluate("[0]")));
    auto *other =
        child->graphics()->createBuffer({{"size", 16}, {"usage", QStringList{"uniform"}}});
    QVERIFY(other);
    QVERIFY(other->id() != buffer->id());
    QVERIFY(QMetaObject::invokeMethod(&host.api, "sessionChanged", Q_ARG(quint64, 1)));
    QVERIFY(!buffer || !buffer->active());
    QVERIFY(gpu->createBuffer({{"size", 16}, {"usage", QStringList{"uniform"}}}));
    QVERIFY(!child->graphics()->createBuffer({{"size", 16}, {"usage", QStringList{"uniform"}}}));
    owner.reset();
    QVERIFY(host.runtime.setEnabled(host.installation(), false));
    QTRY_COMPARE(host.runtime.state(host.installation()), PluginRuntime::Disabled);
}
void PluginRuntimeTest::graphicsNativePixelsComputeAndRecovery()
{
    if (!qEnvironmentVariableIsSet("ECHO_TEST_NATIVE_GPU"))
        QSKIP("Native GPU opt-in; run on an exposed native window");
    Host host;
    auto *scope = graphicsScope(host);
    QVERIFY(scope);
    auto *gpu = scope->graphics();
    auto *vertex = gpu->createBuffer({{"size", 32}, {"usage", QStringList{"vertex"}}},
                                     host.engine.evaluate("[-1,1,1,1,1,-1,-1,-1]"));
    QVERIFY(vertex);
    auto *index =
        gpu->createBuffer({{"size", 12}, {"format", "uint16"}, {"usage", QStringList{"index"}}},
                          host.engine.evaluate("[0,1,2,0,2,3]"));
    QVERIFY(index);
    auto *uniform =
        gpu->createBuffer({{"size", 16}, {"usage", QStringList{"uniform"}}},
                          host.engine.evaluate("new Float32Array([.25,.5,.75,1]).buffer"));
    QVERIFY(uniform);
    auto *texture = gpu->createTexture(
        {{"width", 2}, {"height", 2}, {"format", "rgba8"}, {"usage", QStringList{"sampled"}}},
        host.engine.evaluate("[255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255]"));
    QVERIFY(texture);
    auto *sampler = gpu->createSampler();
    QVERIFY(sampler);
    auto *storage = gpu->createBuffer({{"size", 64}, {"usage", QStringList{"storage"}}});
    QVERIFY(storage);
    auto *floatTexture = gpu->createTexture(
        {{"width", 2},
         {"height", 2},
         {"format", "rgba16f"},
         {"usage", QStringList{"sampled", "storage", "readback", "render-target"}}});
    QVERIFY(floatTexture);
    const QVariantMap layout{
        {"stride", 8},
        {"attributes",
         QVariantList{QVariantMap{{"location", 0}, {"offset", 0}, {"format", "float2"}}}}};
    auto *pipeline = gpu->createRenderPipeline({{"vertexShader", "fixture.vert.qsb"},
                                                {"fragmentShader", "fixture.frag.qsb"},
                                                {"vertexLayout", layout}});
    QVERIFY2(pipeline, qPrintable(gpu->errorMessage()));
    auto *compute = gpu->createComputePipeline({{"computeShader", "fixture.comp.qsb"}});
    QVERIFY(compute);
    QQmlComponent component(&host.engine);
    component.setData(
        "import QtQuick\nimport EchoMusic.PluginSdk\nWindow {id:hostWindow;required property "
        "EchoPluginContext plugin;width:240;height:120;visible:false;EchoGpuSurface "
        "{objectName:'gpu';anchors.fill:parent;graphics:hostWindow.plugin?hostWindow.plugin."
        "graphics:null;dynamicRange:'sdr'}}",
        QUrl("qrc:/gpu-fixture.qml"));
    std::unique_ptr<QObject> object(
        component.createWithInitialProperties({{"plugin", QVariant::fromValue(scope)}}));
    QVERIFY2(object, qPrintable(component.errorString()));
    auto *window = qobject_cast<QQuickWindow *>(object.get());
    QVERIFY(window);
    auto *surface = window->findChild<EchoGpuSurface *>("gpu");
    QVERIFY(surface);
    auto imageBinding = binding(2, "texture", texture->id());
    imageBinding["sampler"] = sampler->id();
    QVariantMap draw{{"count", 6}, {"vertexBuffer", vertex->id()}, {"indexBuffer", index->id()}};
    QVariantMap render{
        {"type", "render"},
        {"pipeline", pipeline->id()},
        {"bindings", QVariantList{binding(0, "uniform", "frame"),
                                  binding(1, "uniform", uniform->id()), imageBinding}},
        {"draws", QVariantList{draw}}};
    QVariantMap dispatch{{"type", "compute"},
                         {"pipeline", compute->id()},
                         {"groups", QVariantList{4, 1, 1}},
                         {"bindings", QVariantList{binding(1, "uniform", uniform->id()),
                                                   QVariantMap{{"binding", 3},
                                                               {"kind", "storage-buffer"},
                                                               {"resource", storage->id()},
                                                               {"access", "write"}},
                                                   QVariantMap{{"binding", 4},
                                                               {"kind", "storage-image"},
                                                               {"resource", floatTexture->id()},
                                                               {"access", "write"}}}}};
    QVERIFY(surface->submit({{"passes", QVariantList{dispatch, render}}}));
    window->show();
    QVERIFY(QTest::qWaitForWindowExposed(window));
    QTRY_COMPARE_WITH_TIMEOUT(surface->status().value("state").toString(), "ready", 10000);
    QVERIFY(surface->status().value("compute").toBool());
    qInfo() << "GPU_CAPABILITIES" << surface->status();
    auto *pixels = surface->readback();
    QVERIFY(pixels);
    QTRY_VERIFY(pixels->finished());
    QCOMPARE(pixels->state(), EchoPluginOperation::Succeeded);
    auto packet = pixels->result().toMap();
    auto bytes = packet.value("data").toByteArray();
    QVERIFY(bytes.size() > 4);
    const int center = (packet.value("height").toInt() / 2 * packet.value("width").toInt() +
                        packet.value("width").toInt() / 2) *
                       4;
    QVERIFY(center + 4 <= bytes.size());
    QVERIFY(quint8(bytes[center]) > 110 && quint8(bytes[center]) < 160);
    QVERIFY(quint8(bytes[center + 1]) > 170 && quint8(bytes[center + 1]) < 205);
    QVERIFY(quint8(bytes[center + 2]) > 210);
    auto *numbers = surface->readback(storage);
    QTRY_VERIFY(numbers->finished());
    QCOMPARE(numbers->state(), EchoPluginOperation::Succeeded);
    const auto computed = numbers->result().toMap().value("data").toByteArray();
    QCOMPARE(computed.size(), 64);
    for (int i = 0; i < 16; ++i) {
        float value;
        std::memcpy(&value, computed.constData() + 4 * i, 4);
        QCOMPARE(value, float(i * 3) + .25f);
    }
    auto *hdr = surface->readback(floatTexture);
    QTRY_VERIFY(hdr->finished());
    QCOMPARE(hdr->state(), EchoPluginOperation::Succeeded);
    QCOMPARE(hdr->result().toMap().value("format").toString(), "rgba16f");
    const auto floating = hdr->result().toMap().value("data").toByteArray();
    qfloat16 first;
    std::memcpy(&first, floating.constData(), 2);
    QCOMPARE(float(first), 4.f);
    // Exercise real HDR output separately from offscreen float-texture support.
    if (surface->status().value("hdrSwapChain").toBool() &&
        surface->status().value("headroom").toDouble() > 1.01) {
        QVERIFY(gpu->writeBuffer(uniform, host.engine.evaluate("[4,4,4,1]")));
        surface->setDynamicRange("hdr");
        QTRY_VERIFY(surface->status().value("hdr").toBool());
        QTRY_VERIFY(window->property("echoHdrOutputActive").toBool());
        auto *extended = surface->readback();
        QTRY_VERIFY(extended->finished());
        QCOMPARE(extended->state(), EchoPluginOperation::Succeeded);
        const auto output = extended->result().toMap();
        QCOMPARE(output.value("format").toString(), "rgba16f");
        const auto data = output.value("data").toByteArray();
        qfloat16 value;
        std::memcpy(&value, data.constData(), 2);
        QVERIFY(float(value) > 1.f);
        qInfo() << "GPU_HDR_OUTPUT" << float(value) << surface->status();
        surface->setDynamicRange("sdr");
        QTRY_VERIFY(!surface->status().value("hdr").toBool());
    }
    QVERIFY(gpu->writeBuffer(uniform, host.engine.evaluate("[.75,.125,.25,1]")));
    auto *changed = surface->readback();
    QTRY_VERIFY(changed->finished());
    QCOMPARE(changed->state(), EchoPluginOperation::Succeeded);
    QVERIFY(changed->result().toMap().value("data").toByteArray() != bytes);
    const auto generation = surface->status().value("generation").toULongLong();
    surface->setContinuous(true);
    surface->setFps(30);
    window->hide();
    QTRY_VERIFY(gpu->drained());
    QTRY_COMPARE(surface->status().value("state").toString(), "suspended");
    QTest::qWait(50);
    const auto count = surface->status().value("frames").toULongLong();
    QTest::qWait(120);
    QCOMPARE(surface->status().value("frames").toULongLong(), count);
    window->setPersistentGraphics(false);
    window->setPersistentSceneGraph(false);
    window->releaseResources();
    window->show();
    QVERIFY(QTest::qWaitForWindowExposed(window));
    QTRY_COMPARE_WITH_TIMEOUT(surface->status().value("state").toString(), "ready", 10000);
    QVERIFY(surface->status().value("generation").toULongLong() > generation);
    auto *restored = surface->readback();
    QTRY_VERIFY(restored->finished());
    QCOMPARE(restored->state(), EchoPluginOperation::Succeeded);
    QCOMPARE(restored->result().toMap().value("data").toByteArray(),
             changed->result().toMap().value("data").toByteArray());
    QPointer<EchoPluginOperation> canceled = surface->readback(storage);
    canceled->cancel();
    QCOMPARE(canceled->state(), EchoPluginOperation::Canceled);
    QPointer<EchoPluginContext> retired = scope;
    QVERIFY(host.runtime.setEnabled(host.installation(), false));
    QTRY_COMPARE(host.runtime.state(host.installation()), PluginRuntime::Disabled);
    QTRY_VERIFY(retired.isNull());
    window->hide();
}
