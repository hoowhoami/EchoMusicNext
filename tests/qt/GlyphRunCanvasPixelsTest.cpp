#include "GlyphRun.h"
#include "GlyphRunCanvas_p.h"
#include "GlyphRunStaticLayout_p.h"
#include "GlyphAtlas_p.h"
#include <QTextLayout>
#include <QGlyphRun>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QQuickRenderControl>
#include <QQuickRenderTarget>
#include <QResource>
#include <QtQuick/private/qquickwindow_p.h>
#include <QtTest>
#include <rhi/qrhi.h>
#include <cmath>
#include <array>
#include <algorithm>
#include <functional>
#ifdef Q_OS_WIN
#include <QLibrary>
#include <QScopeGuard>
#include <QtCore/qt_windows.h>
#include <d3dcompiler.h>
#endif

namespace
{
// GlyphRun observes window visibility. An embedded RenderControl owner can be
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
class GlyphTarget
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
        const QSize logical(256, 144);
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
    QImage read(const std::function<void(QRhi *, QRhiCommandBuffer *)> &consume = {})
    {
        control.polishItems();
        control.beginFrame();
        control.sync();
        control.render();
        if (consume)
            consume(control.rhi(), control.commandBuffer());
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
    ~GlyphTarget()
    {
        window.setVisible(false);
        window.setRenderTarget({});
        control.invalidate();
    }
};
} // namespace

class GlyphRunCanvasPixelsTest : public QObject
{
    Q_OBJECT
private slots:
    void staticLiveLayoutsKeepIndependentPixelsAndRetirement_data()
    {
        QTest::addColumn<qreal>("ratio");
        QTest::addColumn<qreal>("stroke");
        for (const qreal ratio : {1., 1.25, 2.})
            for (const qreal stroke : {0., 1.25})
                QTest::newRow(qPrintable(QString("%1-%2").arg(ratio).arg(stroke)))
                    << ratio << stroke;
    }
    void staticLiveLayoutsKeepIndependentPixelsAndRetirement()
    {
        using EchoMusic::GlyphRun;
        using EchoMusic::GlyphStaticLayout::statistics;
        QFETCH(qreal, ratio);
        QFETCH(qreal, stroke);
        QCOMPARE(statistics().liveResults, qsizetype(0));
        GlyphTarget scene;
        QVERIFY(scene.create(ratio));
        QFont font = QGuiApplication::font();
        font.setFamilies({"Arial", "PingFang SC", "Apple Color Emoji"});
        font.setPixelSize(24);
        const QVariantList runs{QVariantMap{{"key", "line"},
                                            {"text", QString::fromUtf8("缓存 ffi 星😀")},
                                            {"strokeWidth", stroke},
                                            {"strokeBitmap", "fill"},
                                            {"maxBlur", 0},
                                            {"maxHaloRadius", 0}}};
        const QVariantList poses{
            QVariantMap{{"key", "line"}, {"x", 12.}, {"y", 24.}, {"color", QColor(Qt::white)}}};
        const auto initialize = [&](GlyphRun &glyph) {
            glyph.setSize({256, 144});
            glyph.setFont(font);
            glyph.setRuns(runs);
            glyph.setPoses(poses);
        };
        auto first = std::make_unique<GlyphRun>(scene.window.contentItem());
        initialize(*first);
        QVERIFY2(first->errorString().isEmpty(), qPrintable(first->errorString()));
        scene.read();
        const auto reference = scene.read();
        QVERIFY(!reference.isNull());
        const auto originalMetrics = first->metrics();
        const auto before = statistics();
        first->setOpacity(0);
        auto second = std::make_unique<GlyphRun>(scene.window.contentItem());
        initialize(*second);
        QCOMPARE(statistics().hits, before.hits + 1);
        QCOMPARE(statistics().builds, before.builds);
        QCOMPARE(second->metrics(), originalMetrics);
        scene.read();
        QCOMPARE(scene.read(), reference);
        const auto secondUploads = second->textureUploads();
        first->setVisible(false);
        QCOMPARE(first->cacheBytes(), 0);
        scene.read();
        QCOMPARE(scene.read(), reference);
        QCOMPARE(second->textureUploads(), secondUploads);
        first->setVisible(true);
        first.reset();
        scene.read();
        QCOMPARE(scene.read(), reference);
        QCOMPARE(second->textureUploads(), secondUploads);
        QVERIFY(secondUploads > 0);
        const auto beforeDatabase = statistics();
        QVERIFY(QMetaObject::invokeMethod(qGuiApp, "fontDatabaseChanged", Qt::DirectConnection));
        auto third = std::make_unique<GlyphRun>(scene.window.contentItem());
        third->setOpacity(0);
        initialize(*third);
        QCOMPARE(statistics().builds, beforeDatabase.builds + 1);
        QCOMPARE(third->metrics(), originalMetrics);
        auto changed = font;
        changed.setPixelSize(30);
        const auto beforeFont = statistics().builds;
        third->setFont(changed);
        QCOMPARE(statistics().builds, beforeFont + 1);
        QVERIFY(third->metrics() != originalMetrics);
        scene.read();
        QCOMPARE(scene.read(), reference);
        // A second DPR owns a different CPU layout and its own GPU pages.
        {
            GlyphTarget other;
            QVERIFY(other.create(ratio == 2 ? 1 : 2));
            const auto beforeDensity = statistics().builds;
            GlyphRun glyph(other.window.contentItem());
            initialize(glyph);
            QCOMPARE(statistics().builds, beforeDensity + 1);
            QVERIFY(!other.read().isNull());
        }
        third.reset();
        second->setVisible(false);
        QCOMPARE(second->cacheBytes(), 0);
        QCOMPARE(statistics().liveResults, qsizetype(0));
        second.reset();
        scene.read();
        QVERIFY(!scene.window.handle());
    }
    void computeShadersCompileWithWindowsFxc()
    {
#ifdef Q_OS_WIN
        if (qEnvironmentVariableIsSet("ECHO_GLYPH_REFERENCE_COMPUTE_RCC"))
            QSKIP("This pixel reference intentionally disables compute shaders");
        QLibrary library(QStringLiteral("d3dcompiler_47"));
        QVERIFY2(library.load(), qPrintable(library.errorString()));
        const auto compile = reinterpret_cast<decltype(&D3DCompile)>(library.resolve("D3DCompile"));
        QVERIFY(compile);
        for (const auto *name : {"vertical.comp", "vertical-small.comp", "horizontal.comp"}) {
            QFile file(
                QString(":/qt/qml/EchoMusic/PluginSdk/shaders/glyph-canvas/%1.qsb").arg(name));
            QVERIFY(file.open(QIODevice::ReadOnly));
            const auto shader = QShader::fromSerialized(file.readAll());
            QVERIFY(shader.isValid());
            const auto hlsl = shader.shader({QShader::HlslShader, 50, QShader::StandardShader});
            QVERIFY2(!hlsl.shader().isEmpty(), name);
            ID3DBlob *bytecode = nullptr, *errors = nullptr;
            const auto release = qScopeGuard([&] {
                if (bytecode)
                    bytecode->Release();
                if (errors)
                    errors->Release();
            });
            const auto status =
                compile(hlsl.shader().constData(), SIZE_T(hlsl.shader().size()), name, nullptr,
                        nullptr, hlsl.entryPoint().constData(), "cs_5_0", 0, 0, &bytecode, &errors);
            const auto message =
                errors ? QByteArray(static_cast<const char *>(errors->GetBufferPointer()),
                                    qsizetype(errors->GetBufferSize()))
                       : QByteArray(name);
            QVERIFY2(SUCCEEDED(status) && bytecode, message.constData());
        }
#else
        QSKIP("Requires the Windows D3DCompiler/FXC implementation");
#endif
    }
    void initTestCase()
    {
        const auto computeReference = qEnvironmentVariable("ECHO_GLYPH_REFERENCE_COMPUTE_RCC");
        if (!computeReference.isEmpty()) {
            Q_CLEANUP_RESOURCE(echo_plugin_glyph_canvas_compute_shaders);
            QVERIFY(QResource::registerResource(computeReference));
            QFile selected(":/qt/qml/EchoMusic/PluginSdk/shaders/glyph-canvas/vertical.comp.qsb");
            QVERIFY(selected.open(QIODevice::ReadOnly));
            QCOMPARE(selected.readAll(), QByteArray("unsupported-compute-test"));
        }
        const auto reference = qEnvironmentVariable("ECHO_GLYPH_REFERENCE_RCC");
        if (reference.isEmpty())
            return;
        Q_CLEANUP_RESOURCE(echo_plugin_glyph_canvas_shaders);
        QVERIFY(QResource::registerResource(reference));
        QFile expected(qEnvironmentVariable("ECHO_GLYPH_REFERENCE_QSB"));
        QFile selected(":/qt/qml/EchoMusic/PluginSdk/shaders/glyph-canvas/blur.frag.qsb");
        QVERIFY(expected.open(QIODevice::ReadOnly));
        QVERIFY(selected.open(QIODevice::ReadOnly));
        QCOMPARE(selected.readAll(), expected.readAll());
    }
    void cleanupTestCase()
    {
        QTRY_COMPARE_WITH_TIMEOUT(EchoMusic::GlyphCanvas::Memory::total(), qint64(0), 5000);
    }
    void curvesArePreparedOnlyForVisibleOutlinePasses_data()
    {
        QTest::addColumn<qreal>("ratio");
        QTest::newRow("100-percent") << qreal(1);
        QTest::newRow("125-percent") << qreal(1.25);
        QTest::newRow("200-percent") << qreal(2);
    }
    void curvesArePreparedOnlyForVisibleOutlinePasses()
    {
        GlyphTarget scene;
        QFETCH(qreal, ratio);
        QVERIFY(scene.create(ratio));
        QFont font("Arial");
        font.setPixelSize(30);
        const auto initialize = [&](EchoMusic::GlyphRun &glyph, const QString &key,
                                    const QString &text) {
            glyph.setSize(scene.window.size());
            glyph.setFont(font);
            glyph.setMaximumRasterScale(2);
            glyph.setHaloSampling("canvas");
            glyph.setRuns({QVariantMap{
                {"key", key}, {"text", text}, {"maxBlur", 0}, {"maxHaloRadius", 160}}});
        };
        EchoMusic::GlyphRun warm(scene.window.contentItem());
        initialize(warm, "warm", "I");
        QTRY_VERIFY2_WITH_TIMEOUT(!warm.preparing() && !warm.metrics().isEmpty(),
                                  qPrintable(warm.errorString()), 5000);
        warm.setPoses({QVariantMap{{"key", "warm"}, {"x", 8.}, {"y", 30.}}});
        scene.read();
        scene.read();
        QTRY_VERIFY(warm.ready());
        EchoMusic::GlyphRun glyph(scene.window.contentItem());
        initialize(glyph, "ink", QString::fromUtf8("墨 Aa"));
        QTRY_VERIFY2_WITH_TIMEOUT(!glyph.preparing() && !glyph.metrics().isEmpty(),
                                  qPrintable(glyph.errorString()), 5000);
        const auto pose = [](bool halo, qreal x) {
            QVariantMap value{{"key", "ink"}, {"x", x}, {"y", 70.}, {"color", QColor(Qt::white)}};
            if (halo)
                value["halos"] =
                    QVariantList{QVariantMap{{"radius", 40.}, {"color", QColor(Qt::white)}}};
            return value;
        };
        glyph.setPoses({pose(false, 50.)});
        // Reserve accounting headroom without allocating a test-sized heap.
        // Ink/bindings fit; the separate 8 MiB curve scratch deliberately does not.
        EchoMusic::GlyphCanvas::Memory pressure;
        QVERIFY(pressure.resize(EchoMusic::GlyphCanvas::NativeBudget -
                                EchoMusic::GlyphCanvas::Memory::total() - 6LL * 1024 * 1024));
        scene.read();
        auto ink = scene.read();
        QTRY_VERIFY2(glyph.ready(), qPrintable(glyph.errorString()));
        QVERIFY(!ink.isNull());
        QVERIFY(glyph.errorString().isEmpty());
        glyph.setPoses({pose(true, 50.)});
        scene.read();
        QTRY_VERIFY(glyph.errorString().contains("curve preparation"));
        QVERIFY(pressure.resize(0));
        initialize(glyph, "ink", QString::fromUtf8("墨 Aa"));
        QTRY_VERIFY2_WITH_TIMEOUT(!glyph.preparing() && !glyph.metrics().isEmpty(),
                                  qPrintable(glyph.errorString()), 5000);
        glyph.setPoses({pose(true, 50.25)});
        scene.read();
        scene.read();
        QTRY_VERIFY2(glyph.ready() && glyph.errorString().isEmpty(),
                     qPrintable(glyph.errorString()));
        // Leave sparse curve slots and recover their native state more than once.
        glyph.setVisible(false);
        scene.read();
        glyph.setVisible(true);
        QTRY_VERIFY_WITH_TIMEOUT(!glyph.preparing(), 5000);
        scene.read();
        scene.read();
        QTRY_VERIFY(glyph.ready());
        QVERIFY(glyph.errorString().isEmpty());
    }
    void staticTexturesRecoverWithoutPoseChanges_data()
    {
        QTest::addColumn<qreal>("ratio");
        QTest::newRow("100-percent") << qreal(1);
        QTest::newRow("125-percent") << qreal(1.25);
    }
    void staticTexturesRecoverWithoutPoseChanges()
    {
        QFETCH(qreal, ratio);
        QTest::failOnWarning(QRegularExpression("No QSGTexture provided"));
        GlyphTarget scene;
        QVERIFY(scene.create(ratio));
        EchoMusic::GlyphRun glyph(scene.window.contentItem());
        glyph.setSize(scene.window.size());
        QFont font("Arial");
        font.setPixelSize(32);
        glyph.setFont(font);
        glyph.setRuns({QVariantMap{{"key", "static"},
                                   {"text", QString::fromUtf8("墨 Aa😀")},
                                   {"maxBlur", 8},
                                   {"maxHaloRadius", 40}}});
        QVERIFY2(glyph.errorString().isEmpty(), qPrintable(glyph.errorString()));
        QTRY_VERIFY2_WITH_TIMEOUT(!glyph.preparing() && !glyph.metrics().isEmpty(),
                                  qPrintable(glyph.errorString()), 5000);
        glyph.setPoses({QVariantMap{
            {"key", "static"},
            {"x", 24.},
            {"y", 70.},
            {"color", QColor(Qt::white)},
            {"blur", 2.},
            {"halos", QVariantList{QVariantMap{{"radius", 7.}, {"color", QColor(Qt::cyan)}}}}}});
        const auto reference = scene.read();
        QVERIFY(!reference.isNull());
        int ink = 0;
        for (int y = 0; y < reference.height(); ++y)
            for (int x = 0; x < reference.width(); ++x)
                ink += qAlpha(reference.pixel(x, y)) > 32;
        QVERIFY(ink > 100);
        const auto revision = glyph.maskRevision();
        const auto metrics = glyph.metrics();
        const auto uploadCount = glyph.textureUploads();
        QVERIFY(uploadCount > 0);
        auto preparation = glyph.atlasPreparation();
        QVERIFY(preparation && preparation->valid->load());
        // Refresh the same C++ borrow seam as a native atlas consumer. The old
        // lease must retire before any consumer can sample the replacement.
        connect(&glyph, &EchoMusic::GlyphRun::atlasChanged, &glyph,
                [&] { preparation = glyph.atlasPreparation(); });
        const auto readAtlas = [&]() {
            QString error;
            EchoMusic::NativeGlyphAtlas borrowed;
            QRhiReadbackResult result;
            scene.read([&](QRhi *rhi, QRhiCommandBuffer *commands) {
                auto *updates = rhi->nextResourceUpdateBatch();
                borrowed = preparation->acquire(&scene.window, updates, error);
                if (!borrowed.textures.empty())
                    updates->readBackTexture(borrowed.textures.front(), &result);
                commands->resourceUpdate(updates);
            });
            if (!error.isEmpty() || borrowed.uploads != 0)
                return QByteArray();
            return result.data;
        };
        const auto atlasPixels = readAtlas();
        QVERIFY(!atlasPixels.isEmpty());
        for (int cycle = 0; cycle < 3; ++cycle) {
            const auto retired = preparation;
            const auto uploads = glyph.textureUploads();
            QVERIFY(QMetaObject::invokeMethod(&scene.window, "sceneGraphAboutToStop",
                                              Qt::DirectConnection));
            QVERIFY(!retired->valid->load());
            QCoreApplication::processEvents();
            // No setPoses/setRuns/update call: the first resumed frame must
            // repair the retained node even when playback is paused.
            QCOMPARE(scene.read(), reference);
            QCOMPARE(glyph.maskRevision(), revision);
            QCOMPARE(glyph.metrics(), metrics);
            QCOMPARE(glyph.textureUploads() - uploads, uploadCount);
            QVERIFY(preparation && preparation != retired && preparation->valid->load());
            QCOMPARE(readAtlas(), atlasPixels);
            scene.read();
            QCOMPARE(glyph.textureUploads() - uploads, uploadCount);
        }
        const auto retired = preparation;
        QVERIFY(QMetaObject::invokeMethod(&scene.window, "sceneGraphAboutToStop",
                                          Qt::DirectConnection));
        glyph.setVisible(false);
        QCoreApplication::processEvents();
        QVERIFY(!glyph.ready());
        QCOMPARE(glyph.cacheBytes(), qint64(0));
        const auto hidden = scene.read();
        QCOMPARE(glyph.renderBatches(), 0);
        QVERIFY(!retired->valid->load());
        for (int y = 0; y < hidden.height(); ++y)
            for (int x = 0; x < hidden.width(); ++x)
                QCOMPARE(qAlpha(hidden.pixel(x, y)), 0);
        glyph.setVisible(true);
        QTRY_VERIFY_WITH_TIMEOUT(!glyph.preparing() && !glyph.metrics().isEmpty(), 5000);
        QCOMPARE(scene.read(), reference);
        {
            auto closing = std::make_unique<EchoMusic::GlyphRun>(scene.window.contentItem());
            closing->setSize(scene.window.size());
            closing->setRuns({QVariantMap{{"key", "closing"}, {"text", "Closing"}}});
            scene.read();
            QVERIFY(QMetaObject::invokeMethod(&scene.window, "sceneGraphAboutToStop",
                                              Qt::DirectConnection));
            closing.reset();
            QCoreApplication::processEvents();
            QCOMPARE(scene.read(), reference);
        }
        QVERIFY(!scene.window.handle());
    }
    void inkAndDenseSharedKernels()
    {
        GlyphTarget scene;
        QVERIFY(scene.create(1));
        auto *rhi = scene.control.rhi();
        qInfo() << "Grouped convolution device prerequisites" << rhi->backend()
                << rhi->isFeatureSupported(QRhi::Compute)
                << rhi->isTextureFormatSupported(QRhiTexture::RGBA16F,
                                                 QRhiTexture::RenderTarget |
                                                     QRhiTexture::UsedWithLoadStore)
                << rhi->isTextureFormatSupported(QRhiTexture::RGBA8,
                                                 QRhiTexture::RenderTarget |
                                                     QRhiTexture::UsedWithLoadStore)
                << rhi->isYUpInFramebuffer() << rhi->resourceLimit(QRhi::MaxThreadsPerThreadGroup)
                << rhi->resourceLimit(QRhi::MaxThreadGroupY);
        EchoMusic::GlyphRun glyph(scene.window.contentItem());
        glyph.setSize(scene.window.size());
        QFont font;
        font.setPixelSize(6);
        glyph.setFont(font);
        glyph.setMaximumRasterScale(2);
        glyph.setHaloSampling("canvas");
        glyph.setRuns(
            {QVariantMap{{"key", "ink"}, {"text", "I"}, {"maxBlur", 0}, {"maxHaloRadius", 40}}});
        QTRY_VERIFY2_WITH_TIMEOUT(!glyph.preparing() && !glyph.metrics().isEmpty(),
                                  qPrintable(glyph.errorString()), 5000);
        const auto pose = [](int i, bool halo) {
            QVariantMap value{{"key", "ink"},
                              {"instance", QString::number(i)},
                              {"x", 4. + (i % 32) * 7.25},
                              {"y", 12. + (i / 32) * 7.5},
                              {"color", QColor(Qt::white)}};
            if (halo)
                value["halos"] =
                    QVariantList{QVariantMap{{"radius", 1.}, {"color", QColor(Qt::white)}}};
            return value;
        };
        // Ink-only nodes still bind a valid kernel range, without a blur packet.
        glyph.setPoses({pose(0, false)});
        scene.read();
        auto image = scene.read();
        QVERIFY2(!image.isNull() && glyph.errorString().isEmpty(), qPrintable(glyph.errorString()));
        const auto visiblePixels = [](const QImage &input) {
            const auto rgba = input.convertToFormat(QImage::Format_ARGB32_Premultiplied);
            int count = 0;
            for (int y = 0; y < rgba.height(); ++y)
                for (int x = 0; x < rgba.width(); ++x)
                    count += qAlpha(rgba.pixel(x, y)) > 2;
            return count;
        };
        QVERIFY(visiblePixels(image) > 0);
        // Distinct positions require separate halo passes but share one kernel.
        QVariantList poses;
        for (int i = 0; i < 512; ++i)
            poses.append(pose(i, true));
        glyph.setPoses(poses);
        scene.read();
        image = scene.read();
        QVERIFY2(!image.isNull() && glyph.errorString().isEmpty(), qPrintable(glyph.errorString()));
        QVERIFY(visiblePixels(image) > 1000);
        glyph.setPoses({pose(0, false)});
        scene.read();
        image = scene.read();
        QVERIFY2(!image.isNull() && glyph.errorString().isEmpty(), qPrintable(glyph.errorString()));
        QVERIFY(visiblePixels(image) < 100);
        for (int cycle = 0; cycle < 3; ++cycle) {
            glyph.setVisible(false);
            scene.read();
            QTRY_COMPARE_WITH_TIMEOUT(glyph.gpuCacheBytes(), qint64(0), 5000);
            glyph.setVisible(true);
            QTRY_VERIFY_WITH_TIMEOUT(!glyph.preparing() && !glyph.metrics().isEmpty(), 5000);
            glyph.setPoses({pose(0, true)});
            scene.read();
            image = scene.read();
            QVERIFY2(!image.isNull() && glyph.errorString().isEmpty(),
                     qPrintable(glyph.errorString()));
            QVERIFY(visiblePixels(image) > 0);
        }
        QVERIFY(!scene.window.handle());
    }
    void atlasChannelsRemainIndependent()
    {
        GlyphTarget scene;
        QVERIFY(scene.create(1));
        EchoMusic::GlyphRun glyph(scene.window.contentItem());
        glyph.setSize(scene.window.size());
        QFont font("Arial");
        font.setPixelSize(400);
        glyph.setFont(font);
        glyph.setMaximumRasterScale(1);
        glyph.setHaloSampling("canvas");
        glyph.setRuns(
            {QVariantMap{{"key", "wide"}, {"text", "W"}, {"maxBlur", 0}, {"maxHaloRadius", 40}}});
        QTRY_VERIFY2_WITH_TIMEOUT(!glyph.preparing() && !glyph.metrics().isEmpty(),
                                  qPrintable(glyph.errorString()), 5000);
        QTextLayout layout("W", font);
        layout.beginLayout();
        auto line = layout.createLine();
        line.setLineWidth(10000);
        layout.endLayout();
        const auto bounds = layout.glyphRuns(0, 1).first().boundingRect();
        // QGlyphRun's complete native box, rather than its tight ink box,
        // determines these sigma4 fields (no pyramid,13px border and gutter).
        QCOMPARE(scene.window.effectiveDevicePixelRatio(), 1.);
        const int tileWidth = qFloor(bounds.width()) + 28;
        const int tileHeight = qFloor(bounds.height()) + 28;
        const int perChannel = (1024 / tileWidth) * (1024 / tileHeight);
        QVERIFY(tileWidth > 256 && perChannel > 0 && perChannel < 30);
        qInfo() << "Four-channel field lower bound" << tileWidth << tileHeight << perChannel;
        const int count = 3 * perChannel + 1;
        const std::array<QColor, 4> colors{Qt::red, Qt::green, Qt::blue, Qt::white};
        QVariantList poses;
        for (int i = 0; i < count; ++i)
            poses.append(QVariantMap{
                {"key", "wide"},
                {"instance", QString::number(i)},
                {"x", 128. - bounds.center().x() + i * .125},
                {"y", 72. - bounds.center().y() + i * .25},
                {"color", QColor(Qt::transparent)},
                {"halos", QVariantList{QVariantMap{
                              {"radius", 4.}, {"opacity", .125}, {"color", colors[i % 4]}}}}});
        for (const auto &name : {QString("fields-forward.png"), QString("fields-reversed.png")}) {
            glyph.setPoses(poses);
            scene.read();
            QCoreApplication::processEvents();
            QVERIFY2(glyph.errorString().isEmpty(), qPrintable(glyph.errorString()));
            const auto image = scene.read().convertToFormat(QImage::Format_ARGB32_Premultiplied);
            QVERIFY2(!image.isNull() && glyph.errorString().isEmpty(),
                     qPrintable(glyph.errorString()));
            int colored = 0;
            for (int y = 0; y < image.height(); ++y)
                for (int x = 0; x < image.width(); ++x)
                    colored += qRed(image.pixel(x, y)) > 2 && qGreen(image.pixel(x, y)) > 2 &&
                               qBlue(image.pixel(x, y)) > 2;
            QVERIFY(colored > 1000);
            const auto dump = qEnvironmentVariable("ECHO_GLYPH_PIXEL_DUMP");
            if (!dump.isEmpty()) {
                QVERIFY(QDir().mkpath(dump));
                QVERIFY(image.save(QDir(dump).filePath(name)));
            }
            const auto reference = qEnvironmentVariable("ECHO_GLYPH_FIELD_REFERENCE_IMAGES");
            if (!reference.isEmpty()) {
                const auto expected = QImage(QDir(reference).filePath(name))
                                          .convertToFormat(QImage::Format_ARGB32_Premultiplied);
                QCOMPARE(image.size(), expected.size());
                int maximum = 0;
                for (int y = 0; y < image.height(); ++y)
                    for (int x = 0; x < image.width(); ++x) {
                        const auto a = image.pixel(x, y), b = expected.pixel(x, y);
                        maximum =
                            std::max({maximum, qAbs(qRed(a) - qRed(b)), qAbs(qGreen(a) - qGreen(b)),
                                      qAbs(qBlue(a) - qBlue(b)), qAbs(qAlpha(a) - qAlpha(b))});
                    }
                qInfo() << name << "Maximum four-channel reference difference" << maximum;
                QVERIFY(maximum <= 2);
            }
            std::reverse(poses.begin(), poses.end());
        }
        QVERIFY(!scene.window.handle());
    }
    void kernelSampling_data()
    {
        QTest::addColumn<double>("radius");
        QTest::addColumn<double>("ratio");
        for (int window = 1; window <= 85; ++window)
            QTest::newRow(qPrintable(QString("window-%1").arg(window)))
                << window / (3 * std::sqrt(2 * M_PI) / 4) << 1.;
        for (const double radius : {18., 48., 95., 160.})
            for (const double ratio : {1.25, 1.5, 2.})
                QTest::newRow(qPrintable(QString("pyramid-%1-dpr-%2").arg(radius).arg(ratio)))
                    << radius << ratio;
    }
    void kernelSampling()
    {
        QFETCH(double, radius);
        QFETCH(double, ratio);
        GlyphTarget scene;
        const bool initialized = scene.create(ratio);
#ifndef Q_OS_MACOS
        if (!initialized && !scene.control.rhi())
            QSKIP("No offscreen RHI device available");
#endif
        QVERIFY(initialized);
        EchoMusic::GlyphRun glyph(scene.window.contentItem());
        glyph.setSize(scene.window.size());
        QFont font;
        font.setPixelSize(6);
        glyph.setFont(font);
        glyph.setMaximumRasterScale(2);
        glyph.setHaloSampling("canvas");
        glyph.setRuns(
            {QVariantMap{{"key", "thin"}, {"text", "Il"}, {"maxBlur", 0}, {"maxHaloRadius", 512}}});
        QTRY_VERIFY2_WITH_TIMEOUT(!glyph.preparing() && !glyph.metrics().isEmpty(),
                                  qPrintable(glyph.errorString()), 5000);
        const auto pose = [radius](double x, double y) {
            return QVariantMap{{"key", "thin"},
                               {"instance", QString::number(x)},
                               {"x", x},
                               {"y", y},
                               {"color", QColor(Qt::transparent)},
                               {"halos", QVariantList{QVariantMap{{"radius", radius},
                                                                  {"color", QColor(Qt::white)}}}}};
        };
        auto warm = pose(29.5, 31.25);
        warm["halos"] = QVariantList{QVariantMap{{"radius", std::min(160., radius * 1.8 + 3)},
                                                 {"color", QColor(Qt::white)}}};
        glyph.setPoses({warm, pose(217.75, -2.5)});
        scene.read();
        scene.read();
        glyph.setPoses({pose(127.25, 70.5), pose(-1.25, 142.75)});
        scene.read();
        const auto image = scene.read().convertToFormat(QImage::Format_ARGB32_Premultiplied);
        QVERIFY2(!image.isNull() && glyph.errorString().isEmpty(), qPrintable(glyph.errorString()));
        const auto name =
            QString("kernel-%1.png").arg(QString::fromLatin1(QTest::currentDataTag()));
        const auto dump = qEnvironmentVariable("ECHO_GLYPH_PIXEL_DUMP");
        if (!dump.isEmpty()) {
            QVERIFY(QDir().mkpath(dump));
            QVERIFY(image.save(QDir(dump).filePath(name)));
        }
        const auto reference = qEnvironmentVariable("ECHO_GLYPH_REFERENCE_IMAGES");
        if (!reference.isEmpty()) {
            const auto expected = QImage(QDir(reference).filePath(name))
                                      .convertToFormat(QImage::Format_ARGB32_Premultiplied);
            QCOMPARE(image.size(), expected.size());
            int maximum = 0;
            for (int y = 0; y < image.height(); ++y)
                for (int x = 0; x < image.width(); ++x) {
                    const auto a = image.pixel(x, y), b = expected.pixel(x, y);
                    maximum =
                        std::max({maximum, qAbs(qRed(a) - qRed(b)), qAbs(qGreen(a) - qGreen(b)),
                                  qAbs(qBlue(a) - qBlue(b)), qAbs(qAlpha(a) - qAlpha(b))});
                }
            qInfo() << "Maximum reference channel difference" << maximum;
            QVERIFY(maximum <= 2);
        }
        QVERIFY(!scene.window.handle());
    }
    void clippedAndSharedHalos_data()
    {
        QTest::addColumn<double>("ratio");
        QTest::addColumn<double>("radius");
        QTest::addColumn<double>("rotation");
        QTest::newRow("box") << 1. << 4. << 0.;
        QTest::newRow("reduced-storage") << 1. << 12. << 0.;
        QTest::newRow("outline-pyramid") << 1.5 << 48. << 0.;
        QTest::newRow("rotated") << 1.25 << 18. << 17.;
    }
    void clippedAndSharedHalos()
    {
        QFETCH(double, ratio);
        QFETCH(double, radius);
        QFETCH(double, rotation);
        GlyphTarget scene;
        const bool initialized = scene.create(ratio);
#ifndef Q_OS_MACOS
        if (!initialized && !scene.control.rhi())
            QSKIP("No offscreen RHI device available");
#endif
        QVERIFY(initialized);
        EchoMusic::GlyphRun glyph(scene.window.contentItem());
        glyph.setSize(scene.window.size());
        glyph.setTransformOrigin(QQuickItem::TopLeft);
        glyph.setRotation(rotation);
        QFont font;
        font.setPixelSize(36);
        glyph.setFont(font);
        glyph.setMaximumRasterScale(2);
        glyph.setHaloSampling("canvas");
        glyph.setRuns({QVariantMap{
            {"key", "ink"}, {"text", "office MMM"}, {"maxBlur", 0}, {"maxHaloRadius", 160}}});
        QTRY_VERIFY2_WITH_TIMEOUT(!glyph.preparing() && !glyph.metrics().isEmpty(),
                                  qPrintable(glyph.errorString()), 5000);
        const auto paint = [radius](const QString &id, QColor color) {
            return QVariantMap{
                {"key", "ink"},
                {"instance", id},
                {"x", 24.},
                {"y", 44.},
                {"color", QColor(Qt::transparent)},
                {"halos", QVariantList{QVariantMap{{"radius", radius}, {"color", color}}}}};
        };
        auto red = paint("left", Qt::red), blue = paint("right", Qt::blue);
        const auto capture = [&](QVariantList poses) {
            glyph.setPoses(poses);
            return scene.read().convertToFormat(QImage::Format_ARGB32_Premultiplied);
        };
        // The first pass prepares curve resources for the render target.
        capture({red});
        const auto whole = capture({red});
        QVERIFY2(!whole.isNull() && glyph.errorString().isEmpty(), qPrintable(glyph.errorString()));
        red["clip"] = QVariantMap{{"x", 18.5}, {"y", -20.}, {"width", 56.}, {"height", 100.}};
        blue["clip"] = QVariantMap{{"x", 50.5}, {"y", -20.}, {"width", 66.}, {"height", 100.}};
        const auto first = capture({red}), second = capture({blue});
        const auto combined = capture({red, blue});
        const auto reversed = capture({blue, red});
        for (const auto *image : {&first, &second, &combined, &reversed})
            QCOMPARE(image->size(), whole.size());
        const auto dump = qEnvironmentVariable("ECHO_GLYPH_PIXEL_DUMP");
        if (!dump.isEmpty()) {
            const QDir output(dump);
            QVERIFY(output.mkpath("."));
            const QString prefix = QString::fromLatin1(QTest::currentDataTag()) + "-";
            for (const auto &[name, image] :
                 std::initializer_list<std::pair<QString, QImage>>{{"full", whole},
                                                                   {"left", first},
                                                                   {"right", second},
                                                                   {"combined", combined},
                                                                   {"reversed", reversed}})
                QVERIFY(image.save(output.filePath(prefix + name + ".png")));
        }
        int covered = 0, overlapping = 0, maxError = 0, insideError = 0, outsideError = 0;
        for (int y = 0; y < whole.height(); ++y) {
            for (int x = 0; x < whole.width(); ++x) {
                const auto a = first.pixel(x, y), b = second.pixel(x, y);
                const auto both = combined.pixel(x, y), reverse = reversed.pixel(x, y);
                const auto local = glyph.mapFromScene(QPointF((x + .5) / ratio, (y + .5) / ratio));
                const double localX = local.x() - 24., localY = local.y() - 44.;
                if (localX > 20 && localX < 73 && localY > -18 && localY < 78) {
                    insideError = qMax(insideError, qAbs(qRed(whole.pixel(x, y)) - qRed(a)));
                    covered += qRed(a) > 1;
                }
                if (localX < 16 || localX > 77)
                    outsideError = qMax(outsideError, qRed(a));
                overlapping += qAlpha(a) > 2 && qAlpha(b) > 2;
                maxError =
                    std::max({maxError, qAbs(qBlue(both) - qBlue(b)),
                              qAbs(qRed(both) - qRound(qRed(a) * (1 - qAlpha(b) / 255.))),
                              qAbs(qRed(reverse) - qRed(a)),
                              qAbs(qBlue(reverse) - qRound(qBlue(b) * (1 - qAlpha(a) / 255.)))});
            }
        }
        QVERIFY2(covered > 100 && overlapping > 30, "Expected visible ink and overlapping halos");
        qInfo() << "Maximum channel differences inside/outside/overlap" << insideError
                << outsideError << maxError;
        QVERIFY(insideError <= 2 && outsideError <= 2 && maxError <= 2);
        QVERIFY(!scene.window.handle());
    }
};
int main(int argc, char **argv)
{
    QQuickWindow::setSceneGraphBackend("rhi");
    QGuiApplication app(argc, argv);
    GlyphRunCanvasPixelsTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "GlyphRunCanvasPixelsTest.moc"
