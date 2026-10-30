#include "PluginRuntimeGraphicsFixture.h"
#include "EchoGpuGlyphAtlas.h"
#include "GlyphRun.h"
#include "GlyphRunCanvas_p.h"
#include <QQmlComponent>
#include <QSignalSpy>
#include <QTextBoundaryFinder>
namespace
{
EchoGpuResource *atlasPage(EchoGpuGlyphAtlas *atlas, int index = 0)
{
    return qobject_cast<EchoGpuResource *>(atlas->pages().value(index).value<QObject *>());
}
bool drawGlyph(GraphicsFixture &f, EchoGpuSurface &surface, EchoGpuGlyphAtlas *atlas,
               int fragmentIndex = 0)
{
    if (!atlas->ready() || atlas->fragments().isEmpty())
        return false;
    const auto level = atlas->fragments()[fragmentIndex].toMap()["levels"].toList().first().toMap();
    auto data = level["uv"].toList();
    const bool colorInk = level["color"].toBool();
    data.append(1.);
    data.append(colorInk ? 1. : 0.);
    data.append(colorInk ? 1. : 0.);
    data.append(level["coverage"] == "a" ? 1. : 0.);
    auto *uniform = f.gpu->createBuffer({{"size", 32}, {"usage", QStringList{"uniform"}}},
                                        f.host.engine.toScriptValue(data));
    auto *pipeline = f.pipeline("glyph.frag");
    auto *vertices = f.vertices();
    auto *sampler = f.gpu->createSampler();
    auto *page = atlasPage(atlas, level["page"].toInt());
    if (!uniform || !pipeline || !vertices || !sampler || !page)
        return false;
    const bool submitted = surface.submit(
        {{"passes", QVariantList{QVariantMap{
                        {"pipeline", pipeline->id()},
                        {"bindings", QVariantList{bind(0, "uniform", "frame"),
                                                  bind(1, "uniform", uniform->id()),
                                                  QVariantMap{{"binding", 2},
                                                              {"kind", "texture"},
                                                              {"resource", page->id()},
                                                              {"sampler", sampler->id()}}}},
                        {"draws", QVariantList{QVariantMap{{"count", 4},
                                                           {"vertexBuffer", vertices->id()}}}}}}}});
    if (!submitted) {
        qWarning() << "Glyph submit" << f.gpu->errorMessage();
        return false;
    }
    auto *read = surface.readback();
    if (!f.success(read)) {
        qWarning() << "Glyph read" << read->property("error") << surface.status() << atlas->ready();
        return false;
    }
    const auto pixels = read->result().toMap()["data"].toByteArray();
    int ink = 0, clear = 0;
    for (int i = 0; i < pixels.size(); i += 4) {
        const auto r = quint8(pixels[i]), g = quint8(pixels[i + 1]), b = quint8(pixels[i + 2]);
        ink += colorInk ? (qMax(r, qMax(g, b)) - qMin(r, qMin(g, b)) > 10)
                        : (r > 32 && g == 0 && b == 0);
        clear += pixels[i + 3] == 0;
    }
    if (ink <= 15 || clear <= 15)
        qWarning() << "Glyph pixels" << ink << clear << pixels.size() << level << surface.status();
    return ink > 15 && clear > 15;
}
} // namespace
void PluginRuntimeTest::graphicsNativeGlyphAtlasLifecycle()
{
    GraphicsFixture f;
    QVERIFY(f.gpu);
    const auto memory = f.gpu->memoryBudget();
    QQuickWindow window;
    window.setPersistentGraphics(false);
    window.setPersistentSceneGraph(false);
    window.resize(360, 120);
    GlyphRun source(window.contentItem());
    source.setProperty("plugin", QVariant::fromValue(f.scope));
    source.setSize({160, 60});
    source.setX(400);
    source.setMaximumRasterScale(1);
    source.setHaloSampling("canvas");
    source.setAtlasOnly(true);
    const auto text = QString::fromUtf8("A中é😀لا");
    const QVariantList runs{QVariantMap{
        {"key", "line"}, {"text", text}, {"size", 32}, {"maxBlur", 0}, {"maxHaloRadius", 0}}};
    source.setRuns(runs);
    QVERIFY(!source.preparing());
    EchoGpuSurface surface;
    surface.setParentItem(window.contentItem());
    surface.setSize({96, 64});
    surface.setGraphics(f.gpu);
    QVERIFY(!surface.importGlyphAtlas(nullptr));
    auto *atlas = surface.importGlyphAtlas(&source);
    QVERIFY(atlas);
    QCOMPARE(surface.importGlyphAtlas(&source), atlas);
    QVERIFY(atlas->active());
    QVERIFY(!atlas->ready());
    QVERIFY(atlas->pages().isEmpty());
    {
        GlyphRun foreign;
        foreign.setProperty("plugin", QVariant::fromValue(f.scope));
        QVERIFY(!surface.importGlyphAtlas(&foreign));
    }
    source.setProperty("plugin", QVariant::fromValue<QObject *>(nullptr));
    QVERIFY(!surface.importGlyphAtlas(&source));
    source.setProperty("plugin", QVariant::fromValue(f.scope));
    if (!qEnvironmentVariableIsSet("ECHO_TEST_NATIVE_GPU"))
        return;
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QTRY_VERIFY_WITH_TIMEOUT(atlas->ready(), 10000);
    QVERIFY2(atlas->errorString().isEmpty(), qPrintable(atlas->errorString()));
    QVERIFY(!atlas->metrics().isEmpty());
    QVERIFY(!atlas->fragments().isEmpty());
    QSet<int> boundaries{0, int(text.size())};
    QTextBoundaryFinder finder(QTextBoundaryFinder::Grapheme, text);
    while (finder.toNextBoundary() > 0)
        boundaries.insert(finder.position());
    for (const auto &value : atlas->fragments()) {
        const auto fragment = value.toMap();
        const int start = fragment["start"].toInt(), end = start + fragment["length"].toInt();
        QVERIFY(boundaries.contains(start) && boundaries.contains(end));
        const auto levels = fragment["levels"].toList();
        QCOMPARE(levels.size(), 4);
        for (const auto &v : levels) {
            const auto level = v.toMap();
            QVERIFY(level["page"].toInt() >= 0 && level["page"].toInt() < atlas->pages().size());
            const auto uv = level["uv"].toList();
            QCOMPARE(uv.size(), 4);
            QVERIFY(uv[0].toDouble() >= 0 && uv[0].toDouble() + uv[2].toDouble() <= 1);
            QVERIFY(uv[1].toDouble() >= 0 && uv[1].toDouble() + uv[3].toDouble() <= 1);
        }
    }
    auto *page = atlasPage(atlas);
    QVERIFY(page && page->readOnly());
    QVERIFY(f.gpu->shareResource(page).isEmpty());
    QVERIFY(!f.gpu->writeTexture(page, f.host.engine.evaluate("new ArrayBuffer(4)")));
    QVERIFY(drawGlyph(f, surface, atlas));
#ifdef Q_OS_MACOS
    bool colored = false;
    for (int i = 0; i < atlas->fragments().size(); ++i)
        if (atlas->fragments()[i].toMap()["levels"].toList().first().toMap()["color"].toBool()) {
            QVERIFY(drawGlyph(f, surface, atlas, i));
            colored = true;
            break;
        }
    QVERIFY(colored);
#endif
    // Another consumer of the same native source reuses actual atlas pages without upload.
    EchoGpuSurface second;
    second.setParentItem(window.contentItem());
    second.setPosition({120, 0});
    second.setSize({96, 64});
    second.setGraphics(f.gpu);
    auto *other = second.importGlyphAtlas(&source);
    QVERIFY(other);
    QTRY_VERIFY(other->ready());
    QCOMPARE(other->fragments(), atlas->fragments());
    QCOMPARE(other->textureUploads(), 0);
    QVERIFY(drawGlyph(f, second, other));
    {
        const auto foreignPackage = pluginPackage(
            f.host.temporary.path(), "glyph.foreign", "export function activate(c) {}",
            {{"capabilities", QJsonArray{"graphics"}},
             {"requires",
              QJsonObject{{"hostVersion", ">=0.1.0"},
                          {"sdkVersion", ">=1.0.0"},
                          {"features", QJsonArray{"lifecycle", "operations", "graphics.rhi",
                                                  "graphics.glyphAtlases"}}}}});
        f.host.runtime.importDirectory(foreignPackage);
        QTRY_COMPARE(f.host.runtime.rowCount(), 2);
        QString id;
        for (int row = 0; row < 2; ++row)
            if (f.host.runtime.data(f.host.runtime.index(row), PluginRuntime::IdRole) ==
                "glyph.foreign")
                id = f.host.runtime.data(f.host.runtime.index(row), PluginRuntime::InstallationRole)
                         .toString();
        QTRY_COMPARE(f.host.runtime.state(id), PluginRuntime::Active);
        auto *scope = f.host.runtime.context(id);
        source.setProperty("plugin", QVariant::fromValue(scope));
        QVERIFY(!surface.importGlyphAtlas(&source));
        source.setProperty("plugin", QVariant::fromValue(f.scope));
        GlyphRun native(window.contentItem());
        native.setProperty("plugin", QVariant::fromValue(scope));
        native.setSize({128, 60});
        native.setMaximumRasterScale(1);
        native.setHaloSampling("canvas");
        native.setAtlasOnly(true);
        native.setRuns(runs);
        EchoGpuSurface consumer;
        consumer.setParentItem(window.contentItem());
        consumer.setSize({96, 64});
        consumer.setGraphics(scope->graphics());
        auto *isolated = consumer.importGlyphAtlas(&native);
        QVERIFY(isolated);
        QTRY_VERIFY_WITH_TIMEOUT(isolated->ready(), 10000);
        QVERIFY(isolated->textureUploads() >
                0); // Same ink, separate plugin/account atlas allocation.
        QVERIFY(f.host.runtime.setEnabled(id, false));
        QTRY_VERIFY(!isolated->active());
    }
    const auto revision = atlas->revision(), generation = atlas->generation(),
               uploads = atlas->textureUploads();
    for (int i = 0; i < 5; ++i) {
        source.setPoses({QVariantMap{{"key", "line"}, {"x", double(i)}, {"rotation", double(i)}}});
        surface.requestFrame();
        QVERIFY(drawGlyph(f, surface, atlas));
    }
    QCOMPARE(atlas->revision(), revision);
    QCOMPARE(atlas->generation(), generation);
    QCOMPARE(atlas->textureUploads(), uploads);
    source.setPoses({QVariantMap{{"key", "unknown"}, {"x", 1.}}});
    QVERIFY(!source.errorString().isEmpty());
    QVERIFY(!atlas->errorString().isEmpty());
    QVERIFY(atlas->ready());
    QCOMPARE(atlas->generation(), generation);
    QVERIFY(drawGlyph(f, surface, atlas));
    source.setPoses({});
    QVERIFY(source.errorString().isEmpty());

    surface.setPaused(true);
    QTRY_VERIFY(surface.drained());
    QVERIFY(!atlas->ready());
    QVERIFY(other->ready());
    QVERIFY(source.ready());
    second.setPaused(true);
    QTRY_VERIFY(second.drained());
    QVERIFY(!source.preparing());
    QVERIFY(!source.ready());
    QVERIFY(source.metrics().isEmpty());
    surface.setPaused(false);
    QTRY_VERIFY_WITH_TIMEOUT(atlas->ready(), 10000);
    QVERIFY(atlas->generation() > generation);
    QVERIFY(drawGlyph(f, surface, atlas));
    surface.setOpacity(0);
    QTRY_VERIFY(surface.drained());
    QVERIFY(!atlas->ready());
    QVERIFY(source.metrics().isEmpty());
    surface.setOpacity(1);
    QTRY_VERIFY_WITH_TIMEOUT(atlas->ready(), 10000);
    surface.setX(500);
    QTRY_VERIFY(surface.drained());
    QVERIFY(!atlas->ready());
    surface.setX(0);
    QTRY_VERIFY_WITH_TIMEOUT(atlas->ready(), 10000);
    const auto before = atlas->generation();
    window.hide();
    window.releaseResources();
    QTRY_VERIFY(surface.drained());
    QVERIFY(!atlas->ready());
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QTRY_VERIFY_WITH_TIMEOUT(atlas->ready(), 10000);
    QVERIFY(atlas->generation() > before);
    QVERIFY(drawGlyph(f, surface, atlas));
    auto *oldPage = atlasPage(atlas);
    source.setRuns({QVariantMap{{"key", "new"}, {"text", "New Ω"}, {"size", 40}, {"maxBlur", 0}}});
    QVERIFY(!oldPage->active());
    QTRY_VERIFY_WITH_TIMEOUT(atlas->ready(), 10000);
    QCOMPARE(atlas->metrics().first().toMap()["key"].toString(), QString("new"));
    QVERIFY(drawGlyph(f, surface, atlas));
    const auto oldDomain = f.gpu->atlasDomain();
    source.setRuns(
        {QVariantMap{{"key", "cancel"}, {"text", QString(4096, QChar(0x661F))}, {"size", 32}}});
    auto *pending = f.keep(surface.readback());

    QVERIFY(QMetaObject::invokeMethod(&f.host.api, "sessionChanged", Q_ARG(quint64, 1)));
    QTRY_VERIFY(!atlas->active());
    QVERIFY(source.runs().isEmpty());
    QVERIFY(source.metrics().isEmpty());
    QVERIFY(oldDomain != f.gpu->atlasDomain());
    QVERIFY(f.host.runtime.setEnabled(f.scope->installationId(), false));
    QTRY_COMPARE(memory->native.used.load(), 0);
    QTRY_VERIFY(pending->finished());
    QCOMPARE(pending->state(), EchoPluginOperation::Canceled);
    QTRY_COMPARE_WITH_TIMEOUT(EchoMusic::GlyphCanvas::Memory::total(), 0, 10000);
}
void PluginRuntimeTest::graphicsFoliaGlyphAtlasConnection()
{
    const auto directory = qEnvironmentVariable("ECHO_FOLIA_FUME_FIXTURE");
    if (directory.isEmpty())
        QSKIP("Set ECHO_FOLIA_FUME_FIXTURE to the verified author PR source");
    GraphicsFixture f;
    QVERIFY(f.gpu);
    QQmlComponent component(&f.host.engine,
                            QUrl::fromLocalFile(directory + "/qml/styles/fume/FumeBatch.qml"));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    QFont font("Arial");
    font.setPixelSize(32);
    const QVariantList commands{QVariantMap{
        {"key", "fume"},
        {"text", "Fume 星😀"},
        {"size", 32},
        {"weight", 700},
        {"x", 4},
        {"y", 40},
        {"style",
         QVariantMap{{"fill", "white"}, {"alpha", 1.}, {"shadow", "transparent"}, {"blur", 0}}}}};
    std::unique_ptr<QObject> object(
        component.createWithInitialProperties({{"commands", commands},
                                               {"baseFont", QVariant::fromValue(font)},
                                               {"maximumRasterScale", 1.},
                                               {"atlasOnly", true}}));
    auto *batch = qobject_cast<QQuickItem *>(object.get());
    QVERIFY2(batch, qPrintable(component.errorString()));
    batch->setProperty("plugin", QVariant::fromValue(f.scope));
    batch->setSize({200, 100});
    QQuickWindow window;
    window.resize(320, 120);
    batch->setParentItem(window.contentItem());
    EchoGpuSurface surface;
    surface.setParentItem(window.contentItem());
    surface.setSize({96, 64});
    surface.setGraphics(f.gpu);
    QVariant result;
    const QVariant gpuObject = QVariant::fromValue(&surface);
    QVERIFY(QMetaObject::invokeMethod(batch, "importAtlas", Q_RETURN_ARG(QVariant, result),
                                      Q_ARG(QVariant, gpuObject)));
    auto *atlas = qobject_cast<EchoGpuGlyphAtlas *>(result.value<QObject *>());
    QVERIFY(atlas);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QTRY_VERIFY_WITH_TIMEOUT(atlas->ready(), 10000);
    QTRY_VERIFY(batch->property("posesCurrent").toBool());
    QCOMPARE(batch->property("errorString").toString(), QString());
    QVERIFY(drawGlyph(f, surface, atlas));
    // Native drawing is retained: opting back in must keep the PR's preparation/poses path.
    batch->setProperty("atlasOnly", false);
    QTRY_VERIFY_WITH_TIMEOUT(atlas->ready(), 10000);
    QTRY_VERIFY(batch->property("posesCurrent").toBool());
    QVERIFY(drawGlyph(f, surface, atlas));
    object.reset();
    QVERIFY(!atlas->active());
}

void PluginRuntimeTest::graphicsStaticGlyphAtlasReuseAndReentrancy()
{
    if (!qEnvironmentVariableIsSet("ECHO_TEST_NATIVE_GPU"))
        return;
    GraphicsFixture f;
    QVERIFY(f.gpu);
    QQuickWindow window;
    window.resize(360, 120);
    GlyphRun native(window.contentItem());
    native.setProperty("plugin", QVariant::fromValue(f.scope));
    native.setSize({128, 60});
    native.setPosition({180, 0});
    native.setRuns({QVariantMap{
        {"key", "static"}, {"text", QString("Native")}, {"size", 32}, {"maxBlur", 0}}});
    bool reentered = false;
    connect(&native, &GlyphRun::renderingChanged, &native, [&] {
        if (reentered)
            return;
        reentered = true;
        const auto accepted = native.runs();
        native.retireAtlasPreparation();
        native.setRuns(accepted);
    });
    EchoGpuSurface surface;
    surface.setParentItem(window.contentItem());
    surface.setSize({96, 64});
    surface.setGraphics(f.gpu);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QTRY_VERIFY(reentered && native.textureUploads() >= 2 && native.renderBatches() > 0);
    auto *atlas = surface.importGlyphAtlas(&native);
    QVERIFY(atlas);
    QTRY_VERIFY_WITH_TIMEOUT(atlas->ready(), 10000);
    QCOMPARE(atlas->fragments().first().toMap()["levels"].toList().size(), 1);
    QCOMPARE(atlas->textureUploads(), 0);
    QVERIFY(drawGlyph(f, surface, atlas));
    native.setAtlasOnly(true);
    QTRY_VERIFY_WITH_TIMEOUT(atlas->ready(), 10000);
    QVERIFY(drawGlyph(f, surface, atlas));
    native.setVisible(false);
    QTRY_VERIFY(!atlas->ready());
    native.setVisible(true);
    QTRY_VERIFY_WITH_TIMEOUT(atlas->ready(), 10000);
    QVERIFY(drawGlyph(f, surface, atlas));
    atlasPage(atlas)->dispose();
    QVERIFY(!atlas->active());
    QVERIFY(!atlas->ready());
}
