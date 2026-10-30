#include "PluginRuntimeGraphicsFixture.h"
#include <cstring>
namespace
{
bool floats(GraphicsFixture &f, EchoGpuSurface &surface, EchoGpuResource *buffer, float first)
{
    auto *op = surface.readback(buffer);
    if (!f.success(op))
        return false;
    const auto bytes = op->result().toMap().value("data").toByteArray();
    if (bytes.size() != 16)
        return false;
    float result[4];
    std::memcpy(result, bytes.constData(), 16);
    for (int i = 0; i < 4; ++i)
        if (result[i] != first + i)
            return false;
    return true;
}
} // namespace
void PluginRuntimeTest::graphicsSharedResourcesScopesDevicesAndRecovery()
{
    GraphicsFixture f;
    QVERIFY(f.gpu);
    auto *root = f.gpu;
    const auto memory = root->memoryBudget();
    auto *values = root->createBuffer({{"size", 16}, {"usage", QStringList{"storage"}}});
    auto *image = root->createTexture(
        {{"width", 2},
         {"height", 2},
         {"usage", QStringList{"storage", "sampled", "readback", "render-target"}}});
    auto *uniform = root->createBuffer({{"size", 16}, {"usage", QStringList{"uniform"}}});
    QVERIFY(values && image && uniform);
    QVERIFY(root->shareResource(uniform).isEmpty());
    const auto valuesToken = root->shareResource(values, "read-write"),
               imageToken = root->shareResource(image, "read-write");
    QVERIFY(!valuesToken.isEmpty() && !imageToken.isEmpty());
    QVERIFY(values->shared());
    QCOMPARE(root->shareResource(values, "read-write"), valuesToken);
    QVERIFY(root->shareResource(values, "read").isEmpty());
    auto producerOwner = std::make_unique<QObject>(), consumerOwner = std::make_unique<QObject>();
    auto *producerScope = f.scope->createPresentation(producerOwner.get(), "shared-producer");
    auto *consumerScope = f.scope->createPresentation(consumerOwner.get(), "shared-consumer");
    QVERIFY(producerScope && consumerScope);
    auto *a = producerScope->graphics(), *b = consumerScope->graphics();
    const auto logical = root->capabilities().value("logicalBytes");
    auto *writeValues = a->importResource(valuesToken, "read-write"),
         *writeImage = a->importResource(imageToken, "read-write");
    auto *readValues = b->importResource(valuesToken), *readImage = b->importResource(imageToken);
    QVERIFY(writeValues && writeImage && readValues && readImage);
    QVERIFY(readValues->readOnly());
    QVERIFY(!writeValues->readOnly());
    QCOMPARE(root->capabilities().value("logicalBytes"), logical);
    QVERIFY(!b->writeBuffer(readValues, f.host.engine.evaluate("[1,2,3,4]")));
    QVERIFY(!b->writeTexture(readImage, f.host.engine.evaluate("[0,0,0,0]")));
    QVERIFY(a->shareResource(values).isEmpty());
    auto *readOnlySource = root->createBuffer({{"size", 4}, {"usage", QStringList{"storage"}}});
    const auto readOnlyToken = root->shareResource(readOnlySource);
    QVERIFY(!a->importResource(readOnlyToken, "read-write"));
    QVERIFY(!b->importResource("foreign-token"));
    GraphicsFixture isolatedPlugin;
    QVERIFY(isolatedPlugin.gpu);
    QVERIFY(!isolatedPlugin.gpu->importResource(valuesToken));
    QVariantMap normalized;
    QString error;
    const QVariantMap forbidden{{"type", "upload-buffer"},
                                {"destination", readValues->id()},
                                {"data", QVariantList{1, 2, 3, 4}}};
    QVERIFY(!b->prepareSubmission({{"passes", QVariantList{forbidden}}}, normalized, error));
    auto *compute = a->createComputePipeline({{"computeShader", "shared.comp.qsb"}});
    QVERIFY(compute);
    const QVariantMap dispatch{
        {"type", "compute"},
        {"pipeline", compute->id()},
        {"groups", QVariantList{1, 1, 1}},
        {"bindings", QVariantList{bind(3, "storage-buffer", writeValues->id(), "write"),
                                  bind(4, "storage-image", writeImage->id(), "write")}}};
    auto denied = dispatch;
    denied["pipeline"] = b->createComputePipeline({{"computeShader", "shared.comp.qsb"}})->id();
    denied["bindings"] = QVariantList{bind(3, "storage-buffer", readValues->id(), "write"),
                                      bind(4, "storage-image", readImage->id(), "write")};
    QVERIFY(!b->validateFrame({{"passes", QVariantList{denied}}}, error));
    if (!qEnvironmentVariableIsSet("ECHO_TEST_NATIVE_GPU")) {
        QVERIFY(root->revokeSharing(values));
        QVERIFY(!readValues->active());
        return;
    }
    QQuickWindow window;
    window.setPersistentGraphics(false);
    window.setPersistentSceneGraph(false);
    window.resize(220, 100);
    EchoGpuSurface producer, consumer;
    producer.setParentItem(window.contentItem());
    producer.setSize({100, 80});
    producer.setGraphics(a);
    consumer.setParentItem(window.contentItem());
    consumer.setPosition({110, 0});
    consumer.setSize({100, 80});
    consumer.setGraphics(b);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QVERIFY2(f.success(producer.enqueue({{"passes", QVariantList{dispatch}}})),
             qPrintable(a->errorMessage()));
    QVERIFY(floats(f, consumer, readValues, 31));
    QVERIFY(f.pixels(consumer, readImage, {}, 64, 128, 191));
    auto *samplePipeline = b->createRenderPipeline(
        {{"vertexShader", "fixture.vert.qsb"},
         {"fragmentShader", "fixture.frag.qsb"},
         {"topology", "triangle-strip"},
         {"blend", "none"},
         {"vertexLayout",
          QVariantMap{
              {"stride", 8},
              {"attributes",
               QVariantList{QVariantMap{{"location", 0}, {"offset", 0}, {"format", "float2"}}}}}}});
    auto *quad = b->createBuffer({{"size", 32}, {"usage", QStringList{"vertex"}}},
                                 f.host.engine.evaluate("[-1,1,1,1,-1,-1,1,-1]"));
    auto *color = b->createBuffer({{"size", 16}, {"usage", QStringList{"uniform"}}},
                                  f.host.engine.evaluate("[1,1,1,1]"));
    auto *sampler = b->createSampler();
    QVERIFY(samplePipeline && quad && color && sampler);
    auto texture = bind(2, "texture", readImage->id());
    texture["sampler"] = sampler->id();
    const QVariantMap sampled{
        {"type", "render"},
        {"pipeline", samplePipeline->id()},
        {"bindings",
         QVariantList{bind(0, "uniform", "frame"), bind(1, "uniform", color->id()), texture}},
        {"draws", QVariantList{QVariantMap{{"count", 4}, {"vertexBuffer", quad->id()}}}}};
    QVERIFY(f.success(consumer.enqueue({{"passes", QVariantList{sampled}}})));
    QVERIFY(f.pixels(consumer, nullptr, {}, 137, 188, 225));
    QCOMPARE(producer.status().value("deviceId"), consumer.status().value("deviceId"));
    QCOMPARE(values->generation(), readValues->generation());
    QVERIFY(values->generation() > 0);
    const auto charged = memory->native.used.load();
    auto *another = b->importResource(valuesToken);
    QVERIFY(another);
    QVERIFY(floats(f, consumer, another, 31));
    QCOMPARE(memory->native.used.load(), charged);
    producer.setPaused(true);
    QTRY_VERIFY(producer.drained());
    QVERIFY(floats(f, consumer, readValues, 31));
    QVERIFY(f.pixels(consumer, readImage, {}, 64, 128, 191));
    producer.setVisible(false);
    producer.setPaused(false);
    QVERIFY(a->writeBuffer(writeValues, f.host.engine.evaluate("[77,78,79,80]")));
    QVERIFY(floats(f, consumer, readValues, 77));
    // A separate Qt Quick window normally has another QRhi/render thread. It must
    // not quietly allocate a replica containing only the source's CPU seed.
    auto foreignOwner = std::make_unique<QObject>();
    auto *foreign = f.scope->createPresentation(foreignOwner.get(), "other-device");
    QVERIFY(foreign);
    auto *foreignImage = foreign->graphics()->importResource(imageToken);
    QVERIFY(foreignImage);
    QQuickWindow otherWindow;
    otherWindow.resize(60, 60);
    EchoGpuSurface other;
    other.setParentItem(otherWindow.contentItem());
    other.setSize(otherWindow.size());
    other.setGraphics(foreign->graphics());
    otherWindow.show();
    QVERIFY(QTest::qWaitForWindowExposed(&otherWindow));
    QTRY_VERIFY(!other.status().value("deviceId").toString().isEmpty());
    if (other.status().value("deviceId") != consumer.status().value("deviceId")) {
        QTRY_COMPARE(other.status().value("state").toString(), QString("error"));
        QVERIFY(other.status().value("reason").toString().contains("different RHI"));
    } else {
        QVERIFY(f.pixels(other, foreignImage, {}, 64, 128, 191));
    }
    other.setVisible(false);
    QTRY_VERIFY(other.drained());
    otherWindow.hide();
    const auto generation = values->generation();
    consumer.setVisible(false);
    QTRY_VERIFY(consumer.drained());
    QTRY_COMPARE(memory->native.used.load(), 0);
    window.hide();
    window.releaseResources();
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    producer.setVisible(true);
    consumer.setVisible(true);
    QTRY_VERIFY(producer.live() && consumer.live());
    QVERIFY(f.success(producer.enqueue({{"passes", QVariantList{dispatch}}})));
    QVERIFY(floats(f, consumer, readValues, 31));
    QVERIFY(values->generation() > generation);
    // Revocation removes pending submissions without invalidating the owned source.
    for (int i = 0; i < 10; ++i) {
        auto *discard = f.keep(consumer.readback(readValues));
        QVERIFY(discard);
        discard->cancel();
    }
    QPointer<EchoGpuResource> revokedValues = readValues;
    auto *pending = f.keep(consumer.enqueue(
        {{"passes", QVariantList{QVariantMap{
                        {"type", "copy-buffer"},
                        {"source", readValues->id()},
                        {"destination",
                         b->createBuffer({{"size", 16}, {"usage", QStringList{"storage"}}})->id()},
                        {"size", 16}}}}}));
    QVERIFY(pending);
    auto *reading = f.keep(consumer.readback(readValues));
    QVERIFY(reading);
    {
        // Renderer handoff drains the waiting queue; cancellation must still own the operation.
        const auto handedOff = consumer.takeSnapshot();
        QCOMPARE(handedOff.readbacks.size(), 1);
        QCOMPARE(handedOff.readbacks.first().second.data(), reading);
        QVERIFY(root->revokeSharing(values));
        QCOMPARE(reading->state(), EchoPluginOperation::Canceled);
    }
    QTRY_VERIFY(pending->finished());
    QCOMPARE(pending->state(), EchoPluginOperation::Canceled);
    QVERIFY(!revokedValues || !revokedValues->active());
    QVERIFY(values->active());
    QVERIFY(!b->importResource(valuesToken));
    auto *revoked = f.keep(consumer.readback(readImage));
    QVERIFY(revoked);
    QVERIFY(QMetaObject::invokeMethod(&f.host.api, "sessionChanged", Q_ARG(quint64, 1)));
    QTRY_VERIFY(revoked->finished());
    QCOMPARE(revoked->state(), EchoPluginOperation::Canceled);
    QVERIFY(!root->importResource(imageToken));
    producerOwner.reset();
    consumerOwner.reset();
    foreignOwner.reset();
    QVERIFY(f.host.runtime.setEnabled(f.host.installation(), false));
    QTRY_COMPARE(f.host.runtime.state(f.host.installation()), PluginRuntime::Disabled);
    QTRY_VERIFY(producer.drained() && consumer.drained());
    QCOMPARE(memory->native.used.load(), 0);
    QCOMPARE(memory->targets.used.load(), 0);
    QCOMPARE(memory->readbacks.used.load(), 0);
    QCOMPARE(memory->submissions.used.load(), 0);
}
void PluginRuntimeTest::graphicsMultipleStreamsInstancesAndReflection()
{
    GraphicsFixture f;
    QVERIFY(f.gpu);
    auto *gpu = f.gpu;
    const auto memory = gpu->memoryBudget();
    const QVariantList streams{QVariantMap{{"stride", 8}},
                               QVariantMap{{"stride", 8}, {"stepMode", "instance"}},
                               QVariantMap{{"stride", 4}, {"stepMode", "instance"}}};
    const QVariantList attributes{
        QVariantMap{{"binding", 0}, {"location", 0}, {"offset", 0}, {"format", "float2"}},
        QVariantMap{{"binding", 1}, {"location", 1}, {"offset", 0}, {"format", "float2"}},
        QVariantMap{{"binding", 2}, {"location", 2}, {"offset", 0}, {"format", "unorm8x4"}}};
    const QVariantMap definition{
        {"vertexShader", "instanced.vert.qsb"},
        {"fragmentShader", "instanced.frag.qsb"},
        {"blend", "none"},
        {"vertexLayout", QVariantMap{{"bindings", streams}, {"attributes", attributes}}}};
    auto *signedBytes =
        gpu->createBuffer({{"size", 4}, {"usage", QStringList{"vertex"}}, {"format", "int8"}},
                          f.host.engine.evaluate("[-128,127,-1,0]"));
    QVERIFY(signedBytes);
    QVERIFY(!gpu->writeBuffer(signedBytes, f.host.engine.evaluate("[128]")));
    auto *pipeline = gpu->createRenderPipeline(definition);
    QVERIFY(pipeline);
    auto invalid = definition;
    invalid["vertexLayout"] =
        QVariantMap{{"stride", 8}, {"bindings", streams}, {"attributes", attributes}};
    QVERIFY(!gpu->createRenderPipeline(invalid));
    auto badStreams = streams;
    badStreams[0] = QVariantMap{{"stride", 7}};
    invalid["vertexLayout"] = QVariantMap{{"bindings", badStreams}, {"attributes", attributes}};
    QVERIFY(!gpu->createRenderPipeline(invalid));
    auto badAttributes = attributes;
    auto bad = badAttributes[1].toMap();
    bad["binding"] = 3;
    badAttributes[1] = bad;
    invalid["vertexLayout"] = QVariantMap{{"bindings", streams}, {"attributes", badAttributes}};
    QVERIFY(!gpu->createRenderPipeline(invalid));
    bad = attributes[2].toMap();
    bad["offset"] = 4;
    badAttributes = attributes;
    badAttributes[2] = bad;
    invalid["vertexLayout"] = QVariantMap{{"bindings", streams}, {"attributes", badAttributes}};
    QVERIFY(!gpu->createRenderPipeline(invalid));
    auto *positions = gpu->createBuffer({{"size", 24}, {"usage", QStringList{"vertex"}}},
                                        f.host.engine.evaluate("[-.4,-.4,.4,-.4,0,.4]"));
    auto *offsets = gpu->createBuffer({{"size", 16}, {"usage", QStringList{"vertex"}}},
                                      f.host.engine.evaluate("[-.5,0,.5,0]"));
    auto *colors =
        gpu->createBuffer({{"size", 8}, {"usage", QStringList{"vertex"}}, {"format", "uint8"}},
                          f.host.engine.evaluate("[255,0,0,255,0,255,0,255]"));
    QVERIFY(positions && offsets && colors);
    const QVariantList inputs{QVariantMap{{"buffer", positions->id()}},
                              QVariantMap{{"buffer", offsets->id()}},
                              QVariantMap{{"buffer", colors->id()}}};
    QVariantMap draw{{"count", 3}, {"instances", 2}, {"vertexBuffers", inputs}};
    QVariantMap pass{{"type", "render"},
                     {"pipeline", pipeline->id()},
                     {"bindings", QVariantList{bind(0, "uniform", "frame")}},
                     {"draws", QVariantList{draw}}};
    QString error;
    QVERIFY(gpu->validateFrame({{"passes", QVariantList{pass}}}, error));
    auto mixed = draw;
    mixed["vertexBuffer"] = positions->id();
    auto wrong = pass;
    wrong["draws"] = QVariantList{mixed};
    QVERIFY(!gpu->validateFrame({{"passes", QVariantList{wrong}}}, error));
    mixed = draw;
    mixed["vertexBuffers"] = QVariantList{inputs[0]};
    wrong["draws"] = QVariantList{mixed};
    QVERIFY(!gpu->validateFrame({{"passes", QVariantList{wrong}}}, error));
    auto nonAligned = inputs;
    nonAligned[1] = QVariantMap{{"buffer", offsets->id()}, {"offset", 2}};
    mixed = draw;
    mixed["vertexBuffers"] = nonAligned;
    wrong["draws"] = QVariantList{mixed};
    QVERIFY(!gpu->validateFrame({{"passes", QVariantList{wrong}}}, error));
    if (!qEnvironmentVariableIsSet("ECHO_TEST_NATIVE_GPU"))
        return;
    QQuickWindow window;
    window.resize(120, 90);
    EchoGpuSurface surface;
    surface.setParentItem(window.contentItem());
    surface.setSize(window.size());
    surface.setGraphics(gpu);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    const auto picture = [&](int leftR, int leftG, int rightR, int rightG, bool rightEmpty = false,
                             bool leftEmpty = false) {
        auto *op = surface.readback();
        if (!f.success(op))
            return false;
        const auto result = op->result().toMap();
        const auto data = result.value("data").toByteArray();
        const int w = result.value("width").toInt(), h = result.value("height").toInt();
        if (w < 4 || h < 2 || data.size() != w * h * 4)
            return false;
        const int l = (h / 2 * w + w / 4) * 4, r = (h / 2 * w + 3 * w / 4) * 4;
        return quint8(data[l]) == leftR && quint8(data[l + 1]) == leftG &&
               quint8(data[l + 3]) == (leftEmpty ? 0 : 255) &&
               (rightEmpty ? quint8(data[r + 3]) == 0
                           : quint8(data[r]) == rightR && quint8(data[r + 1]) == rightG &&
                                 quint8(data[r + 3]) == 255);
    };
    QVERIFY(f.success(surface.enqueue({{"passes", QVariantList{pass}}})));
    QVERIFY(picture(255, 0, 0, 255));
    auto *signedRead = surface.readback(signedBytes);
    QVERIFY(f.success(signedRead));
    QCOMPARE(signedRead->result().toMap().value("data").toByteArray(),
             QByteArray::fromHex("807fff00"));

    auto *index =
        gpu->createBuffer({{"size", 8}, {"usage", QStringList{"index"}}, {"format", "uint16"}},
                          f.host.engine.evaluate("[0,1,2,0]"));
    QVERIFY(index);
    auto indexed = draw;
    indexed["indexBuffer"] = index->id();
    auto indexedPass = pass;
    indexedPass["draws"] = QVariantList{indexed};
    QVERIFY(f.success(surface.enqueue({{"passes", QVariantList{indexedPass}}})));
    QVERIFY(picture(255, 0, 0, 255));
    if (surface.status().value("indirectDraw").toBool()) {
        auto *args = gpu->createBuffer(
            {{"size", 16}, {"usage", QStringList{"indirect"}}, {"format", "uint32"}},
            f.host.engine.evaluate("[3,2,0,0]"));
        QVERIFY(args);
        auto indirect = draw;
        indirect.remove("count");
        indirect.remove("instances");
        indirect["indirect"] = QVariantMap{{"buffer", args->id()}};
        auto indirectPass = pass;
        indirectPass["draws"] = QVariantList{indirect};
        QVERIFY(f.success(surface.enqueue({{"passes", QVariantList{indirectPass}}})));
        QVERIFY(picture(255, 0, 0, 255));
        auto *indexedArgs = gpu->createBuffer(
            {{"size", 20}, {"usage", QStringList{"indirect"}}, {"format", "uint32"}},
            f.host.engine.evaluate("[3,2,0,0,0]"));
        QVERIFY(indexedArgs);
        indirect["indexBuffer"] = index->id();
        indirect["indirect"] = QVariantMap{{"buffer", indexedArgs->id()}};
        indirectPass["draws"] = QVariantList{indirect};
        QVERIFY(f.success(surface.enqueue({{"passes", QVariantList{indirectPass}}})));
        QVERIFY(picture(255, 0, 0, 255));
    }
    auto overflow = draw;
    overflow["firstInstance"] = 1;
    auto overflowPass = pass;
    overflowPass["draws"] = QVariantList{overflow};
    auto *failed = f.keep(surface.enqueue({{"passes", QVariantList{overflowPass}}}));
    QVERIFY(failed);
    QTRY_VERIFY(failed->finished());
    QCOMPARE(failed->state(), EchoPluginOperation::Failed);
    QVERIFY(failed->errorMessage().contains("stream range"));
    if (surface.status().value("integerAttributes").toBool()) {
        auto integerDefinition = definition;
        integerDefinition["vertexShader"] = "integer.vert.qsb";
        auto integerAttributes = attributes;
        auto tag = integerAttributes[2].toMap();
        tag["format"] = "uint";
        integerAttributes[2] = tag;
        integerDefinition["vertexLayout"] =
            QVariantMap{{"bindings", streams}, {"attributes", integerAttributes}};
        auto *integerPipeline = gpu->createRenderPipeline(integerDefinition);
        auto *tags =
            gpu->createBuffer({{"size", 8}, {"usage", QStringList{"vertex"}}, {"format", "uint32"}},
                              f.host.engine.evaluate("[7,9]"));
        QVERIFY(integerPipeline && tags);
        auto integerInputs = inputs;
        integerInputs[2] = QVariantMap{{"buffer", tags->id()}};
        auto integerDraw = draw;
        integerDraw["vertexBuffers"] = integerInputs;
        auto integerPass = pass;
        integerPass["pipeline"] = integerPipeline->id();
        integerPass["draws"] = QVariantList{integerDraw};
        QVERIFY(f.success(surface.enqueue({{"passes", QVariantList{integerPass}}})));
        QVERIFY(picture(255, 0, 0, 255));
        integerDefinition["vertexShader"] = "instanced.vert.qsb";
        auto *mismatch = gpu->createRenderPipeline(integerDefinition);
        QVERIFY(mismatch);
        integerPass["pipeline"] = mismatch->id();
        auto *rejected = f.keep(surface.enqueue({{"passes", QVariantList{integerPass}}}));
        QVERIFY(rejected);
        QTRY_VERIFY(rejected->finished());
        QCOMPARE(rejected->state(), EchoPluginOperation::Failed);
        QVERIFY(rejected->errorMessage().contains("reflection"));
    }
    auto matrixDefinition = definition;
    matrixDefinition["vertexShader"] = "matrix.vert.qsb";
    auto matrixStreams = streams;
    matrixStreams[1] = QVariantMap{{"stride", 24}, {"stepMode", "instance"}};
    const QVariantList matrixAttributes{
        attributes[0],
        QVariantMap{{"binding", 1},
                    {"location", 1},
                    {"format", "float2"},
                    {"offset", 0},
                    {"matrixSlice", 0}},
        QVariantMap{{"binding", 1},
                    {"location", 2},
                    {"format", "float2"},
                    {"offset", 8},
                    {"matrixSlice", 1}},
        QVariantMap{{"binding", 1}, {"location", 3}, {"format", "float2"}, {"offset", 16}},
        QVariantMap{{"binding", 2}, {"location", 4}, {"format", "unorm8x4"}, {"offset", 0}}};
    matrixDefinition["vertexLayout"] =
        QVariantMap{{"bindings", matrixStreams}, {"attributes", matrixAttributes}};
    auto *matrixPipeline = gpu->createRenderPipeline(matrixDefinition);
    auto *matrices = gpu->createBuffer({{"size", 48}, {"usage", QStringList{"vertex"}}},
                                       f.host.engine.evaluate("[1,0,0,1,-.5,0,1,0,0,1,.5,0]"));
    QVERIFY(matrixPipeline && matrices);
    auto matrixInputs = inputs;
    matrixInputs[1] = QVariantMap{{"buffer", matrices->id()}};
    auto matrixDraw = draw;
    matrixDraw["vertexBuffers"] = matrixInputs;
    auto matrixPass = pass;
    matrixPass["pipeline"] = matrixPipeline->id();
    matrixPass["draws"] = QVariantList{matrixDraw};
    QVERIFY(f.success(surface.enqueue({{"passes", QVariantList{matrixPass}}})));
    QVERIFY(picture(255, 0, 0, 255));
    if (surface.status().value("customInstanceStepRate").toBool()) {
        auto stepDefinition = definition;
        auto stepStreams = streams;
        stepStreams[1] = QVariantMap{{"stride", 8}, {"stepMode", "instance"}, {"stepRate", 2}};
        stepDefinition["vertexLayout"] =
            QVariantMap{{"bindings", stepStreams}, {"attributes", attributes}};
        auto *stepPipeline = gpu->createRenderPipeline(stepDefinition);
        QVERIFY(stepPipeline);
        auto stepPass = pass;
        stepPass["pipeline"] = stepPipeline->id();
        QVERIFY(f.success(surface.enqueue({{"passes", QVariantList{stepPass}}})));
        QVERIFY(picture(0, 255, 0, 0, true));
        if (surface.status().value("baseInstance").toBool()) {
            auto based = draw;
            based["firstInstance"] = 1;
            based["instances"] = 1;
            stepPass["draws"] = QVariantList{based};
            QVERIFY(f.success(surface.enqueue({{"passes", QVariantList{stepPass}}})));
            QVERIFY(picture(0, 0, 0, 255, false, true));
            // One remaining record cannot satisfy base one, even with divisor two.
            auto remaining = inputs;
            remaining[1] = QVariantMap{{"buffer", offsets->id()}, {"offset", 8}};
            based["vertexBuffers"] = remaining;
            stepPass["draws"] = QVariantList{based};
            auto *range = f.keep(surface.enqueue({{"passes", QVariantList{stepPass}}}));
            QVERIFY(range);
            QTRY_VERIFY(range->finished());
            QCOMPARE(range->state(), EchoPluginOperation::Failed);
            QVERIFY(range->errorMessage().contains("stream range"));
        }
    }
    if (surface.status().value("halfAttributes").toBool()) {
        auto halfDefinition = definition;
        auto halfStreams = streams;
        halfStreams[0] = QVariantMap{{"stride", 4}};
        auto halfAttributes = attributes;
        auto position = halfAttributes[0].toMap();
        position["format"] = "half2";
        halfAttributes[0] = position;
        halfDefinition["vertexLayout"] =
            QVariantMap{{"bindings", halfStreams}, {"attributes", halfAttributes}};
        auto *halfPipeline = gpu->createRenderPipeline(halfDefinition);
        auto *halfPositions = gpu->createBuffer(
            {{"size", 12}, {"usage", QStringList{"vertex"}}, {"format", "float16"}},
            f.host.engine.evaluate("[-.4,-.4,.4,-.4,0,.4]"));
        QVERIFY(halfPipeline && halfPositions);
        auto halfInputs = inputs;
        halfInputs[0] = QVariantMap{{"buffer", halfPositions->id()}};
        auto halfDraw = draw;
        halfDraw["vertexBuffers"] = halfInputs;
        auto halfPass = pass;
        halfPass["pipeline"] = halfPipeline->id();
        halfPass["draws"] = QVariantList{halfDraw};
        QVERIFY(f.success(surface.enqueue({{"passes", QVariantList{halfPass}}})));
        QVERIFY(picture(255, 0, 0, 255));
    }
    auto *cancel = f.keep(surface.enqueue({{"passes", QVariantList{pass}}}));
    surface.setVisible(false);
    QTRY_VERIFY(cancel->finished());
    QCOMPARE(cancel->state(), EchoPluginOperation::Canceled);
    QTRY_VERIFY(surface.drained());
    surface.setVisible(true);
    QTRY_VERIFY(surface.live());
    QVERIFY(f.success(surface.enqueue({{"passes", QVariantList{pass}}})));
    QVERIFY(picture(255, 0, 0, 255));
    QVERIFY(f.host.runtime.setEnabled(f.host.installation(), false));
    QTRY_COMPARE(f.host.runtime.state(f.host.installation()), PluginRuntime::Disabled);
    QTRY_VERIFY(surface.drained());
    QCOMPARE(memory->native.used.load(), 0);
    QCOMPARE(memory->targets.used.load(), 0);
    QCOMPARE(memory->readbacks.used.load(), 0);
    QCOMPARE(memory->submissions.used.load(), 0);
}
