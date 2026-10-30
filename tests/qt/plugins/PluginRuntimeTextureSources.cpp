#include "PluginRuntimeGraphicsFixture.h"
#include <QQmlComponent>
#include <QSignalSpy>

void PluginRuntimeTest::graphicsQtTextureSourcesLifecycle()
{
    GraphicsFixture f;
    QVERIFY(f.gpu);
    const auto memory = f.gpu->memoryBudget();
    const auto installation = f.scope->installationId();
    QQmlComponent component(&f.host.engine);
    component.setData(R"(
import QtQuick
Rectangle {
    property var plugin
    property bool glyphVisible: false
    width: 128; height: 40; color: "red"
    layer.enabled: true
    Text { text: "Ag星😀"; font.pixelSize: 22; color: "black"; visible: parent.glyphVisible }
})",
                      QUrl("file:///texture-source.qml"));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    std::unique_ptr<QObject> object(
        component.createWithInitialProperties({{"plugin", QVariant::fromValue(f.scope)}}));
    auto *source = qobject_cast<QQuickItem *>(object.get());
    QVERIFY(source);
    QQuickWindow window;
    window.resize(220, 100);
    source->setParentItem(window.contentItem());
    source->setX(240);
    EchoGpuSurface surface;
    surface.setParentItem(window.contentItem());
    surface.setSize(QSizeF(64, 40));
    surface.setGraphics(f.gpu);
    QVERIFY(!surface.importTextureSource(nullptr));
    auto *image = surface.importTextureSource(source);
    QVERIFY2(image, qPrintable(f.gpu->errorMessage()));
    QCOMPARE(surface.importTextureSource(source), image);
    QVERIFY(image->readOnly());
    QVERIFY(!image->ready());
    QVERIFY(!f.gpu->writeTexture(image, f.host.engine.evaluate("new ArrayBuffer(4)")));
    QVERIFY(f.gpu->shareResource(image).isEmpty());
    QQuickItem ancestor;
    ancestor.setProperty("plugin", QVariant::fromValue(f.scope));
    surface.setParentItem(&ancestor);
    ancestor.setParentItem(window.contentItem());
    QVERIFY(!surface.importTextureSource(&ancestor));
    surface.setParentItem(window.contentItem());
    QQuickWindow other;
    QQuickItem moving;
    moving.setProperty("plugin", QVariant::fromValue(f.scope));
    moving.setParentItem(window.contentItem());
    auto *movingLease = surface.importTextureSource(&moving);
    QVERIFY(movingLease);
    moving.setParentItem(nullptr);
    QVERIFY(!movingLease->active());
    moving.setParentItem(other.contentItem());
    QVERIFY(!surface.importTextureSource(&moving));
    source->setProperty("plugin", QVariant::fromValue<QObject *>(nullptr));
    QVERIFY(!surface.importTextureSource(source));
    source->setProperty("plugin", QVariant::fromValue(f.scope));
    const auto foreignPackage =
        pluginPackage(f.host.temporary.path(), "source.foreign", "export function activate(c) {}");
    f.host.runtime.importDirectory(foreignPackage);
    QTRY_COMPARE(f.host.runtime.rowCount(), 2);
    QString foreignInstallation;
    for (int row = 0; row < f.host.runtime.rowCount(); ++row)
        if (f.host.runtime.data(f.host.runtime.index(row), PluginRuntime::IdRole) ==
            "source.foreign")
            foreignInstallation =
                f.host.runtime.data(f.host.runtime.index(row), PluginRuntime::InstallationRole)
                    .toString();
    QVERIFY(!foreignInstallation.isEmpty());
    QTRY_COMPARE(f.host.runtime.state(foreignInstallation), PluginRuntime::Active);
    source->setProperty("plugin", QVariant::fromValue(f.host.runtime.context(foreignInstallation)));
    QVERIFY(!surface.importTextureSource(source));
    source->setProperty("plugin", QVariant::fromValue(f.scope));
    {
        EchoGpuSurface nested;
        nested.setParentItem(source);
        QVERIFY(!surface.importTextureSource(source));
    }
    {
        EchoGpuSurface transient;
        transient.setParentItem(window.contentItem());
        transient.setGraphics(f.gpu);
        auto *lease = transient.importTextureSource(source);
        QVERIFY(lease && lease->active());
        transient.setGraphics(nullptr);
        QVERIFY(!lease->active());
        QVERIFY(image->active());
    }
    {
        std::unique_ptr<QObject> raced(
            component.createWithInitialProperties({{"plugin", QVariant::fromValue(f.scope)}}));
        auto *item = qobject_cast<QQuickItem *>(raced.get());
        QVERIFY(item);
        item->setParentItem(window.contentItem());
        connect(
            f.gpu, &EchoPluginGraphics::changed, &window, [&] { raced.reset(); },
            Qt::SingleShotConnection);
        QVERIFY(!surface.importTextureSource(item));
    }
    if (!qEnvironmentVariableIsSet("ECHO_TEST_NATIVE_GPU"))
        return;
    auto *pipeline = f.pipeline("source.frag");
    auto *vertices = f.vertices();
    auto *sampler = f.gpu->createSampler();
    QVERIFY(pipeline && vertices && sampler);
    const QVariantMap pass{
        {"pipeline", pipeline->id()},
        {"bindings",
         QVariantList{QVariantMap{{"binding", 0}, {"kind", "uniform"}, {"resource", "frame"}},
                      QVariantMap{{"binding", 2},
                                  {"kind", "texture"},
                                  {"resource", image->id()},
                                  {"sampler", sampler->id()}}}},
        {"draws", QVariantList{QVariantMap{{"count", 4}, {"vertexBuffer", vertices->id()}}}}};
    QVERIFY(surface.submit({{"passes", QVariantList{pass}}}));
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QTRY_VERIFY_WITH_TIMEOUT(image->ready(), 7000);
    QVERIFY2(surface.status().value("state") == "ready",
             qPrintable(surface.status().value("reason").toString()));
    QVERIFY(f.pixels(surface, nullptr, {}, 255, 0, 0));
    auto frames = std::make_unique<QSignalSpy>(&window, &QQuickWindow::frameSwapped);
    source->setProperty("color", QColor(Qt::blue));
    QTRY_VERIFY(frames->size() >= 2);
    QVERIFY(f.pixels(surface, nullptr, {}, 0, 0, 255));
    source->setProperty("color", QColor("#808080"));
    frames->clear();
    QTRY_VERIFY(frames->size() >= 2);
    QVERIFY(f.pixels(surface, nullptr, {}, 128, 128, 128));
    const auto generation = image->generation();
    source->setWidth(96);
    QTRY_VERIFY(image->generation() > generation);
    source->setProperty("glyphVisible", true);
    source->setProperty("color", QColor(Qt::transparent));
    frames->clear();
    QTRY_VERIFY(frames->size() >= 2);
    auto *read = surface.readback();
    QVERIFY(f.success(read));
    const auto pixels = read->result().toMap().value("data").toByteArray();
    int ink = 0, transparent = 0;
    for (int i = 3; i < pixels.size(); i += 4) {
        ink += quint8(pixels[i]) > 32;
        transparent += pixels[i] == 0;
    }
    QVERIFY(ink > 10 && transparent > 10);
    const auto stableGeneration = image->generation();
    const auto stableBytes = memory->native.used.load();
    for (int i = 0; i < 4; ++i) {
        const auto priorFrames = frames->size();
        surface.requestFrame();
        QTRY_VERIFY(frames->size() > priorFrames);
    }
    QCOMPARE(image->generation(), stableGeneration);
    QCOMPARE(memory->native.used.load(), stableBytes);
    surface.setPaused(true);
    QTRY_VERIFY(surface.drained());
    QVERIFY(!image->ready());
    surface.setPaused(false);
    QTRY_VERIFY(image->ready());
    object.reset();
    QVERIFY(!image->active());
    auto *pending = f.keep(surface.readback());
    QVERIFY(QMetaObject::invokeMethod(&f.host.api, "sessionChanged", Q_ARG(quint64, 1)));
    QTRY_VERIFY(pending->finished());
    QCOMPARE(pending->state(), EchoPluginOperation::Canceled);
    QVERIFY(f.host.runtime.setEnabled(installation, false));
    QTRY_COMPARE(f.host.runtime.state(installation), PluginRuntime::Disabled);
    QTRY_COMPARE(memory->native.used.load(), 0);
}
