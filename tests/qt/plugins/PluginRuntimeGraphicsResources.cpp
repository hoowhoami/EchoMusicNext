#include "PluginRuntimeGraphicsFixture.h"
void PluginRuntimeTest::graphicsIndirectGpuArgumentsAndRetirement()
{
    GraphicsFixture f;
    QVERIFY(f.gpu);
    auto *gpu = f.gpu;
    const auto memory = gpu->memoryBudget();
    auto *args = gpu->createBuffer(
        {{"size", 160}, {"usage", QStringList{"storage", "indirect"}}, {"format", "uint32"}});
    auto *wrongUsage = gpu->createBuffer({{"size", 160}, {"usage", QStringList{"storage"}}});
    auto *vertices = f.vertices();
    auto *pipeline = f.pipeline();
    auto *compute = gpu->createComputePipeline({{"computeShader", "indirect.comp.qsb"}});
    QVERIFY(args && wrongUsage && vertices && pipeline && compute);
    QVariantMap indirect{{"buffer", args->id()}, {"offset", 16}, {"drawCount", 2}, {"stride", 32}};
    QVariantMap draw{{"vertexBuffer", vertices->id()}, {"indirect", indirect}};
    QVariantMap pass{{"type", "render"},
                     {"pipeline", pipeline->id()},
                     {"bindings", QVariantList{bind(0, "uniform", "frame")}},
                     {"draws", QVariantList{draw}}};
    QString error;
    const auto validate = [&](QVariantMap d) {
        auto p = pass;
        p["draws"] = QVariantList{d};
        return gpu->validateFrame({{"passes", QVariantList{p}}}, error);
    };
    QVERIFY(validate(draw));
    for (const auto &field : {"count", "first", "instances", "baseVertex", "firstInstance"}) {
        auto bad = draw;
        bad[field] = 1;
        QVERIFY(!validate(bad));
    }
    for (const auto &change :
         {QVariantMap{{"offset", 2}}, QVariantMap{{"drawCount", 0}},
          QVariantMap{{"drawCount", 257}}, QVariantMap{{"stride", 12}}, QVariantMap{{"stride", 18}},
          QVariantMap{{"offset", 156}}, QVariantMap{{"buffer", wrongUsage->id()}},
          QVariantMap{{"oops", 1}}}) {
        auto a = indirect;
        for (auto it = change.begin(); it != change.end(); ++it)
            a[it.key()] = it.value();
        auto bad = draw;
        bad["indirect"] = a;
        QVERIFY(!validate(bad));
    }
    auto exhausted = pass;
    QVariantList many;
    for (int i = 0; i < 129; ++i)
        many.append(draw);
    exhausted["draws"] = many;
    QVERIFY(!gpu->validateFrame({{"passes", QVariantList{exhausted}}}, error));
    auto owner = std::make_unique<QObject>();
    auto *child = f.scope->createPresentation(owner.get(), "indirect-other");
    QVERIFY(child && !child->graphics()->validateFrame({{"passes", QVariantList{pass}}}, error));
    owner.reset();
    if (!qEnvironmentVariableIsSet("ECHO_TEST_NATIVE_GPU"))
        return;
    QQuickWindow window;
    window.resize(100, 80);
    EchoGpuSurface surface;
    surface.setParentItem(window.contentItem());
    surface.setSize(window.size());
    surface.setGraphics(gpu);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QTRY_VERIFY(surface.status().value("available").toBool());
    if (!surface.status().value("indirectDraw").toBool() ||
        !surface.status().value("compute").toBool())
        QSKIP("Native indirect/compute unsupported by this device");
    const QVariantMap dispatch{
        {"type", "compute"},
        {"pipeline", compute->id()},
        {"groups", QVariantList{1, 1, 1}},
        {"bindings", QVariantList{bind(3, "storage-buffer", args->id(), "write")}}};
    QVERIFY(f.success(surface.enqueue({{"passes", QVariantList{dispatch, pass}}})));
    auto *argumentRead = surface.readback(args);
    QVERIFY(f.success(argumentRead));
    QCOMPARE(argumentRead->result().toMap().value("data").toByteArray().mid(16, 8),
             QByteArray::fromHex("0400000001000000"));
    QVERIFY(f.pixels(surface, nullptr, {}, 137, 188, 225));
    auto *copied = gpu->createBuffer(
        {{"size", 160}, {"usage", QStringList{"storage", "indirect"}}, {"format", "uint32"}});
    QVERIFY(copied);
    const QVariantMap copy{{"type", "copy-buffer"},
                           {"source", args->id()},
                           {"destination", copied->id()},
                           {"size", 160}};
    auto copiedDraw = draw;
    auto copiedArgs = indirect;
    copiedArgs["buffer"] = copied->id();
    copiedDraw["indirect"] = copiedArgs;
    auto copiedPass = pass;
    copiedPass["draws"] = QVariantList{copiedDraw};
    QVERIFY(f.success(surface.enqueue({{"passes", QVariantList{copy, copiedPass}}})));
    QVERIFY(f.pixels(surface, nullptr, {}, 137, 188, 225));
    auto *indices =
        gpu->createBuffer({{"size", 12}, {"usage", QStringList{"index"}}, {"format", "uint16"}},
                          f.host.engine.evaluate("[0,1,2,2,1,3]"));
    auto *indexedPipeline = gpu->createRenderPipeline(
        {{"vertexShader", "fixture.vert.qsb"},
         {"fragmentShader", "solid.frag.qsb"},
         {"blend", "none"},
         {"vertexLayout",
          QVariantMap{
              {"stride", 8},
              {"attributes",
               QVariantList{QVariantMap{{"location", 0}, {"offset", 0}, {"format", "float2"}}}}}}});
    QVERIFY(indices && indexedPipeline);
    auto indexed = copiedDraw;
    copiedArgs["offset"] = 80;
    indexed["indirect"] = copiedArgs;
    indexed["indexBuffer"] = indices->id();
    auto indexedPass = copiedPass;
    indexedPass["pipeline"] = indexedPipeline->id();
    indexedPass["draws"] = QVariantList{indexed};
    QVERIFY(f.success(surface.enqueue({{"passes", QVariantList{indexedPass}}})));
    QVERIFY(f.pixels(surface, nullptr, {}, 137, 188, 225));
    // A zero instance count is consumed by the GPU, not mistaken for a missing argument.
    copiedArgs["offset"] = 112;
    copiedArgs["drawCount"] = 1;
    indexed["indirect"] = copiedArgs;
    indexedPass["draws"] = QVariantList{indexed};
    QVERIFY(f.success(surface.enqueue({{"passes", QVariantList{indexedPass}}})));
    auto *empty = surface.readback();
    QVERIFY(f.success(empty));
    const auto zeros = empty->result().toMap().value("data").toByteArray();
    QCOMPARE(zeros, QByteArray(zeros.size(), 0));
    auto direct = pass;
    direct["draws"] = QVariantList{QVariantMap{{"vertexBuffer", vertices->id()}, {"count", 4}}};
    QVERIFY(f.success(surface.enqueue({{"passes", QVariantList{direct}}})));
    QVERIFY(f.success(surface.enqueue({{"passes", QVariantList{pass}}})));
    QVERIFY(f.pixels(surface, nullptr, {}, 137, 188, 225));
    auto *cancel = f.keep(surface.enqueue({{"passes", QVariantList{dispatch, pass}}}));
    surface.setVisible(false);
    QTRY_VERIFY(cancel->finished());
    QCOMPARE(cancel->state(), EchoPluginOperation::Canceled);
    QTRY_VERIFY(surface.drained());
    surface.setVisible(true);
    QTRY_VERIFY(surface.live());
    QVERIFY(f.success(surface.enqueue({{"passes", QVariantList{dispatch, pass}}})));
    QVERIFY(f.pixels(surface, nullptr, {}, 137, 188, 225));
    auto *accountCanceled = f.keep(surface.enqueue({{"passes", QVariantList{pass}}}));
    QPointer<EchoGpuResource> previousArgs = args;
    QVERIFY(QMetaObject::invokeMethod(&f.host.api, "sessionChanged", Q_ARG(quint64, 1)));
    QTRY_VERIFY(accountCanceled->finished());
    QCOMPARE(accountCanceled->state(), EchoPluginOperation::Canceled);
    QVERIFY(!previousArgs || !previousArgs->active());
    QTRY_VERIFY(surface.drained());
    QVERIFY(f.host.runtime.setEnabled(f.host.installation(), false));
    QTRY_COMPARE(f.host.runtime.state(f.host.installation()), PluginRuntime::Disabled);
    QTRY_VERIFY(surface.drained());
    QCOMPARE(memory->native.used.load(), 0);
    QCOMPARE(memory->targets.used.load(), 0);
    QCOMPARE(memory->readbacks.used.load(), 0);
    QCOMPARE(memory->submissions.used.load(), 0);
}
void PluginRuntimeTest::graphicsTextureArraysTransfersAndLifetime()
{
    GraphicsFixture f;
    QVERIFY(f.gpu);
    auto *gpu = f.gpu;
    const auto memory = gpu->memoryBudget();
    QVariantMap definition{
        {"dimension", "2d-array"},
        {"layers", 3},
        {"width", 4},
        {"height", 4},
        {"mipmaps", true},
        {"usage", QStringList{"sampled", "render-target", "readback", "storage"}}};
    auto *array = gpu->createTexture(
        definition, f.host.engine.evaluate("new Uint8Array(Array.from({length:192},(_,i)=> i%4===3 "
                                           "|| i%4===Math.floor(i/64) ? 255 : 0)).buffer"));
    QCOMPARE(gpu->capabilities().value("logicalBytes").toLongLong(), 252);
    auto *flat = gpu->createTexture(
        {{"width", 2}, {"height", 2}, {"usage", QStringList{"sampled", "readback"}}});
    auto *vertices = f.vertices();
    auto *solid = f.pipeline("solid.frag", true);
    auto *sample = f.pipeline("array.frag");
    auto *sampler = gpu->createSampler({{"minFilter", "nearest"}, {"magFilter", "nearest"}});
    auto *compute = gpu->createComputePipeline({{"computeShader", "array.comp.qsb"}});
    QVERIFY(array && flat && vertices && solid && sample && sampler && compute);
    auto invalid = definition;
    invalid.remove("layers");
    QVERIFY(!gpu->createTexture(invalid));
    invalid["layers"] = 257;
    QVERIFY(!gpu->createTexture(invalid));
    invalid = definition;
    invalid["dimension"] = "2d";
    QVERIFY(!gpu->createTexture(invalid));
    invalid = definition;
    invalid["layers"] = true;
    QVERIFY(!gpu->createTexture(invalid));
    invalid = definition;
    invalid["width"] = 4096;
    invalid["height"] = 4096;
    QVERIFY(!gpu->createTexture(invalid));
    QVariantMap drawPass{
        {"type", "render"},
        {"pipeline", solid->id()},
        {"colorTargets",
         QVariantList{QVariantMap{{"texture", array->id()}, {"layer", 2}, {"level", 1}}}},
        {"bindings", QVariantList{bind(0, "uniform", "frame")}},
        {"draws", QVariantList{QVariantMap{{"vertexBuffer", vertices->id()}, {"count", 4}}}}};
    QString error;
    QVERIFY(gpu->validateFrame({{"passes", QVariantList{drawPass}}}, error));
    invalid = drawPass;
    invalid["colorTargets"] = QVariantList{QVariantMap{{"texture", array->id()}, {"layer", 3}}};
    QVERIFY(!gpu->validateFrame({{"passes", QVariantList{invalid}}}, error));
    QVariantMap upload{{"type", "upload-texture"},
                       {"destination", array->id()},
                       {"layer", 1},
                       {"level", 1},
                       {"size", QVariantList{2, 2}},
                       {"data", QVariantList{255, 255, 0, 255, 255, 255, 0, 255, 255, 255, 0, 255,
                                             255, 255, 0, 255}}};
    QVariantMap normalized;
    QVERIFY(gpu->prepareSubmission({{"passes", QVariantList{upload}}}, normalized, error));
    invalid = upload;
    invalid["layer"] = 3;
    QVERIFY(!gpu->prepareSubmission({{"passes", QVariantList{invalid}}}, normalized, error));
    invalid["layer"] = -1;
    QVERIFY(!gpu->prepareSubmission({{"passes", QVariantList{invalid}}}, normalized, error));
    if (!qEnvironmentVariableIsSet("ECHO_TEST_NATIVE_GPU"))
        return;
    QQuickWindow window;
    window.resize(100, 80);
    EchoGpuSurface surface;
    surface.setParentItem(window.contentItem());
    surface.setSize(window.size());
    surface.setGraphics(gpu);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QTRY_VERIFY(surface.status().value("available").toBool());
    if (!surface.status().value("textureArrays").toBool() ||
        !surface.status().value("renderMipmaps").toBool())
        QSKIP("Texture arrays or mip rendering unsupported by this device");
    QVERIFY(f.pixels(surface, array, {{"layer", 0}}, 255, 0, 0));
    QVERIFY(f.pixels(surface, array, {{"layer", 1}}, 0, 255, 0));
    QVERIFY(f.pixels(surface, array, {{"layer", 2}}, 0, 0, 255));
    QVERIFY(f.success(surface.enqueue(
        {{"passes", QVariantList{QVariantMap{{"type", "generate-mips"}, {"texture", array->id()}},
                                 upload, drawPass}}})));
    QVERIFY(f.pixels(surface, array, {{"layer", 1}, {"level", 1}}, 255, 255, 0));
    QVERIFY(f.pixels(surface, array, {{"layer", 2}, {"level", 1}}, 64, 128, 191));
    QVERIFY(f.pixels(surface, array, {{"layer", 0}, {"level", 1}}, 255, 0, 0));
    const auto depthBefore = memory->native.used.load();
    auto otherLayer = drawPass;
    otherLayer["colorTargets"] =
        QVariantList{QVariantMap{{"texture", array->id()}, {"layer", 1}, {"level", 1}}};
    QVERIFY(f.success(surface.enqueue({{"passes", QVariantList{otherLayer}}})));
    QTRY_COMPARE(memory->native.used.load(), depthBefore + 16);
    // Restore yellow after rendering into layer one, before testing the copy.
    QVERIFY(f.success(surface.enqueue({{"passes", QVariantList{upload}}})));

    const QVariantMap copy{{"type", "copy-texture"},    {"source", array->id()},
                           {"sourceLayer", 1},          {"sourceLevel", 1},
                           {"destination", flat->id()}, {"size", QVariantList{2, 2}}};
    QVERIFY(f.success(surface.enqueue({{"passes", QVariantList{copy}}})));
    QVERIFY(f.pixels(surface, flat, {}, 255, 255, 0));
    const QVariantMap copyBack{{"type", "copy-texture"},     {"source", flat->id()},
                               {"destination", array->id()}, {"destinationLayer", 0},
                               {"destinationLevel", 1},      {"size", QVariantList{2, 2}}};
    QVERIFY(f.success(surface.enqueue({{"passes", QVariantList{copyBack}}})));
    QVERIFY(f.pixels(surface, array, {{"layer", 0}, {"level", 1}}, 255, 255, 0));
    QVERIFY(f.pixels(surface, array, {{"layer", 2}, {"level", 1}}, 64, 128, 191));

    auto sampled = drawPass;
    sampled.remove("colorTargets");
    sampled["pipeline"] = sample->id();
    auto texture = bind(2, "texture", array->id());
    texture["sampler"] = sampler->id();
    sampled["bindings"] = QVariantList{bind(0, "uniform", "frame"), texture};
    // Whole-resource writes preserve the complete array shape, not just layer zero.
    QVERIFY(gpu->writeTexture(
        array, f.host.engine.evaluate("new Uint8Array(Array.from({length:192},(_,i)=>i%4===3 || "
                                      "i%4===0 ? 255 : 0)).buffer")));
    QVERIFY(f.success(surface.enqueue({{"passes", QVariantList{sampled}}})));
    QVERIFY(f.pixels(surface, nullptr, {}, 255, 0, 0));
    texture["resource"] = flat->id();
    auto mismatch = sampled;
    mismatch["bindings"] = QVariantList{bind(0, "uniform", "frame"), texture};
    auto *failed = f.keep(surface.enqueue({{"passes", QVariantList{mismatch}}}));
    QVERIFY(failed);
    QTRY_VERIFY(failed->finished());
    QCOMPARE(failed->state(), EchoPluginOperation::Failed);
    QVERIFY(failed->errorMessage().contains("dimension"));
    if (surface.status().value("compute").toBool()) {
        auto image = bind(4, "storage-image", array->id(), "write");
        image["level"] = 1;
        const QVariantMap dispatch{{"type", "compute"},
                                   {"pipeline", compute->id()},
                                   {"groups", QVariantList{1, 1, 1}},
                                   {"bindings", QVariantList{image}}};
        QVERIFY(f.success(surface.enqueue({{"passes", QVariantList{dispatch}}})));
        QVERIFY(f.pixels(surface, array, {{"level", 1}, {"layer", 1}}, 255, 0, 255));
    }
    auto *badRead = f.keep(surface.readback(array, {{"layer", 3}}));
    QVERIFY(badRead);
    QTRY_VERIFY(badRead->finished());
    QCOMPARE(badRead->state(), EchoPluginOperation::Failed);
    auto *canceled = f.keep(surface.enqueue({{"passes", QVariantList{sampled}}}));
    surface.setVisible(false);
    QTRY_VERIFY(canceled->finished());
    QCOMPARE(canceled->state(), EchoPluginOperation::Canceled);
    QTRY_VERIFY(surface.drained());
    surface.setVisible(true);
    QTRY_VERIFY(surface.live());
    QVERIFY(f.success(surface.enqueue({{"passes", QVariantList{sampled}}})));
    QVERIFY(f.pixels(surface, nullptr, {}, 255, 0, 0));
    auto *accountCanceled = f.keep(surface.enqueue({{"passes", QVariantList{sampled}}}));
    QPointer<EchoGpuResource> previousArray = array;
    QVERIFY(QMetaObject::invokeMethod(&f.host.api, "sessionChanged", Q_ARG(quint64, 1)));
    QTRY_VERIFY(accountCanceled->finished());
    QCOMPARE(accountCanceled->state(), EchoPluginOperation::Canceled);
    QVERIFY(!previousArray || !previousArray->active());
    QTRY_VERIFY(surface.drained());
    QVERIFY(f.host.runtime.setEnabled(f.host.installation(), false));
    QTRY_COMPARE(f.host.runtime.state(f.host.installation()), PluginRuntime::Disabled);
    QTRY_VERIFY(surface.drained());
    QCOMPARE(memory->native.used.load(), 0);
    QCOMPARE(memory->targets.used.load(), 0);
    QCOMPARE(memory->readbacks.used.load(), 0);
    QCOMPARE(memory->submissions.used.load(), 0);
}
void PluginRuntimeTest::graphicsCubeVolumeSlicesAndRecovery()
{
    GraphicsFixture f;
    QVERIFY(f.gpu);
    auto *gpu = f.gpu;
    const auto memory = gpu->memoryBudget();
    QVariantMap cubeDefinition{{"dimension", "cube"},
                               {"width", 4},
                               {"height", 4},
                               {"mipmaps", true},
                               {"usage", QStringList{"sampled", "render-target", "readback"}}};
    auto *cube = gpu->createTexture(
        cubeDefinition,
        f.host.engine.evaluate("new Uint8Array(Array.from({length:384},(_,i)=>i%4===3 || "
                               "i%4===Math.floor(i/64)%3 ? 255 : 0)).buffer"));
    QVERIFY(cube);
    QCOMPARE(gpu->capabilities().value("logicalBytes").toLongLong(), 504);
    QVariantMap volumeDefinition = cubeDefinition;
    volumeDefinition["dimension"] = "3d";
    volumeDefinition["depth"] = 4;
    volumeDefinition["usage"] = QStringList{"sampled", "render-target", "storage", "readback"};
    auto *volume = gpu->createTexture(
        volumeDefinition,
        f.host.engine.evaluate("new Uint8Array(Array.from({length:256},(_,i)=>i%4===3 || "
                               "i%4===Math.floor(i/64)%3 ? 255 : 0)).buffer"));
    QVERIFY(volume);
    QCOMPARE(gpu->capabilities().value("logicalBytes").toLongLong(), 504 + 292);
    auto invalid = cubeDefinition;
    invalid["height"] = 2;
    QVERIFY(!gpu->createTexture(invalid));
    invalid = cubeDefinition;
    invalid["layers"] = 6;
    QVERIFY(!gpu->createTexture(invalid));
    invalid = cubeDefinition;
    invalid["usage"] = QStringList{"storage"};
    QVERIFY(!gpu->createTexture(invalid));
    invalid = volumeDefinition;
    invalid.remove("depth");
    QVERIFY(!gpu->createTexture(invalid));
    invalid["depth"] = 2049;
    QVERIFY(!gpu->createTexture(invalid));
    // QRhi's mip chain is based on width/height even for a tall volume.
    invalid = volumeDefinition;
    invalid["width"] = 2;
    invalid["height"] = 2;
    invalid["depth"] = 4;
    auto *tall = gpu->createTexture(invalid);
    QVERIFY(tall);
    QCOMPARE(gpu->capabilities().value("logicalBytes").toLongLong(), 504 + 292 + 72);
    tall->dispose();
    auto *vertices = f.vertices();
    auto *solid = f.pipeline("solid.frag", true);
    auto *cubeSample = f.pipeline("cube.frag");
    auto *volumeSample = f.pipeline("volume.frag");
    auto *sampler =
        gpu->createSampler({{"minFilter", "nearest"}, {"magFilter", "nearest"}, {"w", "clamp"}});
    auto *repeatSampler =
        gpu->createSampler({{"minFilter", "nearest"}, {"magFilter", "nearest"}, {"w", "repeat"}});
    auto *compute = gpu->createComputePipeline({{"computeShader", "volume.comp.qsb"}});
    QVERIFY(vertices && solid && cubeSample && volumeSample && sampler && repeatSampler && compute);
    QVERIFY(!gpu->createSampler({{"w", "invalid"}}));
    QVariantMap pass{
        {"type", "render"},
        {"pipeline", solid->id()},
        {"colorTargets",
         QVariantList{QVariantMap{{"texture", volume->id()}, {"level", 1}, {"layer", 1}}}},
        {"bindings", QVariantList{bind(0, "uniform", "frame")}},
        {"draws", QVariantList{QVariantMap{{"vertexBuffer", vertices->id()}, {"count", 4}}}}};
    QString error;
    QVERIFY(gpu->validateFrame({{"passes", QVariantList{pass}}}, error));
    invalid = pass;
    invalid["colorTargets"] =
        QVariantList{QVariantMap{{"texture", volume->id()}, {"level", 1}, {"layer", 2}}};
    QVERIFY(!gpu->validateFrame({{"passes", QVariantList{invalid}}}, error));
    invalid["colorTargets"] =
        QVariantList{QVariantMap{{"texture", cube->id()}, {"level", 1}, {"layer", 6}}};
    QVERIFY(!gpu->validateFrame({{"passes", QVariantList{invalid}}}, error));
    QVariantMap upload{{"type", "upload-texture"},
                       {"destination", volume->id()},
                       {"level", 1},
                       {"layer", 1},
                       {"size", QVariantList{2, 2}},
                       {"data", QVariantList{255, 255, 0, 255, 255, 255, 0, 255, 255, 255, 0, 255,
                                             255, 255, 0, 255}}};
    QVariantMap normalized;
    QVERIFY(gpu->prepareSubmission({{"passes", QVariantList{upload}}}, normalized, error));
    invalid = upload;
    invalid["layer"] = 2;
    QVERIFY(!gpu->prepareSubmission({{"passes", QVariantList{invalid}}}, normalized, error));
    if (!qEnvironmentVariableIsSet("ECHO_TEST_NATIVE_GPU"))
        return;
    QQuickWindow window;
    window.resize(100, 80);
    EchoGpuSurface surface;
    surface.setParentItem(window.contentItem());
    surface.setSize(window.size());
    surface.setGraphics(gpu);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QTRY_VERIFY(surface.status().value("available").toBool());
    if (!surface.status().value("volumeTextures").toBool() ||
        !surface.status().value("cubeTextures").toBool() ||
        !surface.status().value("volumeMipmaps").toBool() ||
        !surface.status().value("renderVolumeSlices").toBool())
        QSKIP("Device lacks cube/volume textures, mipmaps or slice targets");
    for (int layer = 0; layer < 6; ++layer)
        QVERIFY(f.pixels(surface, cube, {{"layer", layer}}, layer % 3 == 0 ? 255 : 0,
                         layer % 3 == 1 ? 255 : 0, layer % 3 == 2 ? 255 : 0));
    for (int slice = 0; slice < 4; ++slice)
        QVERIFY(f.pixels(surface, volume, {{"layer", slice}}, slice % 3 == 0 ? 255 : 0,
                         slice % 3 == 1 ? 255 : 0, slice % 3 == 2 ? 255 : 0));
    QVERIFY(f.success(surface.enqueue(
        {{"passes", QVariantList{QVariantMap{{"type", "generate-mips"}, {"texture", volume->id()}},
                                 upload, pass}}})));
    QVERIFY(f.pixels(surface, volume, {{"layer", 1}, {"level", 1}}, 64, 128, 191));
    auto cubeTarget = pass;
    cubeTarget["colorTargets"] =
        QVariantList{QVariantMap{{"texture", cube->id()}, {"level", 1}, {"layer", 5}}};
    QVERIFY(f.success(surface.enqueue(
        {{"passes", QVariantList{QVariantMap{{"type", "generate-mips"}, {"texture", cube->id()}},
                                 cubeTarget}}})));
    QVERIFY(f.pixels(surface, cube, {{"layer", 5}, {"level", 1}}, 64, 128, 191));
    const QVariantMap copy{{"type", "copy-texture"},    {"source", volume->id()},
                           {"sourceLevel", 1},          {"sourceLayer", 1},
                           {"destination", cube->id()}, {"destinationLayer", 4},
                           {"destinationLevel", 1},     {"size", QVariantList{2, 2}}};
    QVERIFY(f.success(surface.enqueue({{"passes", QVariantList{copy}}})));
    QVERIFY(f.pixels(surface, cube, {{"layer", 4}, {"level", 1}}, 64, 128, 191));
    auto copyBack = copy;
    copyBack["source"] = cube->id();
    copyBack["sourceLayer"] = 4;
    copyBack["destination"] = volume->id();
    copyBack["destinationLayer"] = 0;
    QVERIFY(f.success(surface.enqueue({{"passes", QVariantList{copyBack}}})));
    QVERIFY(f.pixels(surface, volume, {{"layer", 0}, {"level", 1}}, 64, 128, 191));
    const auto samplePass = [&](EchoGpuResource *pipeline, EchoGpuResource *image) {
        auto p = pass;
        p.remove("colorTargets");
        p["pipeline"] = pipeline->id();
        auto texture = bind(2, "texture", image->id());
        texture["sampler"] = sampler->id();
        p["bindings"] = QVariantList{bind(0, "uniform", "frame"), texture};
        return p;
    };
    QVERIFY(f.success(surface.enqueue({{"passes", QVariantList{samplePass(cubeSample, cube)}}})));
    QVERIFY(f.pixels(surface, nullptr, {}, 255, 0, 0));
    QVERIFY(
        f.success(surface.enqueue({{"passes", QVariantList{samplePass(volumeSample, volume)}}})));
    // z=1.375 clamps to red slice three, or repeats to green slice one.
    QVERIFY(f.pixels(surface, nullptr, {}, 255, 0, 0));
    auto repeated = samplePass(volumeSample, volume);
    auto repeatedBindings = repeated.value("bindings").toList();
    auto repeatedTexture = repeatedBindings[1].toMap();
    repeatedTexture["sampler"] = repeatSampler->id();
    repeatedBindings[1] = repeatedTexture;
    repeated["bindings"] = repeatedBindings;
    QVERIFY(f.success(surface.enqueue({{"passes", QVariantList{repeated}}})));
    QVERIFY(f.pixels(surface, nullptr, {}, 0, 255, 0));
    auto *mismatch =
        f.keep(surface.enqueue({{"passes", QVariantList{samplePass(cubeSample, volume)}}}));
    QVERIFY(mismatch);
    QTRY_VERIFY(mismatch->finished());
    QCOMPARE(mismatch->state(), EchoPluginOperation::Failed);
    QVERIFY(gpu->writeTexture(
        cube, f.host.engine.evaluate("new Uint8Array(Array.from({length:384},(_,i)=>i%4===3 || "
                                     "i%4===0 ? 255 : 0)).buffer")));
    QVERIFY(gpu->writeTexture(
        volume, f.host.engine.evaluate("new Uint8Array(Array.from({length:256},(_,i)=>i%4===3 || "
                                       "i%4===2 ? 255 : 0)).buffer")));
    QVERIFY(
        f.success(surface.enqueue({{"passes", QVariantList{samplePass(volumeSample, volume)}}})));
    QVERIFY(f.pixels(surface, nullptr, {}, 0, 0, 255));
    if (surface.status().value("compute").toBool()) {
        auto image = bind(4, "storage-image", volume->id(), "write");
        image["level"] = 1;
        const QVariantMap dispatch{{"type", "compute"},
                                   {"pipeline", compute->id()},
                                   {"groups", QVariantList{1, 1, 1}},
                                   {"bindings", QVariantList{image}}};
        QVERIFY(f.success(surface.enqueue({{"passes", QVariantList{dispatch}}})));
        QVERIFY(f.pixels(surface, volume, {{"level", 1}, {"layer", 1}}, 255, 0, 255));
    }
    auto *hugeLevel = f.keep(surface.readback(volume, {{"level", 16 * 1024 * 1024}}));
    QVERIFY(hugeLevel);
    QTRY_VERIFY(hugeLevel->finished());
    QCOMPARE(hugeLevel->state(), EchoPluginOperation::Failed);
    auto *badRead = f.keep(surface.readback(volume, {{"level", 1}, {"layer", 2}}));
    QVERIFY(badRead);
    QTRY_VERIFY(badRead->finished());
    QCOMPARE(badRead->state(), EchoPluginOperation::Failed);
    auto *cancel =
        f.keep(surface.enqueue({{"passes", QVariantList{samplePass(volumeSample, volume)}}}));
    surface.setVisible(false);
    QTRY_VERIFY(cancel->finished());
    QCOMPARE(cancel->state(), EchoPluginOperation::Canceled);
    QTRY_VERIFY(surface.drained());
    surface.setVisible(true);
    QTRY_VERIFY(surface.live());
    QVERIFY(f.success(surface.enqueue({{"passes", QVariantList{samplePass(cubeSample, cube)}}})));
    QVERIFY(f.pixels(surface, nullptr, {}, 255, 0, 0));
    auto *account =
        f.keep(surface.enqueue({{"passes", QVariantList{samplePass(volumeSample, volume)}}}));
    QVERIFY(QMetaObject::invokeMethod(&f.host.api, "sessionChanged", Q_ARG(quint64, 1)));
    QTRY_VERIFY(account->finished());
    QCOMPARE(account->state(), EchoPluginOperation::Canceled);
    // Recreate native resources in the next account and disable while they are live.
    // This checks disable retirement independently of the preceding account cleanup.
    auto *nextCube = gpu->createTexture(cubeDefinition);
    auto *nextVolume = gpu->createTexture(volumeDefinition);
    auto *nextVertices = f.vertices();
    auto *nextPipeline = f.pipeline("solid.frag", true);
    QVERIFY(nextCube && nextVolume && nextVertices && nextPipeline);
    EchoGpuSurface nextSurface;
    nextSurface.setParentItem(window.contentItem());
    nextSurface.setSize(window.size());
    nextSurface.setGraphics(gpu);
    QTRY_VERIFY(nextSurface.live());
    auto nextPass = pass;
    nextPass["pipeline"] = nextPipeline->id();
    nextPass["draws"] =
        QVariantList{QVariantMap{{"vertexBuffer", nextVertices->id()}, {"count", 4}}};
    nextPass["colorTargets"] =
        QVariantList{QVariantMap{{"texture", nextCube->id()}, {"level", 1}, {"layer", 5}}};
    auto nextVolumePass = nextPass;
    nextVolumePass["colorTargets"] =
        QVariantList{QVariantMap{{"texture", nextVolume->id()}, {"level", 1}, {"layer", 1}}};
    QVERIFY(f.success(nextSurface.enqueue({{"passes", QVariantList{nextPass, nextVolumePass}}})));
    QVERIFY(memory->native.used.load() > 0);
    auto *retiring = f.keep(nextSurface.enqueue({{"passes", QVariantList{nextPass}}}));
    QVERIFY(f.host.runtime.setEnabled(f.host.installation(), false));
    QTRY_VERIFY(retiring->finished());
    QCOMPARE(retiring->state(), EchoPluginOperation::Canceled);
    QTRY_VERIFY(nextSurface.drained());

    QTRY_COMPARE(f.host.runtime.state(f.host.installation()), PluginRuntime::Disabled);
    QTRY_VERIFY(surface.drained());
    QCOMPARE(memory->native.used.load(), 0);
    QCOMPARE(memory->targets.used.load(), 0);
    QCOMPARE(memory->readbacks.used.load(), 0);
    QCOMPARE(memory->submissions.used.load(), 0);
}
