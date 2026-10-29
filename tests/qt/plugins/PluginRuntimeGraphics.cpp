#include "PluginRuntimeTest.h"
#include "PluginRuntimeTestFixture.h"
#include "EchoGpuSurface.h"
#include <qfloat16.h>
#include <cstring>
namespace
{
EchoPluginContext *graphicsScope(Host &host)
{
    const auto source = package(
        host.temporary.path(), "export function activate(c) {}",
        {"lifecycle", "operations", "graphics.rhi", "graphics.compute", "graphics.readback"},
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
    QList<QPointer<EchoGpuResource>> leases;
    while (auto *sampler = gpu->createSampler())
        leases.append(sampler);
    QVERIFY(leases.size() < 256);
    QVERIFY(!child->graphics()->createSampler());
    for (auto resource : leases)
        if (resource)
            resource->dispose();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QVERIFY(child->graphics()->createSampler());
    std::vector<std::unique_ptr<EchoGpuSurface>> surfaces;
    for (int i = 0; i < 16; ++i) {
        auto surface = std::make_unique<EchoGpuSurface>();
        surface->setGraphics(gpu);
        QVERIFY(surface->graphics());
        surfaces.push_back(std::move(surface));
    }
    EchoGpuSurface overflow;
    overflow.setGraphics(gpu);
    QVERIFY(!overflow.graphics());
    surfaces.clear();
    overflow.setGraphics(gpu);
    QVERIFY(overflow.graphics());
    overflow.setGraphics(nullptr);
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
    QList<QJSValue> operationRoots;
    const auto retain = [&](EchoPluginOperation *op) {
        if (op)
            operationRoots.append(host.engine.newQObject(op));
        return op;
    };
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
        "EchoPluginContext plugin;width:320;height:180;visible:false;color:'transparent';"
        "Rectangle {width:80;height:180;color:'#4b7ca6'}"
        "Rectangle {x:80;width:80;height:180;color:'#804b7ca6'}EchoGpuSurface "
        "{objectName:'gpu';x:160;width:160;height:180;graphics:hostWindow.plugin?hostWindow.plugin."
        "graphics:null}}",
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
    // Test the final visible window, not only the private item framebuffer.
    QTest::qWait(80);
    const auto finalWindow = window->grabWindow();
    QVERIFY(!finalWindow.isNull());
    const auto visibleColor =
        finalWindow.pixelColor(finalWindow.width() * 3 / 4, finalWindow.height() / 2);
    QVERIFY2(visibleColor.blueF() > visibleColor.redF() + .1,
             "Plugin drawing must remain visible in the ordinary window");

    qInfo() << "GPU_CAPABILITIES" << surface->status();
    auto *pixels = retain(surface->readback());
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
    auto *numbers = retain(surface->readback(storage));
    QTRY_VERIFY(numbers->finished());
    QCOMPARE(numbers->state(), EchoPluginOperation::Succeeded);
    const auto computed = numbers->result().toMap().value("data").toByteArray();
    QCOMPARE(computed.size(), 64);
    for (int i = 0; i < 16; ++i) {
        float value;
        std::memcpy(&value, computed.constData() + 4 * i, 4);
        QCOMPARE(value, float(i * 3) + .25f);
    }
    auto *hdr = retain(surface->readback(floatTexture));
    QTRY_VERIFY(hdr->finished());
    QCOMPARE(hdr->state(), EchoPluginOperation::Succeeded);
    QCOMPARE(hdr->result().toMap().value("format").toString(), "rgba16f");
    const auto floating = hdr->result().toMap().value("data").toByteArray();
    qfloat16 first;
    std::memcpy(&first, floating.constData(), 2);
    QCOMPARE(float(first), 4.f);
    const auto builds = surface->status().value("pipelineBuilds").toULongLong();
    surface->setFps(30);
    surface->setContinuous(true);
    QTest::qWait(1100);
    QCOMPARE(surface->status().value("pipelineBuilds").toULongLong(), builds);
    surface->setContinuous(false);
    QCOMPARE(surface->dynamicRange(), "sdr");
    const auto windowFormat = window->requestedFormat();
    const auto rootItems = window->contentItem()->childItems();
    const auto opaqueColor =
        finalWindow.pixelColor(finalWindow.width() / 8, finalWindow.height() / 2);
    const auto alphaColor =
        finalWindow.pixelColor(finalWindow.width() * 3 / 8, finalWindow.height() / 2);
    QVERIFY(opaqueColor.alpha() > 250);
    QVERIFY(alphaColor.alpha() > 120 && alphaColor.alpha() < 136);
    QVERIFY(!window->property("_qt_sg_hdr_format").isValid());
    // A local HDR request must never recreate/reformat/reparent the host window.
    QSignalSpy graphInitialized(window, &QQuickWindow::sceneGraphInitialized);
    QVERIFY(gpu->writeBuffer(uniform, host.engine.evaluate("[4,4,4,1]")));
    surface->setDynamicRange("hdr");
    QTRY_VERIFY(surface->status().value("hdrRequested").toBool());
    QCOMPARE(surface->status().value("dynamicRange").toString(), "sdr");
    QVERIFY(!surface->status().value("hdr").toBool());
    QVERIFY(!surface->status().value("localHdrSupported").toBool());
    QCOMPARE(surface->status().value("reason").toString(), "local-hdr-presentation-unavailable");
    auto *mapped = retain(surface->readback());
    QTRY_VERIFY(mapped->finished());
    QCOMPARE(mapped->state(), EchoPluginOperation::Succeeded);
    QCOMPARE(mapped->result().toMap().value("format").toString(), "rgba8");
    const auto mappedBytes = mapped->result().toMap().value("data").toByteArray();
    QVERIFY(quint8(mappedBytes[center]) > 235 && quint8(mappedBytes[center]) < 255);
    const auto afterRequest = window->grabWindow();
    QCOMPARE(afterRequest.pixelColor(afterRequest.width() / 8, afterRequest.height() / 2),
             opaqueColor);
    QCOMPARE(afterRequest.pixelColor(afterRequest.width() * 3 / 8, afterRequest.height() / 2),
             alphaColor);
    QCOMPARE(window->requestedFormat(), windowFormat);
    QCOMPARE(window->contentItem()->childItems(), rootItems);
    QCOMPARE(graphInitialized.count(), 0);
    QVERIFY(!window->property("_qt_sg_hdr_format").isValid());
    surface->setDynamicRange("sdr");
    QTRY_VERIFY(!surface->status().value("hdrRequested").toBool());
    auto *depthPipeline = gpu->createRenderPipeline({{"vertexShader", "fixture.vert.qsb"},
                                                     {"fragmentShader", "fixture.frag.qsb"},
                                                     {"vertexLayout", layout},
                                                     {"depthTest", true},
                                                     {"depthWrite", true}});
    QVERIFY(depthPipeline);
    auto *redUniform = gpu->createBuffer({{"size", 16}, {"usage", QStringList{"uniform"}}},
                                         host.engine.evaluate("[.9,.1,.1,1]"));
    QVERIFY(redUniform);
    QVERIFY(gpu->writeBuffer(uniform, host.engine.evaluate("[.25,.5,.75,1]")));
    auto offscreen = render;
    offscreen["pipeline"] = depthPipeline->id();
    offscreen["target"] = floatTexture->id();
    auto preserve = offscreen;
    preserve["loadOp"] = "load";
    auto overlayBindings = preserve.value("bindings").toList();
    overlayBindings[1] = binding(1, "uniform", redUniform->id());
    preserve["bindings"] = overlayBindings;
    auto sampled = render;
    auto sourceBinding = imageBinding;
    sourceBinding["resource"] = floatTexture->id();
    auto sampledBindings = sampled.value("bindings").toList();
    sampledBindings[2] = sourceBinding;
    sampled["bindings"] = sampledBindings;
    QVERIFY(surface->submit({{"passes", QVariantList{offscreen, preserve, sampled}}}));
    auto *multipass = retain(surface->readback(floatTexture));
    QTRY_VERIFY(multipass->finished());
    QCOMPARE(multipass->state(), EchoPluginOperation::Succeeded);
    const auto multipassData = multipass->result().toMap().value("data").toByteArray();
    qfloat16 preserved;
    std::memcpy(&preserved, multipassData.constData(), 2);
    QCOMPARE(float(preserved), .25f);
    QVERIFY(surface->submit({{"passes", QVariantList{dispatch, render}}}));
    QVERIFY(gpu->writeBuffer(uniform, host.engine.evaluate("[.75,.125,.25,1]")));
    auto *changed = retain(surface->readback());
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
    auto *restored = retain(surface->readback());
    QTRY_VERIFY(restored->finished());
    QCOMPARE(restored->state(), EchoPluginOperation::Succeeded);
    QCOMPARE(restored->result().toMap().value("data").toByteArray(),
             changed->result().toMap().value("data").toByteArray());
    QPointer<EchoPluginOperation> canceled = retain(surface->readback(storage));
    canceled->cancel();
    QCOMPARE(canceled->state(), EchoPluginOperation::Canceled);
    QPointer<EchoPluginContext> retired = scope;
    QVERIFY(host.runtime.setEnabled(host.installation(), false));
    QTRY_COMPARE(host.runtime.state(host.installation()), PluginRuntime::Disabled);
    QTRY_VERIFY(retired.isNull());
    window->hide();
}
