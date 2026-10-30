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
    for (const auto &name : {"fixture.vert", "fixture.frag", "fixture.comp", "mrt.frag"}) {
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
    // Derived QQuickRhiItem members are gone when QQuickItem emits its final
    // windowChanged(nullptr). This used to invoke a freed connection list.
    QQuickWindow lifetimeWindow;
    for (int i = 0; i < 32; ++i) {
        auto *surface = new EchoGpuSurface;
        surface->setParentItem(lifetimeWindow.contentItem());
        QCOMPARE(surface->window(), &lifetimeWindow);
        delete surface;
    }
    QVERIFY(lifetimeWindow.contentItem()->childItems().isEmpty());

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

void PluginRuntimeTest::graphicsOrderedTransfersMipmapsAndCompletion()
{
    if (!qEnvironmentVariableIsSet("ECHO_TEST_NATIVE_GPU"))
        QSKIP("Requires an exposed native RHI window");
    Host host;
    auto *scope = graphicsScope(host);
    QVERIFY(scope);
    auto *gpu = scope->graphics();
    QList<QJSValue> roots;
    const auto keep = [&](EchoPluginOperation *operation) {
        if (operation)
            roots.append(host.engine.newQObject(operation));
        return operation;
    };
    const auto settled = [&](EchoPluginOperation *operation) {
        return operation && QTest::qWaitFor([&] { return operation->finished(); }, 7000);
    };
    auto *source = gpu->createTexture({{"width", 4},
                                       {"height", 4},
                                       {"mipmaps", true},
                                       {"usage", QStringList{"sampled", "readback"}}});
    auto *target = gpu->createTexture({{"width", 4},
                                       {"height", 4},
                                       {"mipmaps", true},
                                       {"usage", QStringList{"sampled", "readback"}}});
    auto *buffer =
        gpu->createBuffer({{"size", 64}, {"format", "uint32"}, {"usage", QStringList{"storage"}}},
                          host.engine.evaluate("[1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16]"));
    auto *copied =
        gpu->createBuffer({{"size", 64}, {"format", "uint32"}, {"usage", QStringList{"storage"}}});
    QVERIFY(source && target && buffer && copied);
    QQuickWindow window;
    window.resize(120, 80);
    window.setColor(Qt::transparent);
    EchoGpuSurface surface;
    surface.setParentItem(window.contentItem());
    surface.setSize(window.size());
    surface.setGraphics(gpu);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QTRY_VERIFY(surface.status().value("available").toBool());
    QVERIFY(surface.status().value("compute").toBool());
    const auto data = QByteArray::fromHex("ff0000ff00ff00ff000000000000ffffffffffff00000000");
    const QVariantMap upload{
        {"type", "upload-texture"},   {"destination", source->id()}, {"origin", QVariantList{1, 1}},
        {"size", QVariantList{2, 2}}, {"bytesPerRow", 12},           {"data", data}};
    const QVariantMap copy{{"type", "copy-texture"},
                           {"source", source->id()},
                           {"destination", target->id()},
                           {"sourceOrigin", QVariantList{1, 1}},
                           {"size", QVariantList{2, 2}}};
    const QVariantMap copyBuffer{{"type", "copy-buffer"},
                                 {"source", buffer->id()},
                                 {"destination", copied->id()},
                                 {"size", 64}};
    const QVariantMap patchBuffer{{"type", "upload-buffer"},
                                  {"destination", buffer->id()},
                                  {"offset", 4},
                                  {"data", QVariantList{999}}};
    auto partialCopy = copyBuffer;
    partialCopy["sourceOffset"] = 4;
    partialCopy["destinationOffset"] = 8;
    partialCopy["size"] = 4;
    auto *submission = keep(surface.enqueue(
        {{"passes", QVariantList{upload, copy,
                                 QVariantMap{{"type", "generate-mips"}, {"texture", target->id()}},
                                 copyBuffer, patchBuffer, partialCopy}}}));
    QVERIFY(submission && !submission->finished());
    auto *pixels = keep(surface.readback(target, {{"width", 2}, {"height", 2}}));
    auto *values = keep(surface.readback(copied, {{"offset", 0}, {"size", 16}}));
    auto *mip = keep(surface.readback(target, {{"level", 1}, {"width", 1}, {"height", 1}}));
    QVERIFY(settled(submission));
    QCOMPARE(submission->state(), EchoPluginOperation::Succeeded);
    QVERIFY(submission->result().toMap().value("serial").toULongLong() > 0);
    QCOMPARE(submission->result().toMap().value("backend").toString(),
             surface.status().value("backend").toString());
    QVERIFY(settled(pixels) && settled(values) && settled(mip));
    QCOMPARE(pixels->state(), EchoPluginOperation::Succeeded);
    QCOMPARE(pixels->result().toMap().value("data").toByteArray(),
             QByteArray::fromHex("ff0000ff00ff00ff0000ffffffffffff"));
    QCOMPARE(values->state(), EchoPluginOperation::Succeeded);
    const auto bytes = values->result().toMap().value("data").toByteArray();
    QCOMPARE(bytes.size(), 16);
    quint32 numbers[4];
    std::memcpy(numbers, bytes.constData(), 16);
    QCOMPARE(numbers[0], 1u);
    QCOMPARE(numbers[1], 2u);
    QCOMPARE(numbers[2], 999u);
    QCOMPARE(numbers[3], 4u);
    QCOMPARE(mip->state(), EchoPluginOperation::Succeeded);
    const auto averaged = mip->result().toMap().value("data").toByteArray();
    QCOMPARE(averaged.size(), 4);
    for (int i = 0; i < 3; ++i)
        QVERIFY(quint8(averaged[i]) >= 127 && quint8(averaged[i]) <= 128);
    QCOMPARE(quint8(averaged[3]), 255u);
    QCOMPARE(surface.status().value("copyPipelineBuilds").toULongLong(), 1u);
    QTRY_COMPARE(gpu->memoryBudget()->pendingSubmissions.load(), 0);

    // Resource definitions are captured now; later CPU writes belong to a later frame.
    auto *snapshot = keep(surface.enqueue({{"passes", QVariantList{copyBuffer}}}));
    QVERIFY(gpu->writeBuffer(buffer, host.engine.evaluate("[111]")));
    QVERIFY(settled(snapshot));
    QCOMPARE(snapshot->state(), EchoPluginOperation::Succeeded);
    auto *before = keep(surface.readback(copied, {{"size", 4}}));
    QVERIFY(settled(before));
    quint32 first = 0;
    const auto prior = before->result().toMap().value("data").toByteArray();
    QCOMPARE(prior.size(), 4);
    std::memcpy(&first, prior.constData(), 4);
    QCOMPARE(first, 1u);

    QList<EchoPluginOperation *> queued;
    for (int i = 0; i < 8; ++i) {
        auto *op = keep(surface.enqueue({{"passes", QVariantList{copyBuffer}}}));
        QVERIFY(op && !op->finished());
        queued.append(op);
    }
    auto *overflow = keep(surface.enqueue({{"passes", QVariantList{copyBuffer}}}));
    QCOMPARE(overflow->errorCode(), "Busy");
    for (auto *op : queued)
        op->cancel();
    QTRY_COMPARE(gpu->memoryBudget()->pendingSubmissions.load(), 0);
    auto invalid = upload;
    invalid["origin"] = QVariantList{4, 0};
    auto *bad = keep(surface.enqueue({{"passes", QVariantList{invalid}}}));
    QCOMPARE(bad->errorCode(), "InvalidArgument");

    // Device/window loss discards GPU-only contents; fresh CPU definitions recover.
    auto *opacityCanceled = keep(surface.enqueue({{"passes", QVariantList{copyBuffer}}}));
    surface.setOpacity(0);
    QCOMPARE(opacityCanceled->state(), EchoPluginOperation::Canceled);
    QTRY_VERIFY(surface.drained());
    surface.setOpacity(1);
    QTRY_VERIFY(surface.live());
    auto *hidden = keep(surface.enqueue({{"passes", QVariantList{copyBuffer}}}));
    window.hide();
    QCOMPARE(hidden->state(), EchoPluginOperation::Canceled);
    QTRY_VERIFY(surface.drained());
    QTRY_COMPARE(gpu->memoryBudget()->submissions.used.load(), 0);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    auto *restored = keep(surface.enqueue({{"passes", QVariantList{copyBuffer}}}));
    QVERIFY(settled(restored));
    QCOMPARE(restored->state(), EchoPluginOperation::Succeeded);
    auto *after = keep(surface.readback(copied, {{"size", 4}}));
    QVERIFY(settled(after));
    const auto current = after->result().toMap().value("data").toByteArray();
    QCOMPARE(current.size(), 4);
    std::memcpy(&first, current.constData(), 4);
    QCOMPARE(first, 111u);

    const auto alignment = surface.status().value("uniformBufferAlignment").toInt();
    QVERIFY(alignment > 0);
    auto *uniform =
        gpu->createBuffer({{"size", alignment + 16}, {"usage", QStringList{"uniform"}}});
    auto *vertices = gpu->createBuffer({{"size", 40}, {"usage", QStringList{"vertex"}}},
                                       host.engine.evaluate("[0,0,-1,1,1,1,-1,-1,1,-1]"));
    auto *indices =
        gpu->createBuffer({{"size", 12}, {"usage", QStringList{"index"}}, {"format", "uint16"}},
                          host.engine.evaluate("[0,1,2,2,1,3]"));
    auto *white =
        gpu->createTexture({{"width", 1}, {"height", 1}, {"usage", QStringList{"sampled"}}},
                           host.engine.evaluate("[255,255,255,255]"));
    auto *sampler = gpu->createSampler({{"mipFilter", "linear"}});
    auto *pipeline = gpu->createRenderPipeline(
        {{"vertexShader", "fixture.vert.qsb"},
         {"fragmentShader", "fixture.frag.qsb"},
         {"topology", "triangles"},
         {"vertexLayout",
          QVariantMap{
              {"stride", 8},
              {"attributes",
               QVariantList{QVariantMap{{"location", 0}, {"offset", 0}, {"format", "float2"}}}}}}});
    QVERIFY(uniform && vertices && indices && white && sampler && pipeline);
    QVERIFY(gpu->writeBuffer(uniform, host.engine.evaluate("[.25,.5,.75,1]"), alignment));
    QVariantMap color = binding(1, "uniform", uniform->id());
    color["offset"] = alignment;
    color["size"] = 16;
    QVariantMap sample = binding(2, "texture", white->id());
    sample["sampler"] = sampler->id();
    const auto physical = surface.effectiveColorBufferSize();
    QVariantMap render{
        {"type", "render"},
        {"pipeline", pipeline->id()},
        {"bindings", QVariantList{binding(0, "uniform", "frame"), color, sample}},
        {"viewport", QVariantList{0, 0, physical.width(), physical.height()}},
        {"scissor", QVariantList{0, 0, physical.width() / 2, physical.height()}},
        {"draws",
         QVariantList{QVariantMap{
             {"count", 6},
             {"vertexBuffer", vertices->id()},
             {"vertexOffset", surface.status().value("baseVertex").toBool() ? 0 : 8},
             {"indexBuffer", indices->id()},
             {"baseVertex", surface.status().value("baseVertex").toBool() ? 1 : 0},
             {"firstInstance", surface.status().value("baseInstance").toBool() ? 1 : 0}}}}};
    auto *rendered = keep(surface.enqueue({{"passes", QVariantList{render}}}));
    auto *image = keep(surface.readback());
    QVERIFY(settled(rendered) && settled(image));
    QCOMPARE(rendered->state(), EchoPluginOperation::Succeeded);
    QCOMPARE(image->state(), EchoPluginOperation::Succeeded);
    const auto packet = image->result().toMap();
    const auto imageBytes = packet.value("data").toByteArray();
    const int width = packet.value("width").toInt(), height = packet.value("height").toInt();
    const auto left = (height / 2 * width + width / 4) * 4,
               right = (height / 2 * width + width * 3 / 4) * 4;
    QVERIFY(quint8(imageBytes[left + 2]) > 210);
    QCOMPARE(quint8(imageBytes[right + 3]), 0u);
    if (surface.status().value("baseVertex").toBool())
        for (const auto y : {height / 8, height * 7 / 8})
            QVERIFY(quint8(imageBytes[(y * width + width * 3 / 8) * 4 + 3]) > 250);
    const auto builds = surface.status().value("pipelineBuilds").toULongLong();
    render["scissor"] =
        QVariantList{physical.width() / 2, 0, physical.width() / 2, physical.height()};
    auto *redraw = keep(surface.enqueue({{"passes", QVariantList{render}}}));
    QVERIFY(settled(redraw));
    QCOMPARE(redraw->state(), EchoPluginOperation::Succeeded);
    QCOMPARE(surface.status().value("pipelineBuilds").toULongLong(), builds);
    const auto memory = gpu->memoryBudget();
    auto *retired = keep(surface.enqueue({{"passes", QVariantList{copyBuffer}}}));
    QVERIFY(host.runtime.setEnabled(host.installation(), false));
    QTRY_COMPARE(host.runtime.state(host.installation()), PluginRuntime::Disabled);
    QVERIFY(retired->finished());
    QTRY_COMPARE(memory->pendingSubmissions.load(), 0);
    surface.setGraphics(nullptr);
}
void PluginRuntimeTest::graphicsMultipleTargetsMipRenderingAndLifetime()
{
    Host host;
    auto *scope = graphicsScope(host);
    QVERIFY(scope);
    auto *gpu = scope->graphics();
    const auto memory = gpu->memoryBudget();
    auto *a = gpu->createTexture({{"width", 4},
                                  {"height", 4},
                                  {"mipmaps", true},
                                  {"usage", QStringList{"render-target", "readback"}}});
    auto *b = gpu->createTexture({{"width", 4},
                                  {"height", 4},
                                  {"mipmaps", true},
                                  {"usage", QStringList{"render-target", "readback"}}});
    auto *vertices = gpu->createBuffer({{"size", 32}, {"usage", QStringList{"vertex"}}},
                                       host.engine.evaluate("[-1,1,1,1,-1,-1,1,-1]"));
    const QVariantMap layout{
        {"stride", 8},
        {"attributes",
         QVariantList{QVariantMap{{"location", 0}, {"offset", 0}, {"format", "float2"}}}}};
    auto definition = QVariantMap{{"vertexShader", "fixture.vert.qsb"},
                                  {"fragmentShader", "mrt.frag.qsb"},
                                  {"vertexLayout", layout},
                                  {"topology", "triangle-strip"},
                                  {"blend", "none"},
                                  {"depthTest", true}};
    auto *pipeline = gpu->createRenderPipeline(definition);
    QVERIFY(a && b && vertices && pipeline);
    QVariantList views{QVariantMap{{"texture", a->id()}, {"level", 1}},
                       QVariantMap{{"texture", b->id()}, {"level", 1}}};
    QVariantMap pass{
        {"type", "render"},
        {"pipeline", pipeline->id()},
        {"colorTargets", views},
        {"bindings", QVariantList{binding(0, "uniform", "frame")}},
        {"draws", QVariantList{QVariantMap{{"count", 4}, {"vertexBuffer", vertices->id()}}}}};
    QString error;
    QVERIFY(gpu->validateFrame({{"passes", QVariantList{pass}}}, error));
    auto invalid = pass;
    invalid["target"] = "surface";
    QVERIFY(!gpu->validateFrame({{"passes", QVariantList{invalid}}}, error));
    invalid = pass;
    invalid["colorTargets"] = QVariantList{views.first(), views.first()};
    QVERIFY(!gpu->validateFrame({{"passes", QVariantList{invalid}}}, error));
    invalid["colorTargets"] =
        QVariantList{views.first(), QVariantMap{{"texture", b->id()}, {"level", 0}}};
    QVERIFY(!gpu->validateFrame({{"passes", QVariantList{invalid}}}, error));
    invalid["colorTargets"] = QVariantList{QVariantMap{{"texture", a->id()}, {"level", 3}}};
    QVERIFY(!gpu->validateFrame({{"passes", QVariantList{invalid}}}, error));
    invalid["colorTargets"] = QVariantList{};
    QVERIFY(!gpu->validateFrame({{"passes", QVariantList{invalid}}}, error));
    auto owner = std::make_unique<QObject>();
    auto *child = scope->createPresentation(owner.get(), "mrt-owner");
    QVERIFY(child && !child->graphics()->validateFrame({{"passes", QVariantList{pass}}}, error));
    owner.reset();
    if (!qEnvironmentVariableIsSet("ECHO_TEST_NATIVE_GPU"))
        return;
    QQuickWindow window;
    window.resize(120, 80);
    EchoGpuSurface surface;
    surface.setParentItem(window.contentItem());
    surface.setSize(window.size());
    surface.setGraphics(gpu);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QTRY_VERIFY(surface.status().value("available").toBool());
    if (!surface.status().value("multipleRenderTargets").toBool() ||
        !surface.status().value("renderMipmaps").toBool())
        QSKIP("Device lacks MRT or non-base mip rendering");
    QList<QJSValue> roots;
    const auto keep = [&](EchoPluginOperation *op) {
        if (op)
            roots.append(host.engine.newQObject(op));
        return op;
    };
    const auto success = [&](EchoPluginOperation *op) {
        return op && QTest::qWaitFor([&] { return op->finished(); }, 7000) &&
               op->state() == EchoPluginOperation::Succeeded;
    };
    const bool independent = surface.status().value("independentBlend").toBool();
    auto incomplete = pass;
    incomplete["colorTargets"] = QVariantList{views.first()};
    auto *missingOutput = keep(surface.enqueue({{"passes", QVariantList{incomplete}}}));
    QVERIFY(missingOutput);
    QTRY_VERIFY(missingOutput->finished());
    QCOMPARE(missingOutput->state(), EchoPluginOperation::Failed);
    QVERIFY(missingOutput->errorMessage().contains("output location"));
    definition["depthTest"] = false;
    definition["blends"] = QVariantList{"none", independent ? "add" : "none"};
    auto *second = gpu->createRenderPipeline(definition);
    QVERIFY(second);
    auto load = pass;
    load["pipeline"] = second->id();
    load["loadOp"] = "load";
    auto *submitted = keep(surface.enqueue({{"passes", QVariantList{pass, load}}}));
    QVERIFY2(success(submitted),
             submitted ? qPrintable(submitted->errorMessage()) : "no operation");
    const auto check = [&] {
        auto *red = keep(surface.readback(a, {{"level", 1}}));
        auto *green = keep(surface.readback(b, {{"level", 1}}));
        if (!success(red) || !success(green))
            return false;
        const auto r = red->result().toMap(), g = green->result().toMap();
        if (r.value("width").toInt() != 2 || g.value("height").toInt() != 2)
            return false;
        const auto rb = r.value("data").toByteArray(), gb = g.value("data").toByteArray();
        if (rb.size() != 16 || gb.size() != 16)
            return false;
        for (int i = 0; i < 16; i += 4)
            if (qAbs(int(quint8(rb[i])) - 64) > 1 || quint8(rb[i + 1]) != 0 ||
                qAbs(int(quint8(gb[i + 1])) - (independent ? 128 : 64)) > 1 || quint8(gb[i]) != 0)
                return false;
        return true;
    };
    QVERIFY(check());
    if (surface.status().value("compute").toBool()) {
        auto *image = gpu->createTexture({{"width", 4},
                                          {"height", 4},
                                          {"format", "rgba16f"},
                                          {"mipmaps", true},
                                          {"usage", QStringList{"storage", "readback"}}});
        auto *values = gpu->createBuffer({{"size", 64}, {"usage", QStringList{"storage"}}});
        auto *color = gpu->createBuffer({{"size", 16}, {"usage", QStringList{"uniform"}}},
                                        host.engine.evaluate("[0,0,0,0]"));
        auto *compute = gpu->createComputePipeline({{"computeShader", "fixture.comp.qsb"}});
        QVERIFY(image && values && color && compute);
        auto imageView = binding(4, "storage-image", image->id());
        imageView["access"] = "write";
        imageView["level"] = 1;
        auto output = binding(3, "storage-buffer", values->id());
        output["access"] = "write";
        const QVariantMap dispatch{
            {"type", "compute"},
            {"pipeline", compute->id()},
            {"groups", QVariantList{1, 1, 1}},
            {"bindings", QVariantList{binding(1, "uniform", color->id()), output, imageView}}};
        QVERIFY(success(keep(surface.enqueue({{"passes", QVariantList{dispatch}}}))));
        auto *read = keep(surface.readback(image, {{"level", 1}}));
        QVERIFY(success(read));
        const auto bytes = read->result().toMap().value("data").toByteArray();
        QCOMPARE(bytes.size(), 32);
        for (int i = 0; i < 16; ++i) {
            qfloat16 value;
            std::memcpy(&value, bytes.constData() + i * 2, 2);
            const float expected[] = {4., 2., .5, 1.};
            QCOMPARE(float(value), expected[i % 4]);
        }
    }
    auto *canceled = keep(surface.enqueue({{"passes", QVariantList{pass}}}));
    window.hide();
    QTRY_VERIFY(canceled->finished());
    QCOMPARE(canceled->state(), EchoPluginOperation::Canceled);
    QTRY_COMPARE(memory->pendingSubmissions.load(), 0);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QTRY_VERIFY(surface.status().value("available").toBool());
    QVERIFY(success(keep(surface.enqueue({{"passes", QVariantList{pass, load}}}))));
    QVERIFY(check());
    const auto installation = scope->installationId();
    QVERIFY(host.runtime.setEnabled(installation, false));
    QTRY_COMPARE(host.runtime.state(installation), PluginRuntime::Disabled);
    QTRY_COMPARE(memory->native.used.load(), 0);
    QTRY_COMPARE(memory->targets.used.load(), 0);
}
