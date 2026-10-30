#include "GlyphRun.h"
#include "GlyphRunCanvas_p.h"
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QQuickWindow>
#include <QQuickRenderControl>
#include <QQuickRenderTarget>
#include <QtQuick/private/qquickshadereffectsource_p.h>
#include <rhi/qrhi.h>
#include <memory>
#include <vector>
#include <QtTest>
#include <cmath>
#include <limits>

using EchoMusic::GlyphRun;
using EchoMusic::GlyphCanvas::Memory;
namespace
{
struct Moments
{
    double mass = 0, x = 0, y = 0, sigmaX = 0, sigmaY = 0;
};
Moments moments(const QImage &image, qreal dpr)
{
    Moments m;
    double xx = 0, yy = 0;
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x) {
            const double v = qRed(image.pixel(x, y));
            m.mass += v;
            m.x += x * v;
            m.y += y * v;
            xx += x * x * v;
            yy += y * y * v;
        }
    if (m.mass) {
        m.x /= m.mass;
        m.y /= m.mass;
        m.sigmaX = std::sqrt(xx / m.mass - m.x * m.x) / dpr;
        m.sigmaY = std::sqrt(yy / m.mass - m.y * m.y) / dpr;
        m.x /= dpr;
        m.y /= dpr;
    }
    return m;
}
} // namespace
class GlyphRunCanvasRenderingTest final : public QObject
{
    Q_OBJECT
private slots:
    void haloProjectionCullingRemainsConservative()
    {
        using EchoMusic::GlyphCanvas::isHaloOutsideTarget;
        QMatrix4x4 identity;
        QVERIFY(isHaloOutsideTarget(QRectF(-2, -.5, .5, 1), identity));
        QVERIFY(!isHaloOutsideTarget(QRectF(-1.25, -.5, .5, 1), identity));
        QVERIFY(!isHaloOutsideTarget(QRectF(-1, -.5, .1, 1), identity));
        QMatrix4x4 negativeW;
        negativeW(3, 3) = -1;
        QVERIFY(!isHaloOutsideTarget(QRectF(-2, -.5, .5, 1), negativeW));
        negativeW(3, 3) = 0;
        QVERIFY(!isHaloOutsideTarget(QRectF(-2, -.5, .5, 1), negativeW));
        QMatrix4x4 crossingW;
        crossingW(3, 0) = 1;
        crossingW(3, 3) = 0;
        QVERIFY(!isHaloOutsideTarget(QRectF(-2, -.5, 3, 1), crossingW));
        QMatrix4x4 nonfinite;
        nonfinite(0, 0) = std::numeric_limits<float>::infinity();
        QVERIFY(!isHaloOutsideTarget(QRectF(-2, -.5, .5, 1), nonfinite));
        nonfinite(0, 0) = std::numeric_limits<float>::quiet_NaN();
        QVERIFY(!isHaloOutsideTarget(QRectF(-2, -.5, .5, 1), nonfinite));
        QMatrix4x4 nonfiniteDepth;
        nonfiniteDepth(2, 2) = std::numeric_limits<float>::infinity();
        QVERIFY(!isHaloOutsideTarget(QRectF(-2, -.5, .5, 1), nonfiniteDepth));
        QMatrix4x4 windowProjection, captureProjection;
        windowProjection.ortho(0, 256, 256, 0, -1, 1);
        captureProjection.ortho(-128, 128, 256, 0, -1, 1);
        const QRectF sameHalo(-100, 10, 20, 20);
        QVERIFY(isHaloOutsideTarget(sameHalo, windowProjection));
        QVERIFY(!isHaloOutsideTarget(sameHalo, captureProjection));
        QVERIFY(
            !isHaloOutsideTarget(QRectF(-200, 20, 40, 40),
                                 captureProjection * QMatrix4x4(QTransform::fromScale(.5, .5))));
    }
    void offscreenHalosRemainVisibleThroughNegativeSourceRect_data()
    {
        QTest::addColumn<int>("offscreenCount");
        QTest::newRow("single-control") << 1;
        QTest::newRow("capacity") << 196;
    }
    void offscreenHalosRemainVisibleThroughNegativeSourceRect()
    {
        QFETCH(int, offscreenCount);
        const auto baseline = Memory::total();
        QQuickWindow window;
        window.setColor(Qt::black);
        window.setMinimumSize({512, 256});
        window.setMaximumSize({512, 256});
        window.resize(512, 256);
        QQuickItem paper(window.contentItem());
        paper.setSize({256, 256});
        GlyphRun item(&paper);
        item.setSize({256, 256});
        QFont font("Arial");
        font.setPixelSize(96);
        item.setFont(font);
        item.setMaximumRasterScale(2);
        item.setHaloSampling("canvas");
        item.setRuns({QVariantMap{
            {"key", "w"}, {"text", "W"}, {"size", 96}, {"maxBlur", 0}, {"maxHaloRadius", 40}}});
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QTRY_VERIFY_WITH_TIMEOUT(!item.preparing(), 10000);
        QVERIFY2(item.errorString().isEmpty(), qPrintable(item.errorString()));
        QVariantList poses;
        for (int i = 0; i <= offscreenCount; ++i)
            poses.append(QVariantMap{
                {"key", "w"},
                {"instance", QString::number(i)},
                {"x", i == offscreenCount ? 32 : -7168 + (i % 14) * 512 + 32},
                {"y", i == offscreenCount ? 32 : (i / 14) * 512 + 32},
                {"scale", 2},
                {"color", QColor(Qt::white)},
                {"halos", QVariantList{QVariantMap{
                              {"radius", .5}, {"opacity", .7}, {"color", QColor(Qt::white)}}}}});
        item.setPoses(poses);
        window.grabWindow();
        QCoreApplication::processEvents();
        QVERIFY2(item.errorString().isEmpty(), qPrintable(item.errorString()));
        QTRY_VERIFY_WITH_TIMEOUT(item.ready(), 10000);
        QQuickShaderEffectSource capture(window.contentItem());
        QSignalSpy captureCompleted(&capture, &QQuickShaderEffectSource::scheduledUpdateCompleted);
        QSignalSpy swaps(&window, &QQuickWindow::frameSwapped);
        capture.setX(256);
        capture.setSize({256, 256});
        capture.setSourceRect({-7168, 0, 256, 256});
        const auto dpr = window.effectiveDevicePixelRatio();
        capture.setTextureSize({qRound(256 * dpr), qRound(256 * dpr)});
        capture.setSourceItem(&paper);
        capture.setHideSource(false);
        capture.setLive(true);
        const auto firstImage = window.grabWindow();
        QCoreApplication::processEvents();
        const auto captureDirectory =
            qEnvironmentVariable("ECHO_GLYPH_CAPTURE_DIR", QDir::tempPath() + "/echo-glyph-canvas");
        QVERIFY(QDir().mkpath(captureDirectory));
        QVERIFY(firstImage.save(captureDirectory +
                                QString("/negative-first-%1.png").arg(offscreenCount)));
        qInfo() << "CANVAS_CAPTURE_ORDER" << "first-grab" << offscreenCount
                << captureCompleted.count() << swaps.count() << capture.sourceRect() << "active"
                << window.isActive() << "applicationState" << QGuiApplication::applicationState();
        capture.scheduleUpdate();
        window.update();
        QTRY_VERIFY_WITH_TIMEOUT(captureCompleted.count() > 0, 10000);
        const int swap = swaps.count();
        qInfo() << "CANVAS_CAPTURE_ORDER" << "layer-completed" << offscreenCount
                << captureCompleted.count() << swap;
        window.update();
        QTRY_VERIFY_WITH_TIMEOUT(swaps.count() > swap, 10000);
        const auto image = window.grabWindow();
        QCoreApplication::processEvents();
        qInfo() << "CANVAS_CAPTURE_ORDER" << "fresh-swap-and-grab" << offscreenCount
                << captureCompleted.count() << swaps.count();
        QVERIFY(
            image.save(captureDirectory + QString("/negative-ready-%1.png").arg(offscreenCount)));
        QVERIFY2(item.errorString().isEmpty(), qPrintable(item.errorString()));
        QCOMPARE(image.size(), QSize(qRound(512 * dpr), qRound(256 * dpr)));
        qint64 mass = 0;
        int maxDiff = 0;
        for (int y = 0; y < qRound(256 * dpr); ++y)
            for (int x = 0; x < qRound(256 * dpr); ++x) {
                const auto a = image.pixelColor(x, y),
                           b = image.pixelColor(x + qRound(256 * dpr), y);
                mass += a.red();
                maxDiff =
                    std::max({maxDiff, std::abs(a.red() - b.red()), std::abs(a.green() - b.green()),
                              std::abs(a.blue() - b.blue())});
            }
        QVERIFY(mass > 0);
        QCOMPARE(maxDiff, 0);
        qInfo() << "CANVAS_NEGATIVE_CAPTURE" << dpr << poses.size() << "RGB max" << maxDiff;
        capture.setSourceItem(nullptr);
        item.setVisible(false);
        window.grabWindow();
        QCoreApplication::processEvents();
        QTRY_COMPARE_WITH_TIMEOUT(Memory::total(), baseline, 15000);
    }
    void haloSupportAcrossViewportEdgeIsPreserved()
    {
        const auto baseline = Memory::total();
        QQuickWindow window;
        window.setColor(Qt::black);
        window.setMinimumSize({512, 256});
        window.setMaximumSize({512, 256});
        window.resize(512, 256);
        GlyphRun item(window.contentItem());
        item.setSize({512, 256});
        QFont font("Arial");
        font.setPixelSize(96);
        item.setFont(font);
        item.setMaximumRasterScale(1);
        item.setHaloSampling("canvas");
        item.setRuns({QVariantMap{
            {"key", "w"}, {"text", "W"}, {"size", 96}, {"maxBlur", 0}, {"maxHaloRadius", 40}}});
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QTRY_VERIFY_WITH_TIMEOUT(!item.preparing(), 10000);
        QVERIFY2(item.errorString().isEmpty(), qPrintable(item.errorString()));
        item.setPoses({QVariantMap{
            {"key", "w"},
            {"x", 128},
            {"y", 80},
            {"color", QColor(Qt::white)},
            {"halos", QVariantList{QVariantMap{
                          {"radius", 40}, {"opacity", .7}, {"color", QColor(Qt::white)}}}}}});
        const auto reference = window.grabWindow();
        QCoreApplication::processEvents();
        QTRY_VERIFY_WITH_TIMEOUT(item.ready(), 10000);
        item.setX(-256);
        const auto moved = window.grabWindow();
        QCoreApplication::processEvents();
        QVERIFY2(item.errorString().isEmpty(), qPrintable(item.errorString()));
        const auto dpr = window.effectiveDevicePixelRatio();
        qint64 mass = 0;
        int maxDiff = 0;
        for (int y = 0; y < qRound(256 * dpr); ++y)
            for (int x = 0; x < qRound(256 * dpr); ++x) {
                const auto a = moved.pixelColor(x, y),
                           b = reference.pixelColor(x + qRound(256 * dpr), y);
                mass += a.red();
                maxDiff =
                    std::max({maxDiff, std::abs(a.red() - b.red()), std::abs(a.green() - b.green()),
                              std::abs(a.blue() - b.blue())});
            }
        QVERIFY(mass > 0);
        QVERIFY(maxDiff <= 1);
        qInfo() << "CANVAS_EDGE_SUPPORT" << dpr << "mass" << mass << "RGB max" << maxDiff;
        item.setVisible(false);
        window.grabWindow();
        QCoreApplication::processEvents();
        QTRY_COMPARE_WITH_TIMEOUT(Memory::total(), baseline, 15000);
    }

    void capturedAndMainPassesKeepTheSameSource()
    {
        const auto baseline = Memory::total();
        QQuickWindow window;
        window.setColor(Qt::black);
        window.setMinimumSize({1024, 256});
        window.setMaximumSize({1024, 256});
        window.resize(1024, 256);
        QQuickItem paper(window.contentItem());
        paper.setSize({256, 256});
        GlyphRun item(&paper);
        item.setSize({256, 256});
        GlyphRun second(&paper);
        second.setSize({256, 256});
        QFont font("Arial");
        font.setPixelSize(96);
        item.setFont(font);
        item.setMaximumRasterScale(1);
        item.setHaloSampling("canvas");
        item.setRuns({QVariantMap{
            {"key", "w"}, {"text", "W"}, {"size", 96}, {"maxBlur", 0}, {"maxHaloRadius", 40}}});
        QVariantList poses;
        for (int i = 0; i < 64; ++i)
            poses.append(QVariantMap{
                {"key", "w"},
                {"instance", QString::number(i)},
                {"x", double(i % 8) * 16},
                {"y", double(i / 8) * 16},
                {"color", QColor::fromRgbF(.7, .2, .4, .2)},
                {"halos", QVariantList{QVariantMap{
                              {"radius", .5}, {"opacity", .2}, {"color", QColor("#a3ff81")}}}}});
        second.setFont(font);
        second.setMaximumRasterScale(1);
        second.setHaloSampling("canvas");
        second.setRuns(item.runs());
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QTRY_VERIFY_WITH_TIMEOUT(!item.preparing() && !second.preparing(), 10000);
        QVERIFY2(item.errorString().isEmpty(), qPrintable(item.errorString()));
        item.setPoses(poses.mid(0, 32));
        second.setPoses(poses.mid(32));
        window.grabWindow();
        QCoreApplication::processEvents();
        QTRY_VERIFY_WITH_TIMEOUT(item.ready() && second.ready(), 10000);
        const qreal dpr = window.effectiveDevicePixelRatio();
        const auto ref = window.grabWindow().copy(0, 0, qRound(256 * dpr), qRound(256 * dpr));
        const auto uploads = item.textureUploads(), revision = item.maskRevision();
        std::vector<std::unique_ptr<QQuickShaderEffectSource>> sources;
        for (int i = 0; i < 3; ++i) {
            auto source = std::make_unique<QQuickShaderEffectSource>(window.contentItem());
            source->setX((i + 1) * 256);
            source->setSize({256, i == 2 ? 248. : 256.});
            source->setTextureSize({qRound(256 * dpr), qRound((i == 2 ? 248 : 256) * dpr)});
            source->setSourceRect(QRectF(i == 1 ? 8 : 0, i == 2 ? 8 : 0, 256, i == 2 ? 248 : 256));
            if (i == 2)
                source->setSamples(4);
            source->setSourceItem(&paper);
            source->setHideSource(false);
            source->setLive(true);
            sources.push_back(std::move(source));
        }
        QImage actual;
        for (int frame = 0; frame < 12; ++frame) {
            item.setX(frame % 2);
            second.setX(frame % 2);
            actual = window.grabWindow();
            QCoreApplication::processEvents();
            QTest::qWait(16);
            QVERIFY2(item.errorString().isEmpty(), qPrintable(item.errorString()));
            QVERIFY(item.ready() && second.ready());
            QVERIFY2(second.errorString().isEmpty(), qPrintable(second.errorString()));
            QCOMPARE(item.textureUploads(), uploads);
            QCOMPARE(item.maskRevision(), revision);
        }
        QVERIFY(!ref.isNull());
        QVERIFY(moments(ref, dpr).mass > 0);
        QCOMPARE(actual.size(), QSize(qRound(1024 * dpr), qRound(256 * dpr)));
        int maxDiff = 0;
        for (int copy = 0; copy < 4; ++copy)
            for (int y = 0; y < ref.height(); ++y)
                for (int x = 0; x < ref.width(); ++x) {
                    const int sx = x + (copy == 2 ? qRound(8 * dpr) : 0) - qRound(dpr),
                              sy = y + (copy == 3 ? qRound(8 * dpr) : 0);
                    const auto a = (sx >= 0 && sx < ref.width() && sy < ref.height())
                                       ? ref.pixelColor(sx, sy)
                                       : QColor(Qt::black),
                               b = actual.pixelColor(qRound(copy * 256 * dpr) + x, y);
                    maxDiff =
                        std::max({maxDiff, std::abs(a.red() - b.red()),
                                  std::abs(a.green() - b.green()), std::abs(a.blue() - b.blue())});
                }
        qInfo() << "MULTIPASS" << "DPR" << dpr << "RGB max" << maxDiff << "native"
                << Memory::total() << "peak" << Memory::highWater();
        QVERIFY(maxDiff <= 1);
        sources.clear();
        item.setVisible(false);
        second.setVisible(false);
        window.grabWindow();
        QCoreApplication::processEvents();
        QTRY_COMPARE_WITH_TIMEOUT(Memory::total(), baseline, 15000);
    }
    void renderControlKeepsEarlierRenderParametersUntilFrameEnd()
    {
        const auto baseline = Memory::total();
        QQuickRenderControl control;
        QQuickWindow window(&control);
        window.setColor(Qt::black);
        window.resize(256, 256);
        QVERIFY(control.initialize());
        auto *rhi = control.rhi();
        QVERIFY(rhi);
        struct Target
        {
            std::unique_ptr<QRhiTexture> texture;
            std::unique_ptr<QRhiRenderPassDescriptor> pass;
            std::unique_ptr<QRhiTextureRenderTarget> target;
        };
        const auto makeTarget = [rhi](QSize size) {
            auto t = std::make_unique<Target>();
            t->texture.reset(
                rhi->newTexture(QRhiTexture::RGBA8, size, 1,
                                QRhiTexture::RenderTarget | QRhiTexture::UsedAsTransferSource));
            if (!t->texture->create())
                return std::unique_ptr<Target>{};
            t->target.reset(
                rhi->newTextureRenderTarget(QRhiTextureRenderTargetDescription(t->texture.get())));
            t->pass.reset(t->target->newCompatibleRenderPassDescriptor());
            t->target->setRenderPassDescriptor(t->pass.get());
            if (!t->target->create())
                return std::unique_ptr<Target>{};
            return t;
        };
        auto first = makeTarget({256, 256}), second = makeTarget({512, 256});
        QVERIFY(first && second);
        const auto select = [&window](const Target &t) {
            auto target = QQuickRenderTarget::fromRhiRenderTarget(t.target.get());
            target.setDevicePixelRatio(1);
            window.setRenderTarget(target);
        };
        select(*first);
        // Prepare while windowless, then attach the accepted pure-pixel/path
        // result. This isolates the frame-lifetime regression from the existing
        // contract that hidden windows do not start new preparation jobs.
        GlyphRun item;
        item.setSize({256, 256});
        QFont font("Arial");
        font.setPixelSize(32);
        item.setFont(font);
        item.setMaximumRasterScale(1);
        item.setHaloSampling("canvas");
        item.setRuns(
            {QVariantMap{{"key", "same"}, {"text", "W"}, {"maxBlur", 0}, {"maxHaloRadius", 40}}});
        QTRY_VERIFY_WITH_TIMEOUT(!item.preparing(), 10000);
        QVERIFY2(item.errorString().isEmpty(), qPrintable(item.errorString()));
        QVERIFY(!item.metrics().isEmpty());
        item.setPoses(
            {QVariantMap{{"key", "same"},
                         {"instance", "first"},
                         {"x", 64},
                         {"y", 100},
                         {"halos",
                          QVariantList{QVariantMap{{"radius", .5}, {"color", QColor(Qt::white)}}}}},
             QVariantMap{{"key", "same"},
                         {"instance", "second"},
                         {"x", 320},
                         {"y", 100},
                         {"halos", QVariantList{QVariantMap{{"radius", .5},
                                                            {"color", QColor(Qt::white)}}}}}});
        item.setParentItem(window.contentItem());
        QCOMPARE(window.effectiveDevicePixelRatio(), 1.);
        bool firstDone = false, secondDone = false;
        QRhiReadbackResult a, b;
        a.completed = [&] { firstDone = true; };
        b.completed = [&] { secondDone = true; };
        control.polishItems();
        control.beginFrame();
        control.sync();
        control.render();
        auto *updates = rhi->nextResourceUpdateBatch();
        updates->readBackTexture(QRhiReadbackDescription(first->texture.get()), &a);
        control.commandBuffer()->resourceUpdate(updates);
        // A second scene render in the SAME QRhi frame changes the projection.
        // beforeRendering fires again; beforeFrameBegin must not fire again.
        window.resize(512, 256);
        select(*second);
        control.polishItems();
        control.sync();
        control.render();
        updates = rhi->nextResourceUpdateBatch();
        updates->readBackTexture(QRhiReadbackDescription(second->texture.get()), &b);
        control.commandBuffer()->resourceUpdate(updates);
        control.endFrame();
        QTRY_VERIFY_WITH_TIMEOUT(firstDone && secondDone, 10000);
        QVERIFY2(item.errorString().isEmpty(), qPrintable(item.errorString()));
        QCOMPARE(a.pixelSize, QSize(256, 256));
        QCOMPARE(b.pixelSize, QSize(512, 256));
        const QImage imageA(reinterpret_cast<const uchar *>(a.data.constData()), 256, 256,
                            QImage::Format_RGBA8888_Premultiplied);
        const QImage imageB(reinterpret_cast<const uchar *>(b.data.constData()), 512, 256,
                            QImage::Format_RGBA8888_Premultiplied);
        QVERIFY(moments(imageA, 1).mass > 0);
        QVERIFY(moments(imageB.copy(256, 0, 256, 256), 1).mass > 0);
        int maxDiff = 0;
        for (int y = 0; y < 256; ++y)
            for (int x = 0; x < 256; ++x) {
                const auto p = imageA.pixelColor(x, y), q = imageB.pixelColor(x, y),
                           r = imageB.pixelColor(x + 256, y);
                maxDiff =
                    std::max({maxDiff, std::abs(p.red() - q.red()), std::abs(p.green() - q.green()),
                              std::abs(p.blue() - q.blue()), std::abs(p.red() - r.red()),
                              std::abs(p.green() - r.green()), std::abs(p.blue() - r.blue())});
            }
        qInfo() << "CANVAS_RENDER_CONTROL" << "renders in one frame" << 2 << "RGB max" << maxDiff;
        QVERIFY(maxDiff <= 1);
        item.setVisible(false);
        control.beginFrame();
        control.sync();
        control.render();
        control.endFrame();
        QCoreApplication::processEvents();
        QTRY_COMPARE_WITH_TIMEOUT(Memory::total(), baseline, 15000);
        control.invalidate();
    }
    void canvasFontStringsAreRejectedBeforeSerialization()
    {
        const auto baseline = Memory::total();
        GlyphRun item;
        QFont font("Arial");
        font.setPixelSize(16);
        item.setFont(font);
        item.setMaximumRasterScale(1);
        item.setHaloSampling("canvas");
        item.setRuns({QVariantMap{{"key", "accepted"}, {"text", "Preserved"}, {"maxBlur", 0}}});
        QTRY_VERIFY_WITH_TIMEOUT(!item.preparing(), 10000);
        QVERIFY2(item.errorString().isEmpty(), qPrintable(item.errorString()));
        const auto acceptedFont = item.font();
        const auto runs = item.runs(), metrics = item.metrics();
        const auto revision = item.maskRevision();
        const auto bytes = item.cacheBytes(), nativeBytes = Memory::total();
        QSignalSpy preparations(&item, &GlyphRun::preparationChanged);
        font.setStyleName(QString(65537, QChar('A')));
        item.setFont(font);
        QVERIFY2(!item.errorString().isEmpty(),
                 "styleName bypassed the complete font string budget");
        QVERIFY(item.errorString().contains("serialization budget"));
        QVERIFY(!item.preparing());
        QCOMPARE(preparations.count(), 0);
        QCOMPARE(item.font(), acceptedFont);
        QCOMPARE(item.runs(), runs);
        QCOMPARE(item.metrics(), metrics);
        QCOMPARE(item.maskRevision(), revision);
        QCOMPARE(item.cacheBytes(), bytes);
        QCOMPARE(Memory::total(), nativeBytes);
        item.setVisible(false);
        QTRY_COMPARE_WITH_TIMEOUT(Memory::total(), baseline, 15000);
    }
    void parentScaleIsRejectedBeforeBlurPlanning()
    {
        const auto baseline = Memory::total();
        QQuickWindow window;
        window.setMinimumSize({512, 512});
        window.setMaximumSize({512, 512});
        window.resize(512, 512);
        window.setColor(Qt::black);
        QQuickItem parent(window.contentItem());
        parent.setSize({512, 512});
        GlyphRun item(&parent);
        item.setSize({512, 512});
        QFont font("Arial");
        font.setPixelSize(32);
        item.setFont(font);
        item.setMaximumRasterScale(1);
        item.setHaloSampling("canvas");
        item.setRuns({QVariantMap{
            {"key", "bounded"}, {"text", "M"}, {"maxBlur", 0}, {"maxHaloRadius", 160}}});
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QTRY_VERIFY_WITH_TIMEOUT(!item.preparing(), 10000);
        QVERIFY2(item.errorString().isEmpty(), qPrintable(item.errorString()));
        item.setPoses({QVariantMap{{"key", "bounded"},
                                   {"halos", QVariantList{QVariantMap{{"radius", 128}}}}}});
        const auto initial = window.grabWindow();
        QCoreApplication::processEvents();
        QVERIFY2(item.errorString().isEmpty(), qPrintable(item.errorString()));
        QVERIFY(moments(initial, window.effectiveDevicePixelRatio()).mass > 0);
        parent.setScale(1e12);
        window.grabWindow();
        QCoreApplication::processEvents();
        QTRY_VERIFY_WITH_TIMEOUT(!item.errorString().isEmpty(), 10000);
        QVERIFY(item.errorString().contains("maximumRasterScale"));
        QVERIFY(!item.ready());
        QCOMPARE(item.cacheBytes(), 0);
        item.setVisible(false);
        window.grabWindow();
        QCoreApplication::processEvents();
        QTRY_COMPARE_WITH_TIMEOUT(Memory::total(), baseline, 15000);
    }
    void renderErrorNotificationCanStartARecoveryGeneration()
    {
        const auto baseline = Memory::total();
        QQuickWindow window;
        window.setMinimumSize({512, 512});
        window.setMaximumSize({512, 512});
        window.resize(512, 512);
        window.setColor(Qt::black);
        GlyphRun item(window.contentItem());
        item.setSize({512, 512});
        QFont font("Arial");
        font.setPixelSize(32);
        item.setFont(font);
        item.setMaximumRasterScale(1);
        item.setHaloSampling("canvas");
        item.setRuns({QVariantMap{{"key", "A"}, {"text", "Initial"}, {"maxBlur", 0}}});
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QTRY_VERIFY_WITH_TIMEOUT(!item.preparing(), 10000);
        QVERIFY2(item.errorString().isEmpty(), qPrintable(item.errorString()));
        QTRY_VERIFY_WITH_TIMEOUT(item.ready(), 10000);
        const auto initial = window.grabWindow();
        QCOMPARE(initial.size(), QSize(qRound(512 * window.effectiveDevicePixelRatio()),
                                       qRound(512 * window.effectiveDevicePixelRatio())));
        QVERIFY(moments(initial, window.effectiveDevicePixelRatio()).mass > 0);
        bool requestedRecovery = false;
        connect(&item, &GlyphRun::errorChanged, &item, [&] {
            if (!requestedRecovery && item.errorString().contains("maximumRasterScale")) {
                requestedRecovery = true;
                item.setRuns({QVariantMap{{"key", "B"}, {"text", "Recovered B"}, {"maxBlur", 0}}});
            }
        });
        item.setPoses({QVariantMap{{"key", "A"}, {"scale", 2}}});
        window.grabWindow();
        QCoreApplication::processEvents();
        QTRY_VERIFY_WITH_TIMEOUT(requestedRecovery, 10000);
        // The old error callback must not cancel B after its synchronous
        // notification receiver has accepted a complete newer request.
        QVERIFY(item.preparing() ||
                (!item.metrics().isEmpty() &&
                 item.metrics().first().toMap().value("key").toString() == "B"));
        QTRY_VERIFY_WITH_TIMEOUT(!item.preparing(), 10000);
        QVERIFY2(item.errorString().isEmpty(), qPrintable(item.errorString()));
        QCOMPARE(item.runs().first().toMap().value("key").toString(), QString("B"));
        QCOMPARE(item.metrics().first().toMap().value("key").toString(), QString("B"));
        window.grabWindow();
        QCoreApplication::processEvents();
        QTRY_VERIFY_WITH_TIMEOUT(item.ready(), 10000);
        item.setVisible(false);
        window.grabWindow();
        QCoreApplication::processEvents();
        QTRY_COMPARE_WITH_TIMEOUT(Memory::total(), baseline, 15000);
    }
    void windowPoolReusesRetiredInkAndRecoversAfterErrors()
    {
#ifndef Q_OS_MACOS
        QSKIP("This native rendering fixture uses the Cocoa font stack");
#endif
        const auto baseline = Memory::total();
        QQuickWindow window;
        window.setMinimumSize({1024, 1024});
        window.setMaximumSize({1024, 1024});
        window.resize(1024, 1024);
        window.setColor(Qt::black);
        GlyphRun pin(window.contentItem()), item(window.contentItem());
        QFont font;
        font.setFamilies({"Arial", "PingFang SC"});
        font.setPixelSize(32);
        for (auto *glyph : {&pin, &item}) {
            glyph->setSize({1024, 1024});
            glyph->setFont(font);
            glyph->setMaximumRasterScale(1);
            glyph->setHaloSampling("canvas");
        }
        pin.setRuns({QVariantMap{{"key", "pinned"}, {"text", "@"}, {"maxBlur", 0}}});
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QTRY_VERIFY_WITH_TIMEOUT(!pin.preparing(), 10000);
        QVERIFY2(pin.errorString().isEmpty(), qPrintable(pin.errorString()));
        QTRY_VERIFY_WITH_TIMEOUT(pin.ready(), 10000);
        const auto uploads = pin.textureUploads();
        const auto pool = pin.sharedCacheKey();
        const auto text = QString::fromUtf8("春江花月夜山河星海风云天地日光");
        int rejectedPreparations = 0;
        for (int generation = 0; generation < 40; ++generation) {
            const QVariantList requested{QVariantMap{{"key", "changing"},
                                                     {"text", text},
                                                     {"size", 128 + (generation % 17) * 4},
                                                     {"maxBlur", 0}}};
            const auto requestedFont = item.font();
            const auto requestedPoses = item.poses();
            item.setRuns(requested);
            QTRY_VERIFY_WITH_TIMEOUT(!item.preparing(), 10000);
            if (item.errorString() == "Canvas ink exceeds the shared native budget") {
                ++rejectedPreparations;
                QCOMPARE(item.runs(), requested);
                QCOMPARE(item.font(), requestedFont);
                QCOMPARE(item.poses(), requestedPoses);
                QCOMPARE(item.cacheBytes(), 0);
                QVERIFY(!item.ready());
                QVERIFY(Memory::highWater() <= EchoMusic::GlyphCanvas::NativeBudget);
                window.grabWindow();
                QCoreApplication::processEvents();
                const auto pinnedBytes =
                    pin.cacheBytes() + pin.gpuCacheBytes() + pin.sharedCacheBytes();
                QTRY_COMPARE_WITH_TIMEOUT(Memory::total(), baseline + pinnedBytes, 10000);
                qInfo() << "CANVAS_POOL_BOUNDED_REJECTION" << generation << "drained"
                        << Memory::total() << "peak" << Memory::highWater();
                // One explicit recovery attempt, with the exact same complete
                // input after the retired generation has actually drained.
                item.setRuns(requested);
                QTRY_VERIFY_WITH_TIMEOUT(!item.preparing(), 10000);
                QCOMPARE(item.runs(), requested);
                QCOMPARE(item.font(), requestedFont);
                QCOMPARE(item.poses(), requestedPoses);
            }
            QVERIFY2(item.errorString().isEmpty(), qPrintable(item.errorString()));
            const auto image = window.grabWindow();
            QCoreApplication::processEvents();
            QTRY_VERIFY_WITH_TIMEOUT(item.ready(), 10000);
            QCOMPARE(image.size(), QSize(qRound(1024 * window.effectiveDevicePixelRatio()),
                                         qRound(1024 * window.effectiveDevicePixelRatio())));
            QCOMPARE(item.sharedCacheKey(), pool);
            QCOMPARE(pin.textureUploads(), uploads);
            QVERIFY(Memory::total() <= EchoMusic::GlyphCanvas::NativeBudget);
        }
        // The pin keeps the same pool alive throughout all replacements. A
        // historical strong-reference atlas would eventually fill eight pages.
        item.setRuns(
            {QVariantMap{{"key", "too-large"}, {"text", "W"}, {"size", 2048}, {"maxBlur", 0}}});
        QTRY_VERIFY_WITH_TIMEOUT(!item.preparing(), 10000);
        QVERIFY(!item.errorString().isEmpty());
        QCOMPARE(item.cacheBytes(), 0);
        QVERIFY(!item.ready());
        const QVariantList valid{
            QVariantMap{{"key", "recovered"}, {"text", "Recovered"}, {"size", 32}, {"maxBlur", 0}}};
        item.setRuns(valid);
        QTRY_VERIFY_WITH_TIMEOUT(!item.preparing(), 10000);
        QVERIFY2(item.errorString().isEmpty(), qPrintable(item.errorString()));
        window.grabWindow();
        QCoreApplication::processEvents();
        QTRY_VERIFY_WITH_TIMEOUT(item.ready(), 10000);
        // This is a render-time rejection, not a CPU fixture substitute.
        item.setPoses({QVariantMap{{"key", "recovered"}, {"scale", 2}}});
        window.grabWindow();
        QCoreApplication::processEvents();
        QTRY_VERIFY_WITH_TIMEOUT(!item.errorString().isEmpty(), 10000);
        QVERIFY(item.errorString().contains("maximumRasterScale"));
        QCOMPARE(item.cacheBytes(), 0);
        QVERIFY(item.metrics().isEmpty());
        QVERIFY(!item.ready());
        item.setPoses({});
        item.setRuns(valid);
        QTRY_VERIFY_WITH_TIMEOUT(!item.preparing(), 10000);
        QVERIFY2(item.errorString().isEmpty(), qPrintable(item.errorString()));
        window.grabWindow();
        QCoreApplication::processEvents();
        QTRY_VERIFY_WITH_TIMEOUT(item.ready(), 10000);
        QCOMPARE(item.sharedCacheKey(), pool);
        QCOMPARE(pin.textureUploads(), uploads);
        item.setVisible(false);
        pin.setVisible(false);
        window.grabWindow();
        QCoreApplication::processEvents();
        QTRY_COMPARE_WITH_TIMEOUT(Memory::total(), baseline, 15000);
        qInfo() << "CANVAS_POOL_REUSE" << "generations" << 40 << "boundedRejections"
                << rejectedPreparations << "retired" << Memory::total() << "peak"
                << Memory::highWater();
    }
    void sourceMatrixKeepsBothMomentsAndStableResources()
    {
#ifndef Q_OS_MACOS
        QSKIP("This pinned Chrome Graphite reference uses the Cocoa font stack");
#endif
        QFile file(QFINDTESTDATA("../fixtures/glyph-run/folia-default-canvas-matrix.json"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto fixture = QJsonDocument::fromJson(file.readAll()).object();
        const auto baselineBytes = Memory::total();
        QQuickWindow window;
        window.setColor(Qt::black);
        window.setMinimumSize({1024, 1024});
        window.setMaximumSize({1024, 1024});
        window.resize(1024, 1024);
        GlyphRun item(window.contentItem());
        item.setSize({1024, 1024});
        QFont font;
        QStringList families;
        for (const auto &family : fixture["font"].toObject()["families"].toArray())
            families.append(family.toString());
        font.setFamilies(families);
        font.setPixelSize(152);
        font.setWeight(QFont::Weight(780));
        item.setFont(font);
        item.setMaximumRasterScale(2.24);
        item.setHaloSampling("canvas");
        item.setRuns({QVariantMap{{"key", "source"},
                                  {"text", fixture["text"].toString()},
                                  {"size", fixture["font"].toObject()["size"].toDouble()},
                                  {"maxBlur", 0},
                                  {"maxHaloRadius", 1024}}});
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QTRY_VERIFY_WITH_TIMEOUT(!item.preparing(), 10000);
        QVERIFY2(item.errorString().isEmpty(), qPrintable(item.errorString()));
        QTRY_VERIFY_WITH_TIMEOUT(item.ready(), 10000);
        const auto dpr = window.effectiveDevicePixelRatio();
        QVERIFY(dpr == 1 || dpr == 2);
        const auto metric = item.metrics().first().toMap();
        const double baseline = metric["baseline"].toDouble(),
                     middle = (metric["emAscent"].toDouble() - metric["emDescent"].toDouble()) / 2;
        const auto revision = item.maskRevision(), uploads = item.textureUploads();
        const auto gates = fixture["gates"].toObject();
        const auto captureDir =
            qEnvironmentVariable("ECHO_GLYPH_CAPTURE_DIR", QDir::tempPath() + "/echo-glyph-canvas");
        QVERIFY(QDir().mkpath(captureDir));
        int count = 0, failures = 0;
        for (const auto &entry : fixture["groups"].toArray()) {
            const auto group = entry.toObject();
            if (group["dpr"].toDouble() != dpr)
                continue;
            QVERIFY(group["contextOptions"].toString() == "omitted");
            int ordinal = 0;
            for (const auto &poseEntry : group["poses"].toArray()) {
                const auto pose = poseEntry.toObject();
                const double zoom = pose["zoom"].toDouble(), alpha = pose["alpha"].toDouble(),
                             blur = pose["blur"].toDouble();
                QVariantList halos;
                if (blur > 0)
                    halos.append(QVariantMap{{"radius", blur / (2 * zoom * dpr)},
                                             {"opacity", alpha * pose["shadowAlpha"].toDouble()}});
                item.setPoses({QVariantMap{{"key", "source"},
                                           {"x", 512.},
                                           {"y", 512 - baseline + zoom * middle},
                                           {"scale", zoom},
                                           {"color", QColor::fromRgbF(1, 1, 1, alpha)},
                                           {"halos", halos}}});
                QVERIFY2(item.errorString().isEmpty(), qPrintable(item.errorString()));
                const auto image = window.grabWindow();
                QCoreApplication::processEvents();
                QCOMPARE(image.size(), QSize(qRound(1024 * dpr), qRound(1024 * dpr)));
                QVERIFY2(item.errorString().isEmpty(), qPrintable(item.errorString()));
                const auto actual = moments(image, dpr);
                const auto expected = pose["moments"].toObject();
                const auto mass = expected["mass"].toDouble();
                const double energy =
                    mass ? std::abs(actual.mass / mass - 1) : (actual.mass ? 1 : 0);
                const double sx = mass ? std::abs(actual.sigmaX / expected["sigma"].toDouble() - 1)
                                       : 0,
                             sy = mass ? std::abs(actual.sigmaY / expected["sigmaY"].toDouble() - 1)
                                       : 0;
                const double cx = mass ? std::abs(actual.x - expected["centroidX"].toDouble()) : 0,
                             cy = mass ? std::abs(actual.y - expected["centroidY"].toDouble()) : 0;
                const bool failed = energy >= gates["relativeMass"].toDouble() ||
                                    sx >= gates["relativeSigmaX"].toDouble() ||
                                    sy >= gates["relativeSigmaY"].toDouble() ||
                                    std::max(cx, cy) > gates["centroidLogicalPixels"].toDouble();
                if (failed) {
                    ++failures;
                    const auto path = QString("%1/%2-dpr%3-frame%4.png")
                                          .arg(captureDir, group["kind"].toString())
                                          .arg(dpr)
                                          .arg(ordinal);
                    QVERIFY(image.save(path));
                    qWarning() << "Canvas source mismatch" << group["kind"] << dpr << ordinal
                               << energy << sx << sy << cx << cy << path;
                }
                QCOMPARE(item.maskRevision(), revision);
                QCOMPARE(item.textureUploads(), uploads);
                QVERIFY(item.cacheBytes() + item.gpuCacheBytes() + item.sharedCacheBytes() <=
                        EchoMusic::GlyphCanvas::NativeBudget);
                ++count;
                ++ordinal;
            }
        }
        QCOMPARE(count, 264);
        QCOMPARE(failures, 0);
        item.setVisible(false);
        window.grabWindow();
        QCoreApplication::processEvents();
        QTRY_COMPARE_WITH_TIMEOUT(Memory::total(), baselineBytes, 15000);
        qInfo() << "CANVAS_SOURCE_MATRIX" << "dpr" << dpr << "frames" << count << "uploads"
                << uploads << "retired" << Memory::total() << "peak" << Memory::highWater();
        if (!qEnvironmentVariableIsSet("ECHO_GLYPH_CANVAS_CHILD")) {
            QProcess child;
            auto environment = QProcessEnvironment::systemEnvironment();
            environment.insert("QT_SCALE_FACTOR", dpr == 1 ? "2" : "1");
            environment.insert("ECHO_GLYPH_CANVAS_CHILD", "1");
            child.setProcessEnvironment(environment);
            child.start(QCoreApplication::applicationFilePath(),
                        {"sourceMatrixKeepsBothMomentsAndStableResources"});
            QVERIFY(child.waitForFinished(120000));
            const auto output = child.readAllStandardOutput() + child.readAllStandardError();
            qInfo().noquote() << output;
            QVERIFY2(child.exitStatus() == QProcess::NormalExit && child.exitCode() == 0,
                     output.constData());
        }
    }
};
int main(int argc, char **argv)
{
    QGuiApplication application(argc, argv);
    GlyphRunCanvasRenderingTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "GlyphRunCanvasRenderingTest.moc"
