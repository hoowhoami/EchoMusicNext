#include "GlyphRun.h"
#include <QFontInfo>
#include <QBuffer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QUrl>
#include <QGuiApplication>
#include <QPainter>
#include <QPainterPathStroker>
#include <QFontDatabase>
#include <QPointer>
#include <QProcess>
#include <QProcessEnvironment>
#include <QQuickWindow>
#include <QQuickItemGrabResult>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QTextBoundaryFinder>
#include <QTextLayout>
#include <QtEndian>
#include <QtTest>
#include <limits>
#include <memory>
#include <cmath>
using namespace EchoMusic;
static QVariantList oneRun(const QString &text);

class GlyphRunTest : public QObject
{
    Q_OBJECT
private slots:
    void shapesWholeRunsWithTheCompleteFontChain();
    void metricsKeepGraphemesAndLigaturesIntact();
    void renderedInkMatchesContextualShaping();
    void blurTwentyPixelsExceedsTheSmallFontRadius();
    void largeIndependentHalosKeepTheirRadiusAndFrameCache();
    void rejectsInputsAtomically();
    void framePosesKeepMasksAndTextures();
    void hiddenAndDestroyedReleaseResources();
    void destructionClearsShaderEffectSourceReferences();
    void retinaLongLinesKeepClearInkAndBoundedEffects();
    void metricsReportActualRunInkRelativeToTheBaseline();
    void paragraphsUseNativeWrapBidiAndBaselineOffsets();
    void paragraphRenderingAndClusterPosesShareNativeLineCoordinates();
    void colorGlyphsKeepNativeRgbaAndOrdinaryFallbackTint();
    void largeHaloBudgetsKeepEnergyMomentsAndFrameCache();
    void measurementOnlyHasNoMasksAndTransitionsAtomically();
    void clippedInstancesKeepContextAndOrderWithoutUploads();
    void instanceAndClipRejectionsKeepTheAcceptedFrame();
    void sharedArabicClusterSupportsTwoClippedInstances();
    void syntheticBoldMatchesNativeTextLayout();
    void haloKernelMatchesCanvasForIdenticalInk();
    void strokeOnlyUsesCompleteNativeOutlines();
    void largeVisibleFontsKeepFullDprAndAtomicBudgets();
    void sourceSonnetDecorationKeepsTheExistingInkBudget();
    void automaticEnvironmentRebuildKeepsExplicitRejectionErrors();
    void screenCompositionMatchesPremultipliedOverlapWithoutUploads();
    void metricsDistinguishAdvanceFromFullContextInkBounds();
    void bitmapStrokeFallbackKeepsRgbaAndOutlineContext();
    void emMetricsUseThePrimaryFontTypographicRatio();
    void poseOpacityAppliesAfterEachFragmentComposite();
    void fractionalSizesKeepWholeContextAndFallbackFonts();
    void textureTintMultipliesRgbaInkAndHalosWithoutUploads();
};

void GlyphRunTest::destructionClearsShaderEffectSourceReferences()
{
    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.setData("import QtQuick\nShaderEffectSource { width: 64; height: 64 }",
                      QUrl("qrc:/glyph-destruction-test.qml"));
    auto *glyph = new GlyphRun;
    std::unique_ptr<QObject> capture(component.createWithInitialProperties(
        {{"sourceItem", QVariant::fromValue<QQuickItem *>(glyph)}}));
    QVERIFY2(capture, qPrintable(component.errorString()));
    QCOMPARE(capture->property("sourceItem").value<QQuickItem *>(), glyph);
    QSignalSpy destroyed(glyph, &QObject::destroyed);
    delete glyph;
    QCOMPARE(destroyed.count(), 1);
    QVERIFY(!capture->property("sourceItem").value<QQuickItem *>());
    // The source listener must also be safe when the consumer retires later.
    capture.reset();
}

void GlyphRunTest::textureTintMultipliesRgbaInkAndHalosWithoutUploads()
{
    QQuickWindow window;
    window.setGeometry(0, 0, 320, 260);
    window.setColor(Qt::black);
    GlyphRun item(window.contentItem());
    item.setSize({320, 260});
    QFont font;
    font.setFamilies({"Arial", "Apple Color Emoji"});
    font.setPixelSize(96);
    item.setFont(font);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    for (const auto &text : {QString("M"), QString::fromUtf8("😀")}) {
        item.setRuns({QVariantMap{
            {"key", "tinted"}, {"text", text}, {"maxBlur", 0}, {"maxHaloRadius", 40}}});
        QVERIFY2(item.errorString().isEmpty(), qPrintable(item.errorString()));
        QVariantMap pose{{"key", "tinted"},
                         {"x", 110},
                         {"y", 65},
                         {"opacity", .6},
                         {"color", QColor(255, 255, 255, 210)},
                         {"halos", QVariantList{QVariantMap{
                                       {"radius", 18}, {"color", QColor(255, 80, 140, 230)}}}}};
        item.setPoses({pose});
        QTRY_VERIFY(item.textureUploads() > 0);
        const auto opaque = window.grabWindow();
        const auto revision = item.maskRevision(), uploads = item.textureUploads();
        // Explicit white tint, including alpha zero, is the omitted default.
        pose["tint"] = QColor(255, 255, 255, 0);
        item.setPoses({pose});
        QCOMPARE(window.grabWindow(), opaque);
        const QColor tint(70, 210, 120, 20);
        pose["tint"] = tint;
        item.setPoses({pose});
        const auto tinted = window.grabWindow();
        int maximumError = 0;
        qint64 energy = 0;
        for (int y = 0; y < opaque.height(); ++y)
            for (int x = 0; x < opaque.width(); ++x) {
                const auto a = opaque.pixel(x, y), b = tinted.pixel(x, y);
                energy += qRed(a) + qGreen(a) + qBlue(a);
                maximumError =
                    std::max({maximumError, std::abs(qRed(b) - qRound(qRed(a) * tint.redF())),
                              std::abs(qGreen(b) - qRound(qGreen(a) * tint.greenF())),
                              std::abs(qBlue(b) - qRound(qBlue(a) * tint.blueF()))});
            }
        QVERIFY(energy > 10000);
        QVERIFY2(maximumError <= 2,
                 qPrintable(QString("Post-composite tint error %1").arg(maximumError)));
        for (int frame = 0; frame < 120; ++frame) {
            pose["tint"] = QColor(frame * 2, 200, 100, frame);
            item.setPoses({pose});
        }
        QCOMPARE(item.maskRevision(), revision);
        QCOMPARE(item.textureUploads(), uploads);
        const auto accepted = item.poses();
        pose["tint"] = "not-a-colour";
        item.setPoses({pose});
        QVERIFY(!item.errorString().isEmpty());
        QCOMPARE(item.poses(), accepted);
    }
    item.setVisible(false);
    QCOMPARE(item.cacheBytes(), 0);
}

void GlyphRunTest::fractionalSizesKeepWholeContextAndFallbackFonts()
{
    QQuickWindow window;
    window.setGeometry(0, 0, 640, 260);
    window.setColor(Qt::black);
    GlyphRun item(window.contentItem());
    item.setSize({640, 260});
    QFont font;
    font.setFamilies({"Times New Roman", "PingFang SC", "Apple Color Emoji"});
    font.setPixelSize(64);
    item.setFont(font);
    item.setMeasurementOnly(true);
    const QString text = QString::fromUtf8("ffi لا 星👩‍🚀");
    qreal previous = 0;
    for (const qreal size : {.125, .8969064634404105, 1.25, 12.4, 80.5}) {
        const QVariantList runs{
            QVariantMap{{"key", "fractional"}, {"text", text}, {"size", size}, {"maxBlur", 0}}};
        item.setRuns(runs);
        QVERIFY2(item.errorString().isEmpty(), qPrintable(item.errorString()));
        QCOMPARE(item.runs(), runs);
        const auto metric = item.metrics().first().toMap();
        QFont reference = font;
        const int referenceSize = std::max(64, qCeil(size));
        reference.setPixelSize(referenceSize);
        QTextLayout layout(text, reference);
        layout.beginLayout();
        auto line = layout.createLine();
        line.setLineWidth(1000000);
        layout.endLayout();
        const qreal scale = size / referenceSize;
        QCOMPARE(metric["advance"].toDouble(), line.horizontalAdvance() * scale);
        QVERIFY(metric["advance"].toDouble() > previous);
        previous = metric["advance"].toDouble();
        QCOMPARE(item.cacheBytes(), 0);
        const auto graphemes = metric["graphemes"].toList();
        for (const auto &value : graphemes) {
            const auto grapheme = value.toMap();
            QCOMPARE(grapheme["x"].toDouble(), line.cursorToX(grapheme["start"].toInt()) * scale);
        }
        // Actual rendering uses the selected complete-context glyphs at their real
        // fractional QRawFont size, rather than a rounded or enlarged bitmap.
        item.setMeasurementOnly(false);
        item.setPoses({QVariantMap{{"key", "fractional"}, {"x", 30}, {"y", 40}}});
        if (!window.isVisible()) {
            window.show();
            QVERIFY(QTest::qWaitForWindowExposed(&window));
        }
        QTRY_VERIFY(item.textureUploads() > 0);
        const auto actual = window.grabWindow();
        QImage expected(actual.size(), QImage::Format_RGB32);
        expected.setDevicePixelRatio(window.devicePixelRatio());
        expected.fill(Qt::black);
        {
            QPainter painter(&expected);
            painter.setPen(Qt::white);
            QList<int> clusters;
            for (const auto &value : graphemes) {
                const auto start = value.toMap()["clusterStart"].toInt();
                if (clusters.isEmpty() || clusters.last() != start)
                    clusters.append(start);
            }
            clusters.append(text.size());
            for (int cluster = 0; cluster + 1 < clusters.size(); ++cluster)
                for (auto run : layout.glyphRuns(clusters[cluster],
                                                 clusters[cluster + 1] - clusters[cluster])) {
                    auto raw = run.rawFont();
                    raw.setPixelSize(raw.pixelSize() * scale);
                    run.setRawFont(raw);
                    auto positions = run.positions();
                    for (auto &position : positions)
                        position *= scale;
                    run.setPositions(positions);
                    painter.drawGlyphRun({30, 40}, run);
                }
        }
        qint64 error = 0, energy = 0;
        int maximumChannelError = 0;
        for (int y = 0; y < actual.height(); ++y)
            for (int x = 0; x < actual.width(); ++x) {
                const auto a = actual.pixel(x, y), e = expected.pixel(x, y);
                error += std::abs(qRed(a) - qRed(e)) + std::abs(qGreen(a) - qGreen(e)) +
                         std::abs(qBlue(a) - qBlue(e));
                maximumChannelError =
                    std::max({maximumChannelError, std::abs(qRed(a) - qRed(e)),
                              std::abs(qGreen(a) - qGreen(e)), std::abs(qBlue(a) - qBlue(e))});
                energy += qRed(e) + qGreen(e) + qBlue(e);
            }
        qInfo() << "Fractional native RGB" << size << energy << maximumChannelError
                << double(error) / std::max(qint64(1), energy);
        QVERIFY2(maximumChannelError <= 2 && (energy <= 1000 || double(error) / energy < .015),
                 qPrintable(QString("Fractional native glyph RGB error %1 at %2px")
                                .arg(double(error) / energy)
                                .arg(size)));
        item.setPoses({});
        item.setMeasurementOnly(true);
    }
    const auto accepted = item.runs(), metrics = item.metrics();
    const auto revision = item.maskRevision();
    for (qreal size : {0., .001, -.5, std::numeric_limits<qreal>::infinity()}) {
        item.setRuns({QVariantMap{{"key", "bad"}, {"text", text}, {"size", size}}});
        QVERIFY(!item.errorString().isEmpty());
        QCOMPARE(item.runs(), accepted);
        QCOMPARE(item.metrics(), metrics);
        QCOMPARE(item.maskRevision(), revision);
    }
    item.setRuns(accepted);
    item.setMeasurementOnly(false);
    const auto renderedRevision = item.maskRevision();
    for (int frame = 0; frame < 120; ++frame)
        item.setPoses({QVariantMap{{"key", "fractional"}, {"x", frame * .01}, {"opacity", .5}}});
    QCOMPARE(item.maskRevision(), renderedRevision);
    item.setPoses({});
    item.setMeasurementOnly(true);
    const qreal size = 12.4, scale = size / 64;
    const QString paragraph = QString::fromUtf8("office 星👩‍🚀\nسلام longsoftword");
    item.setRuns({QVariantMap{{"key", "wrapped"},
                              {"text", paragraph},
                              {"size", size},
                              {"maxWidth", 72},
                              {"lineHeight", 18},
                              {"spacing", .2},
                              {"maxBlur", 0}}});
    QVERIFY2(item.errorString().isEmpty(), qPrintable(item.errorString()));
    QFont reference = font;
    reference.setLetterSpacing(QFont::AbsoluteSpacing, .2 / scale);
    QString layoutText = paragraph;
    layoutText.replace('\n', QChar(0x2028));
    QTextLayout wrapped(layoutText, reference);
    QTextOption option;
    option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    option.setFlags(QTextOption::IncludeTrailingSpaces);
    wrapped.setTextOption(option);
    wrapped.beginLayout();
    qreal y = 0;
    while (true) {
        auto line = wrapped.createLine();
        if (!line.isValid())
            break;
        line.setLineWidth(72 / scale);
        line.setPosition({0, y});
        y += 18 / scale;
    }
    wrapped.endLayout();
    const auto wrappedMetric = item.metrics().first().toMap();
    QCOMPARE(wrappedMetric["lineCount"].toInt(), wrapped.lineCount());
    QCOMPARE(wrappedMetric["height"].toDouble(), y * scale);
    for (const auto &value : wrappedMetric["graphemes"].toList()) {
        const auto g = value.toMap();
        const auto line = wrapped.lineForTextPosition(g["start"].toInt());
        QCOMPARE(g["y"].toDouble(),
                 (line.y() + line.ascent() - wrapped.lineAt(0).ascent()) * scale);
    }
    item.setVisible(false);
    QCOMPARE(item.cacheBytes(), 0);
}

void GlyphRunTest::poseOpacityAppliesAfterEachFragmentComposite()
{
    QQuickWindow window;
    window.setGeometry(0, 0, 340, 280);
    window.setColor(Qt::black);
    GlyphRun item(window.contentItem());
    item.setSize({340, 280});
    QFont font("Arial");
    font.setPixelSize(96);
    item.setFont(font);
    QTextLayout layout("M", font);
    layout.beginLayout();
    layout.createLine().setLineWidth(10000);
    layout.endLayout();
    int nativeGlyphs = 0;
    for (const auto &glyphRun : layout.glyphRuns())
        nativeGlyphs += glyphRun.glyphIndexes().size();
    QCOMPARE(nativeGlyphs, 1);
    item.setRuns(
        {QVariantMap{{"key", "single"}, {"text", "M"}, {"maxBlur", 32}, {"maxHaloRadius", 40}}});
    QVERIFY2(item.errorString().isEmpty(), qPrintable(item.errorString()));
    QVariantMap pose{
        {"key", "single"},
        {"x", 120},
        {"y", 85},
        {"color", QColor(230, 240, 255, 204)},
        {"halos", QVariantList{QVariantMap{{"radius", 12}, {"color", QColor(255, 0, 0, 230)}},
                               QVariantMap{{"radius", 24}, {"color", QColor(0, 255, 30, 200)}}}}};
    item.setPoses({pose});
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QTRY_VERIFY(item.textureUploads() > 0);
    const auto revision = item.maskRevision(), uploads = item.textureUploads();
    for (const int blur : {0, 20}) {
        pose["blur"] = blur;
        pose.remove("opacity");
        item.setPoses({pose});
        const QImage opaque = window.grabWindow();
        const auto artifact = qEnvironmentVariable("GLYPH_OPACITY_ARTIFACT");
        if (!artifact.isEmpty())
            QVERIFY(opaque.save(artifact + QString("-blur%1.png").arg(blur)));
        pose["opacity"] = 1;
        item.setPoses({pose});
        QCOMPARE(window.grabWindow(), opaque);
        for (const qreal opacity : {.37, .6, 0.}) {
            pose["opacity"] = opacity;
            item.setPoses({pose});
            const QImage faded = window.grabWindow();
            int maximumError = 0;
            qint64 visible = 0;
            for (int y = 0; y < opaque.height(); ++y)
                for (int x = 0; x < opaque.width(); ++x) {
                    const auto a = opaque.pixel(x, y), b = faded.pixel(x, y);
                    visible += qRed(a) + qGreen(a) + qBlue(a);
                    maximumError =
                        std::max({maximumError, std::abs(qRed(b) - qRound(qRed(a) * opacity)),
                                  std::abs(qGreen(b) - qRound(qGreen(a) * opacity)),
                                  std::abs(qBlue(b) - qRound(qBlue(a) * opacity))});
                }
            QVERIFY(visible > 10000);
            QVERIFY2(maximumError <= 2,
                     qPrintable(QString("Single-fragment composite opacity error %1, blur %2")
                                    .arg(maximumError)
                                    .arg(blur)));
        }
    }
    for (int frame = 0; frame < 120; ++frame) {
        pose["opacity"] = frame / 119.;
        item.setPoses({pose});
        QCoreApplication::processEvents();
    }
    QCOMPARE(item.maskRevision(), revision);
    QCOMPARE(item.textureUploads(), uploads);
    item.setVisible(false);
    QCOMPARE(item.cacheBytes(), 0);
}

void GlyphRunTest::emMetricsUseThePrimaryFontTypographicRatio()
{
    GlyphRun item;
    item.setMeasurementOnly(true);
    struct Fixture
    {
        const char *family;
        int size;
        qreal middle;
    };
    // Chrome155 actual Canvas alphabeticBaseline for middle, same installed primary fonts.
    for (const auto &fixture : {Fixture{"Arial", 40, 11.03125}, Fixture{"Arial", 160, 44.125},
                                Fixture{"Times New Roman", 80, 21.015625}}) {
        if (!QFontDatabase::families().contains(fixture.family))
            continue;
        QFont font;
        font.setFamilies({fixture.family, "PingFang SC", "Apple Color Emoji"});
        font.setPixelSize(fixture.size);
        font.setWeight(QFont::Medium);
        item.setFont(font);
        QVariantMap first;
        for (const auto &text :
             {QString("Hg"), QString::fromUtf8("星😀"), QString(" "), QString("")}) {
            item.setRuns({QVariantMap{{"key", "em"}, {"text", text}, {"maxBlur", 0}}});
            QVERIFY(item.errorString().isEmpty());
            const auto metric = item.metrics().first().toMap();
            QVERIFY2(metric.contains("emAscent") && metric.contains("emDescent"),
                     "Primary font em metrics are missing");
            QCOMPARE(metric["emAscent"].toDouble() + metric["emDescent"].toDouble(), fixture.size);
            QCOMPARE((metric["emAscent"].toDouble() - metric["emDescent"].toDouble()) / 2,
                     fixture.middle);
            if (!first.isEmpty()) {
                QCOMPARE(metric["emAscent"], first["emAscent"]);
                QCOMPARE(metric["emDescent"], first["emDescent"]);
            }
            first = metric;
        }
    }
    // Independent table arithmetic covers primary fonts beyond the Latin Chrome fixtures.
    for (const char *family :
         {"Helvetica Neue", "PingFang SC", "Apple Color Emoji", "Apple Symbols"}) {
        if (!QFontDatabase::families().contains(family))
            continue;
        QFont font(family);
        font.setPixelSize(73);
        item.setFont(font);
        item.setRuns({QVariantMap{{"key", "em"}, {"text", "Hg"}, {"maxBlur", 0}}});
        const auto metric = item.metrics().first().toMap();
        const auto raw = QRawFont::fromFont(font);
        const auto os2 = raw.fontTable("OS/2");
        float ascent = raw.ascent(), descent = raw.descent();
        if (os2.size() >= 72) {
            const auto typoAscent = qFromBigEndian<qint16>(os2.constData() + 68);
            const auto typoDescent = -int(qFromBigEndian<qint16>(os2.constData() + 70));
            if (typoAscent > 0 && typoDescent >= 0) {
                ascent = typoAscent;
                descent = typoDescent;
            }
        }
        const float normalized =
            std::round(ascent * float(raw.pixelSize()) / (ascent + descent) * 64) / 64;
        QCOMPARE(metric["emAscent"].toDouble(), normalized);
        QCOMPARE(metric["emDescent"].toDouble(),
                 std::round(float(raw.pixelSize()) * 64) / 64 - normalized);
    }
    QCOMPARE(item.cacheBytes(), 0);
}

void GlyphRunTest::metricsDistinguishAdvanceFromFullContextInkBounds()
{
    GlyphRun item;
    item.setMeasurementOnly(true);
    for (bool italic : {false, true}) {
        QFont font;
        font.setFamilies({"Times New Roman", "PingFang SC", "Apple Color Emoji"});
        font.setPixelSize(80);
        font.setItalic(italic);
        item.setFont(font);
        for (const auto &text : {QString("f"), QString("j"), QString(" "), QString(""),
                                 QString::fromUtf8("ffi سلام abc星👩‍🚀"),
                                 QString::fromUtf8("אבג abc لا")}) {
            item.setRuns({QVariantMap{{"key", "measure"}, {"text", text}, {"maxBlur", 0}}});
            QVERIFY2(item.errorString().isEmpty(), qPrintable(item.errorString()));
            const auto metric = item.metrics().first().toMap();
            QVERIFY2(metric.contains("advance") && metric.contains("inkLeft") &&
                         metric.contains("inkRight"),
                     "Native advance and horizontal ink bounds are missing");
            QTextLayout layout(text, font);
            layout.beginLayout();
            auto line = layout.createLine();
            if (line.isValid())
                line.setLineWidth(1000000);
            layout.endLayout();
            QRectF ink;
            for (const auto &run : layout.glyphRuns()) {
                const auto indexes = run.glyphIndexes();
                const auto positions = run.positions();
                for (int index = 0; index < indexes.size(); ++index) {
                    const auto bounds = run.rawFont().boundingRect(indexes[index]);
                    if (!bounds.isEmpty())
                        ink |= bounds.translated(positions[index]);
                }
            }
            QCOMPARE(metric["width"].toDouble(), line.isValid() ? line.naturalTextWidth() : 0);
            QCOMPARE(metric["advance"].toDouble(), line.isValid() ? line.horizontalAdvance() : 0);
            QCOMPARE(metric["inkLeft"].toDouble(), ink.isEmpty() ? 0 : ink.left());
            QCOMPARE(metric["inkRight"].toDouble(), ink.isEmpty() ? 0 : ink.right());
            if (italic && text == "f") {
                QVERIFY(metric["inkLeft"].toDouble() < 0);
                QVERIFY(metric["inkRight"].toDouble() - metric["inkLeft"].toDouble() >
                        metric["width"].toDouble());
            }
        }
    }
    QCOMPARE(item.cacheBytes(), 0);
    QCOMPARE(item.textureUploads(), 0);
}

void GlyphRunTest::bitmapStrokeFallbackKeepsRgbaAndOutlineContext()
{
    if (!QFontDatabase::families().contains("Apple Color Emoji"))
        QSKIP("Requires a native bitmap colour font");
    QQuickWindow window;
    window.setGeometry(0, 0, 620, 240);
    window.setColor(Qt::black);
    GlyphRun item(window.contentItem());
    item.setSize({620, 240});
    QFont font;
    font.setFamilies({"Times New Roman", "Apple Color Emoji"});
    font.setPixelSize(96);
    item.setFont(font);
    const QString text = QString::fromUtf8("ffi 👩‍🚀 😀");
    const QVariantList runs{QVariantMap{{"key", "mixed"},
                                        {"text", text},
                                        {"strokeWidth", 1.92},
                                        {"strokeBitmap", "fill"},
                                        {"maxBlur", 0}}};
    item.setRuns(runs);
    QVERIFY2(item.errorString().isEmpty(), qPrintable(item.errorString()));
    item.setPoses({QVariantMap{{"key", "mixed"}, {"x", 30}, {"y", 30}, {"color", "red"}}});
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QTRY_VERIFY(item.textureUploads() > 0);
    const auto actual = window.grabWindow();
    QImage expected(actual.size(), QImage::Format_RGB32);
    expected.setDevicePixelRatio(window.devicePixelRatio());
    expected.fill(Qt::black);
    QTextLayout layout(text, font);
    layout.beginLayout();
    auto line = layout.createLine();
    line.setLineWidth(1000000);
    layout.endLayout();
    {
        QPainter painter(&expected);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::red);
        painter.translate(30, 30);
        for (const auto &run : layout.glyphRuns()) {
            QPainterPath outline;
            QList<quint32> bitmapIndexes;
            QList<QPointF> bitmapPositions;
            const auto indexes = run.glyphIndexes();
            const auto positions = run.positions();
            for (int index = 0; index < indexes.size(); ++index) {
                const auto path = run.rawFont().pathForGlyph(indexes[index]);
                if (path.isEmpty() && !run.rawFont().boundingRect(indexes[index]).isEmpty()) {
                    bitmapIndexes.append(indexes[index]);
                    bitmapPositions.append(positions[index]);
                } else
                    outline.addPath(path.translated(positions[index]));
            }
            QPainterPathStroker stroker;
            stroker.setWidth(1.92);
            stroker.setJoinStyle(Qt::MiterJoin);
            stroker.setMiterLimit(10);
            painter.fillPath(stroker.createStroke(outline), Qt::red);
            if (!bitmapIndexes.isEmpty()) {
                auto bitmap = run;
                bitmap.setGlyphIndexes(bitmapIndexes);
                bitmap.setPositions(bitmapPositions);
                painter.drawGlyphRun({0, 0}, bitmap);
            }
        }
    }
    qint64 error = 0, energy = 0, colour = 0;
    for (int y = 0; y < actual.height(); ++y)
        for (int x = 0; x < actual.width(); ++x) {
            const auto a = actual.pixel(x, y), e = expected.pixel(x, y);
            error += std::abs(qRed(a) - qRed(e)) + std::abs(qGreen(a) - qGreen(e)) +
                     std::abs(qBlue(a) - qBlue(e));
            energy += qRed(e) + qGreen(e) + qBlue(e);
            colour += qGreen(a) + qBlue(a);
        }
    QVERIFY(colour > 10000);
    qInfo() << "Bitmap stroke mixed-context RGB relative error" << double(error) / energy;
    QVERIFY2(double(error) / energy < .12,
             qPrintable(QString("Mixed bitmap stroke RGB error %1").arg(double(error) / energy)));
    const auto revision = item.maskRevision(), uploads = item.textureUploads();
    const auto acceptedMetrics = item.metrics();
    for (int frame = 0; frame < 120; ++frame) {
        item.setPoses({QVariantMap{
            {"key", "mixed"},
            {"x", 30 + (frame % 7) * .01},
            {"y", 30},
            {"color", QColor(255, 0, 0, 128)},
            {"opacity", frame % 2 ? .4 : .5},
            {"clip", QVariantMap{{"x", 0}, {"y", 0}, {"width", 160}, {"height", 240}}}}});
        QCoreApplication::processEvents();
    }
    item.setPoses(
        {QVariantMap{{"key", "mixed"},
                     {"x", 30},
                     {"y", 30},
                     {"color", QColor(255, 0, 0, 128)},
                     {"opacity", .5},
                     {"clip", QVariantMap{{"x", 0}, {"y", 0}, {"width", 160}, {"height", 240}}}}});
    const auto clipped = window.grabWindow();
    int opacityError = 0;
    int opacitySamples = 0;
    for (int y = 0; y < clipped.height(); ++y)
        for (int x = 0; x < qFloor(188 * window.devicePixelRatio()); ++x) {
            const auto before = actual.pixelColor(x, y), after = clipped.pixelColor(x, y);
            // Only the bitmap's coloured ink is a single native fragment here.
            // Overlapping red Latin outlines retain the established per-fragment source-over.
            if (before.green() + before.blue() < 64)
                continue;
            ++opacitySamples;
            const int error =
                std::max({std::abs(after.red() - qRound(before.red() * (128. / 255) * .5)),
                          std::abs(after.green() - qRound(before.green() * (128. / 255) * .5)),
                          std::abs(after.blue() - qRound(before.blue() * (128. / 255) * .5))});
            opacityError = std::max(opacityError, error);
        }
    QVERIFY(opacitySamples > 1000);
    QVERIFY2(opacityError <= 2,
             qPrintable(QString("Bitmap alpha/pose opacity error %1").arg(opacityError)));
    for (int y = 0; y < clipped.height(); ++y)
        for (int x = qCeil(191 * window.devicePixelRatio()); x < clipped.width(); ++x)
            QCOMPARE(clipped.pixelColor(x, y), QColor(Qt::black));
    QCOMPARE(item.maskRevision(), revision);
    QCOMPARE(item.textureUploads(), uploads);
    for (const QVariant &invalid : {QVariant("unknown"), QVariant(true)}) {
        auto run = runs.first().toMap();
        run["strokeBitmap"] = invalid;
        item.setRuns({run});
        QVERIFY(!item.errorString().isEmpty());
        QCOMPARE(item.runs(), runs);
        QCOMPARE(item.metrics(), acceptedMetrics);
        QCOMPARE(item.maskRevision(), revision);
    }
    auto strict = runs.first().toMap();
    strict.remove("strokeBitmap");
    item.setRuns({strict});
    QVERIFY(!item.errorString().isEmpty());
    QCOMPARE(item.runs(), runs);
    item.setVisible(false);
    QCOMPARE(item.cacheBytes(), 0);
}

void GlyphRunTest::screenCompositionMatchesPremultipliedOverlapWithoutUploads()
{
    QQuickWindow window;
    window.setGeometry(0, 0, 280, 200);
    window.setColor(Qt::black);
    GlyphRun item(window.contentItem());
    item.setSize({280, 200});
    QFont font("Helvetica Neue");
    font.setPixelSize(96);
    item.setFont(font);
    item.setRuns({QVariantMap{{"key", "word"}, {"text", "M"}, {"maxBlur", 0}}});
    const QVariantMap whiteA{
        {"key", "word"}, {"instance", "a"}, {"x", 55}, {"y", 40}, {"color", "white"}};
    const QVariantMap whiteB{
        {"key", "word"}, {"instance", "b"}, {"x", 63}, {"y", 44}, {"color", "white"}};
    item.setPoses({whiteA});
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QTRY_VERIFY(item.textureUploads() > 0);
    const auto coverageA = window.grabWindow();
    item.setPoses({whiteB});
    const auto coverageB = window.grabWindow();
    const QColor background(45, 70, 90), colorA(40, 210, 240, 153), colorB(230, 30, 80, 128);
    window.setColor(background);
    auto poseA = whiteA, poseB = whiteB;
    poseA["color"] = colorA;
    poseB["color"] = colorB;
    item.setPoses({poseA, poseB});
    const auto normal = window.grabWindow();
    const auto revision = item.maskRevision(), uploads = item.textureUploads();
    QVERIFY2(item.setProperty("composition", "screen"), "Public composition property is missing");
    const auto screen = window.grabWindow();
    int overlapping = 0;
    qint64 normalError = 0, screenError = 0;
    for (int y = 0; y < screen.height(); ++y)
        for (int x = 0; x < screen.width(); ++x) {
            const double a = qRed(coverageA.pixel(x, y)) / 255.0 * colorA.alphaF();
            const double b = qRed(coverageB.pixel(x, y)) / 255.0 * colorB.alphaF();
            if (a <= .1 || b <= .1)
                continue;
            ++overlapping;
            for (int channel = 0; channel < 3; ++channel) {
                auto component = [channel](QColor color) {
                    return channel == 0   ? color.redF()
                           : channel == 1 ? color.greenF()
                                          : color.blueF();
                };
                const double ca = component(colorA) * a, cb = component(colorB) * b,
                             bg = component(background);
                const int expectedScreen = qRound((cb + (ca + bg * (1 - ca)) * (1 - cb)) * 255);
                const int expectedNormal = qRound((cb + (ca + bg * (1 - a)) * (1 - b)) * 255);
                const auto actualScreen = QColor(screen.pixel(x, y)),
                           actualNormal = QColor(normal.pixel(x, y));
                screenError += std::abs(qRound(component(actualScreen) * 255) - expectedScreen);
                normalError += std::abs(qRound(component(actualNormal) * 255) - expectedNormal);
            }
        }
    QVERIFY(overlapping > 100);
    QVERIFY2(
        double(screenError) / (overlapping * 3) < 1.5,
        qPrintable(
            QString("Screen overlap RGB error: %1").arg(double(screenError) / (overlapping * 3))));
    QVERIFY(double(normalError) / (overlapping * 3) < 1.5);
    QVERIFY(screen != normal);
    const auto grab = item.grabToImage();
    QSignalSpy ready(grab.data(), &QQuickItemGrabResult::ready);
    QTRY_VERIFY(!ready.isEmpty());
    const auto transparent = grab->image().convertToFormat(QImage::Format_ARGB32_Premultiplied);
    QCOMPARE(transparent.size(), screen.size());
    qint64 alphaError = 0;
    for (int y = 0; y < transparent.height(); ++y)
        for (int x = 0; x < transparent.width(); ++x) {
            const double a = qRed(coverageA.pixel(x, y)) / 255.0 * colorA.alphaF();
            const double b = qRed(coverageB.pixel(x, y)) / 255.0 * colorB.alphaF();
            alphaError += std::abs(qAlpha(transparent.pixel(x, y)) - qRound((a + b - a * b) * 255));
        }
    QVERIFY2(
        double(alphaError) / (transparent.width() * transparent.height()) < .2,
        qPrintable(QString("Screen transparent alpha error: %1")
                       .arg(double(alphaError) / (transparent.width() * transparent.height()))));
    QCOMPARE(item.maskRevision(), revision);
    QCOMPARE(item.textureUploads(), uploads);
    QVERIFY(item.setProperty("composition", "source-over"));
    QCOMPARE(window.grabWindow(), normal);
    QVERIFY(item.setProperty("composition", "multiply"));
    QVERIFY(!item.errorString().isEmpty());
    QCOMPARE(item.property("composition").toString(), QString("source-over"));
    QCOMPARE(window.grabWindow(), normal);
    for (int frame = 0; frame < 120; ++frame) {
        QVERIFY(item.setProperty("composition", frame % 2 ? "screen" : "source-over"));
        QCoreApplication::processEvents();
    }
    QCOMPARE(item.maskRevision(), revision);
    QCOMPARE(item.textureUploads(), uploads);
    item.setVisible(false);
    QCOMPARE(item.cacheBytes(), 0);
}

void GlyphRunTest::automaticEnvironmentRebuildKeepsExplicitRejectionErrors()
{
    GlyphRun item;
    item.setRuns(oneRun(QString(GlyphRun::MaxTextUnits + 1, 'a')));
    const auto rejected = item.errorString();
    QVERIFY(!rejected.isEmpty());
    QSignalSpy errors(&item, &GlyphRun::errorChanged);
    QQuickWindow window;
    item.setParentItem(window.contentItem());
    QCOMPARE(item.errorString(), rejected);
    QVERIFY(item.runs().isEmpty());
    QVERIFY(item.metrics().isEmpty());
    item.setVisible(false);
    item.setVisible(true);
    QCOMPARE(item.errorString(), rejected);
    QCOMPARE(errors.count(), 0); // No transient fake recovery signal.
    item.setRuns(oneRun("fixed"));
    QVERIFY(item.errorString().isEmpty());
    const QVariantList pose{
        QVariantMap{{"key", "line"}, {"x", 42}, {"opacity", .4}, {"color", "red"}}};
    item.setPoses(pose);
    QVERIFY(item.errorString().isEmpty());
    item.setPoses({QVariantMap{{"key", "line"}, {"x", std::numeric_limits<double>::quiet_NaN()}}});
    const auto rejectedPose = item.errorString();
    QVERIFY(!rejectedPose.isEmpty());
    errors.clear();
    item.setVisible(false);
    item.setVisible(true);
    QCOMPARE(item.errorString(), rejectedPose);
    QCOMPARE(item.poses(), pose);
    QCOMPARE(errors.count(), 0);
    item.setRuns(item.runs());
    QVERIFY(item.errorString().isEmpty());
}

void GlyphRunTest::sourceSonnetDecorationKeepsTheExistingInkBudget()
{
    // Actual type-impact layout, viewport1000x700/base112, one hero "brightest":
    // decoration fontScale5.5/weight300, stroke=616*.006 (a8da4437).
    QQuickWindow window;
    GlyphRun item(window.contentItem());
    QFont font("Helvetica Neue");
    font.setPixelSize(616);
    font.setWeight(QFont::Light);
    item.setFont(font);
    const QVariantList runs{QVariantMap{
        {"key", "decoration"}, {"text", "brightest"}, {"strokeWidth", 3.696}, {"maxBlur", 0}}};
    item.setRuns(runs);
    qInfo() << "Sonnet actual decoration" << window.devicePixelRatio() << "bytes"
            << item.cacheBytes() << "error" << item.errorString();
    QVERIFY2(item.errorString().isEmpty(), qPrintable(item.errorString()));
    QCOMPARE(item.runs(), runs);
    QVERIFY(item.cacheBytes() > 0);
    QVERIFY(item.cacheBytes() <= 32LL * 1024 * 1024);
}

void GlyphRunTest::strokeOnlyUsesCompleteNativeOutlines()
{
    QQuickWindow window;
    window.setColor(Qt::black);
    window.setGeometry(0, 0, 700, 240);
    GlyphRun item(window.contentItem());
    item.setSize({700, 240});
    QFont font("Times New Roman");
    font.setPixelSize(72);
    font.setItalic(true);
    item.setFont(font);
    const QString text = QString::fromUtf8("ffi سلام");
    item.setRuns(
        {QVariantMap{{"key", "outline"}, {"text", text}, {"maxBlur", 0}, {"strokeWidth", 2.5}}});
    QVERIFY2(item.errorString().isEmpty(), qPrintable(item.errorString()));
    item.setPoses({QVariantMap{{"key", "outline"}, {"x", 40}, {"y", 20}, {"color", "red"}}});
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QTRY_VERIFY(item.textureUploads() > 0);
    const auto actual = window.grabWindow();
    QImage expected(actual.size(), QImage::Format_RGB32);
    expected.setDevicePixelRatio(window.devicePixelRatio());
    expected.fill(Qt::black);
    QTextLayout layout(text, font);
    layout.beginLayout();
    auto line = layout.createLine();
    line.setLineWidth(1000000);
    layout.endLayout();
    QPainterPath path;
    path.addText(QPointF(40, 20 + line.ascent()), font, text);
    QPainterPathStroker stroker;
    stroker.setWidth(2.5);
    stroker.setJoinStyle(Qt::MiterJoin);
    stroker.setMiterLimit(10);
    {
        QPainter painter(&expected);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.fillPath(stroker.createStroke(path), Qt::red);
    }
    qint64 error = 0, energy = 0, actualEnergy = 0;
    for (int y = 0; y < actual.height(); ++y)
        for (int x = 0; x < actual.width(); ++x) {
            error += std::abs(qRed(actual.pixel(x, y)) - qRed(expected.pixel(x, y)));
            energy += qRed(expected.pixel(x, y));
            actualEnergy += qRed(actual.pixel(x, y));
        }
    QVERIFY2(double(error) / energy < .16,
             qPrintable(QString("Full-context stroke RGB error %1").arg(double(error) / energy)));
    QVERIFY(actualEnergy > 1000);
    const auto defaultStroke = item.runs();
    for (const auto &policy : {"reject", "fill"}) {
        auto explicitPolicy = defaultStroke.first().toMap();
        explicitPolicy["strokeBitmap"] = policy;
        item.setRuns({explicitPolicy});
        QVERIFY(item.errorString().isEmpty());
        item.setPoses({QVariantMap{{"key", "outline"}, {"x", 40}, {"y", 20}, {"color", "red"}}});
        QCOMPARE(window.grabWindow(), actual); // Ordinary outline masks stay byte-identical.
    }
    item.setRuns(defaultStroke);
    item.setPoses({QVariantMap{{"key", "outline"}, {"x", 40}, {"y", 20}, {"color", "red"}}});
    QCOMPARE(window.grabWindow(), actual);
    const auto revision = item.maskRevision(), uploads = item.textureUploads();
    for (int frame = 0; frame < 120; ++frame) {
        item.setPoses({QVariantMap{{"key", "outline"},
                                   {"x", 40 + frame * .1},
                                   {"y", 20},
                                   {"color", frame % 2 ? "blue" : "red"},
                                   {"opacity", .7}}});
        QCoreApplication::processEvents();
    }
    QCOMPARE(item.maskRevision(), revision);
    QCOMPARE(item.textureUploads(), uploads);
    const auto accepted = item.runs();
    const auto outlinedMetrics = item.metrics();
    auto noStroke = accepted.first().toMap();
    noStroke.remove("strokeWidth");
    const QVariantList bodyPose{
        QVariantMap{{"key", "outline"}, {"x", 40}, {"y", 20}, {"color", "red"}}};
    item.setRuns({noStroke});
    item.setPoses(bodyPose);
    QCoreApplication::processEvents();
    const auto defaultBody = window.grabWindow();
    noStroke["strokeWidth"] = 0;
    item.setRuns({noStroke});
    item.setPoses(bodyPose);
    QCoreApplication::processEvents();
    QCOMPARE(window.grabWindow(), defaultBody);
    QCOMPARE(item.metrics(), outlinedMetrics);
    item.setRuns(accepted);
    for (double width : {-1., 8.01, std::numeric_limits<double>::quiet_NaN()}) {
        item.setRuns({QVariantMap{{"key", "outline"}, {"text", text}, {"strokeWidth", width}}});
        QVERIFY(!item.errorString().isEmpty());
        QCOMPARE(item.runs(), accepted);
    }
    if (QFontDatabase::families().contains("Apple Color Emoji")) {
        QFont emoji("Apple Color Emoji");
        emoji.setPixelSize(64);
        item.setFont(emoji);
        const auto before = item.runs();
        item.setRuns({QVariantMap{{"key", "bitmap"},
                                  {"text", QString::fromUtf8("👩‍🚀")},
                                  {"strokeWidth", 2},
                                  {"maxBlur", 0}}});
        QVERIFY2(!item.errorString().isEmpty(),
                 "Bitmap color glyph without an outline must reject stroke-only input");
        QCOMPARE(item.runs(), before);
    }
    item.setRuns(
        {QVariantMap{{"key", "blank"}, {"text", " "}, {"strokeWidth", 8}, {"maxBlur", 0}}});
    QVERIFY(item.errorString().isEmpty());
    QCOMPARE(item.cacheBytes(), 0);
}

void GlyphRunTest::largeVisibleFontsKeepFullDprAndAtomicBudgets()
{
    QQuickWindow window;
    window.setColor(Qt::black);
    window.setGeometry(0, 0, 900, 900);
    window.setMinimumSize({900, 900});
    GlyphRun item(window.contentItem());
    item.setSize({900, 900});
    QFont font("PingFang SC");
    font.setPixelSize(616);
    font.setWeight(QFont::Bold);
    item.setFont(font);
    QCOMPARE(item.font(), font);
    const auto text = QString::fromUtf8("星");
    const QVariantList run{
        QVariantMap{{"key", "large"}, {"text", text}, {"maxBlur", 0}, {"strokeWidth", 3.696}}};
    item.setRuns(run);
    QVERIFY2(item.errorString().isEmpty(), qPrintable(item.errorString()));
    item.setPoses({QVariantMap{{"key", "large"}, {"x", 80}, {"y", 30}, {"color", "white"}}});
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QTRY_VERIFY(item.textureUploads() > 0);
    const auto image = window.grabWindow();
    const auto completeMetric = item.metrics().first().toMap();
    QVERIFY(image.height() / window.devicePixelRatio() >
            30 + completeMetric.value("baseline").toDouble() +
                completeMetric.value("inkBottom").toDouble() + 8);
    QImage expected(image.size(), QImage::Format_RGB32);
    expected.setDevicePixelRatio(window.devicePixelRatio());
    expected.fill(Qt::black);
    QTextLayout layout(text, font);
    layout.beginLayout();
    auto line = layout.createLine();
    line.setLineWidth(1000000);
    layout.endLayout();
    QPainterPath path;
    path.addText(QPointF(80, 30 + line.ascent()), font, text);
    QPainterPathStroker stroker;
    stroker.setWidth(3.696);
    stroker.setJoinStyle(Qt::MiterJoin);
    stroker.setMiterLimit(10);
    {
        QPainter painter(&expected);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.fillPath(stroker.createStroke(path), Qt::white);
    }
    qint64 error = 0, energy = 0;
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x) {
            error += std::abs(qRed(image.pixel(x, y)) - qRed(expected.pixel(x, y)));
            energy += qRed(expected.pixel(x, y));
        }
    QVERIFY2(double(error) / energy < .08,
             qPrintable(QString("616px native outline RGB error %1").arg(double(error) / energy)));
    QVERIFY(item.cacheBytes() <= 32LL * 1024 * 1024);
    const auto revision = item.maskRevision();
    item.setRuns({QVariantMap{{"key", "large"},
                              {"text", QString(20, QChar(0x661f))},
                              {"size", 616},
                              {"maxBlur", 0},
                              {"strokeWidth", 3.696}}});
    QVERIFY(!item.errorString().isEmpty());
    QCOMPARE(item.runs(), run);
    QCOMPARE(item.maskRevision(), revision);
    item.setRuns({QVariantMap{{"key", "large"}, {"text", text}, {"size", 1025}}});
    QVERIFY(!item.errorString().isEmpty());
    QCOMPARE(item.runs(), run);
    item.setVisible(false);
    QCOMPARE(item.cacheBytes(), 0);
}

void GlyphRunTest::haloKernelMatchesCanvasForIdenticalInk()
{
    const QString chrome = qEnvironmentVariable(
        "GLYPH_RUN_CHROME", "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome");
    if (!QFile::exists(chrome))
        QSKIP("Chrome is required for the independent Canvas convolution oracle");
    QQuickWindow window;
    window.setColor(Qt::black);
    window.setMinimumSize({1024, 1024});
    window.setGeometry(0, 0, 1024, 1024);
    GlyphRun item(window.contentItem());
    item.setSize({1024, 1024});
    QFont font("PingFang SC");
    font.setPixelSize(152);
    font.setWeight(QFont::Weight(780));
    item.setFont(font);
    const auto text = QString::fromUtf8("星");
    item.setRuns(
        {QVariantMap{{"key", "kernel"}, {"text", text}, {"maxBlur", 0}, {"maxHaloRadius", 512}}});
    QVERIFY2(item.errorString().isEmpty(), qPrintable(item.errorString()));
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QTRY_VERIFY(item.textureUploads() > 0);
    const qreal dpr = window.devicePixelRatio(),
                baseline = item.metrics().first().toMap().value("baseline").toDouble();
    QImage ink(QSize(256 * dpr, 256 * dpr), QImage::Format_ARGB32_Premultiplied);
    ink.setDevicePixelRatio(dpr);
    ink.fill(Qt::transparent);
    QTextLayout layout(text, font);
    layout.beginLayout();
    auto line = layout.createLine();
    line.setLineWidth(1000000);
    layout.endLayout();
    {
        QPainter painter(&ink);
        painter.setPen(Qt::white);
        layout.draw(&painter, {});
    }
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    QVERIFY(ink.save(&buffer, "PNG"));
    const QByteArray html = QByteArray(R"HTML(<!doctype html><pre id="result"></pre><script>
const dpr=DENSITY;
const img=new Image();
img.onload=()=>{
    const out=[];
    function surface(){
        const c=document.createElement('canvas');c.width=c.height=1024*dpr;
        const x=c.getContext('2d',{willReadFrequently:true});
        x.fillStyle='black';x.fillRect(0,0,c.width,c.height);
        x.setTransform(.22*dpr,0,0,.22*dpr,512*dpr,500*dpr);
        return {c,x};
    }
    for(const gain of [0,.6,.8,1]){
        const {c,x}=surface();
        // fillText shadows modulate the already rasterized coverage with paint
        // alpha. drawImage/globalAlpha instead puts alpha inside its image filter.
        // Reproduce TEXT paint semantics with the identical immutable ink.
        if(gain){x.shadowColor='rgba(255,255,255,.756)';x.shadowBlur=213*gain;
            x.shadowOffsetX=1100*dpr;x.drawImage(img,-5000,0,256,256);}
        x.shadowColor='transparent';x.shadowOffsetX=0;x.globalAlpha=.756;
        x.drawImage(img,0,0,256,256);
        const p=x.getImageData(0,0,c.width,c.height).data;
        let mass=0,first=0,second=0;
        for(let y=0;y<c.height;y++)for(let xx=0;xx<c.width;xx++){
            const v=p[(y*c.width+xx)*4];mass+=v;first+=xx*v;second+=xx*xx*v;}
        // Independent original Fume fillText validates the alpha-stage oracle,
        // rather than assuming that an image-filter pipeline is equivalent.
        const direct=surface(),split=surface();
        for(const target of [direct,split]){
            target.x.font='780 151.54071925781247px "Helvetica Neue", "PingFang SC"';
            target.x.textBaseline='middle';target.x.shadowBlur=213*gain;}
        direct.x.fillStyle='rgba(255,255,255,.756)';direct.x.shadowColor='white';
        direct.x.fillText('星',0,0);
        if(gain){split.x.fillStyle='white';split.x.shadowColor='rgba(255,255,255,.756)';
            split.x.shadowOffsetX=1100*dpr;split.x.fillText('星',-5000,0);}
        split.x.shadowOffsetX=0;split.x.shadowColor='transparent';
        split.x.fillStyle='rgba(255,255,255,.756)';split.x.fillText('星',0,0);
        const original=direct.x.getImageData(0,0,c.width,c.height).data,
              projected=split.x.getImageData(0,0,c.width,c.height).data;
        let sourceTextSplitMaxError=0;
        for(let i=0;i<original.length;i+=4)
            sourceTextSplitMaxError=Math.max(sourceTextSplitMaxError,Math.abs(original[i]-projected[i]));
        out.push({mass,sigma:Math.sqrt(second/mass-(first/mass)**2)/dpr,sourceTextSplitMaxError});
    }
    document.getElementById('result').textContent=JSON.stringify(out);
};img.src='data:image/png;base64,IMAGE';
</script>)HTML")
                                .replace("DENSITY", QByteArray::number(dpr))
                                .replace("IMAGE", png.toBase64());
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath("oracle.html");
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(html), html.size());
    file.close();
    QProcess process;
    process.start(chrome, {"--headless", "--no-sandbox", "--disable-background-networking",
                           "--disable-component-update", "--virtual-time-budget=1000", "--dump-dom",
                           QUrl::fromLocalFile(path).toString()});
    QVERIFY(process.waitForFinished(20000));
    QCOMPARE(process.exitCode(), 0);
    const auto output = process.readAllStandardOutput();
    const QByteArray opening = "<pre id=\"result\">";
    const auto start = output.indexOf(opening), end = output.indexOf("</pre>", start);
    QVERIFY2(start >= 0 && end > start, output.constData());
    const auto oracle =
        QJsonDocument::fromJson(output.mid(start + opening.size(), end - start - opening.size()))
            .array();
    QCOMPARE(oracle.size(), 4);
    for (int index = 0; index < 4; ++index) {
        const qreal gain = index == 0 ? 0 : .4 + index * .2;
        QVariantMap pose{{"key", "kernel"},
                         {"x", 512},
                         {"y", 500 - (1 - .22) * baseline},
                         {"scale", .22},
                         {"color", QColor::fromRgbF(1, 1, 1, .756)}};
        if (index)
            pose["halos"] = QVariantList{QVariantMap{
                {"radius", 213 * gain / (2 * .22 * dpr)}, {"opacity", .756}, {"color", "white"}}};
        item.setPoses({pose});
        QVERIFY(item.errorString().isEmpty());
        QCoreApplication::processEvents();
        const auto actual = window.grabWindow();
        double mass = 0, first = 0, second = 0;
        for (int y = 0; y < actual.height(); ++y)
            for (int x = 0; x < actual.width(); ++x) {
                const double v = qRed(actual.pixel(x, y));
                mass += v;
                first += x * v;
                second += x * x * v;
            }
        const double sigma = std::sqrt(second / mass - std::pow(first / mass, 2)) / dpr;
        const auto reference = oracle[index].toObject();
        QCOMPARE(reference["sourceTextSplitMaxError"].toInt(-1), 0);
        const double massError = std::abs(mass / reference["mass"].toDouble() - 1),
                     sigmaError = std::abs(sigma / reference["sigma"].toDouble() - 1);
        qInfo() << "Same-ink Canvas" << dpr << gain << "native" << mass << sigma << "Canvas"
                << reference << "relative" << massError << sigmaError;
        QVERIFY2(massError < (index ? .15 : .05),
                 "Identical-ink Canvas energy differs beyond its bound");
        QVERIFY2(sigmaError < (index ? .15 : .05),
                 "Identical-ink Canvas second moment differs beyond its bound");
    }
}

void GlyphRunTest::syntheticBoldMatchesNativeTextLayout()
{
    QFont font;
    font.setFamilies({"Helvetica Neue", "Inter", "Noto Sans CJK SC", "Noto Sans JP",
                      "Source Han Sans SC", "PingFang SC", "Hiragino Sans GB", "Microsoft YaHei",
                      "Helvetica Neue", "Arial", "sans-serif"});
    font.setPixelSize(152);
    font.setWeight(QFont::Weight(780));
    const QString text = QString::fromUtf8("星");
    QTextLayout layout(text, font);
    layout.beginLayout();
    auto line = layout.createLine();
    line.setLineWidth(1000000);
    layout.endLayout();
    QList<qint64> masses;
    for (int method = 0; method < 3; ++method) {
        QImage image(256, 256, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        {
            QPainter painter(&image);
            painter.setPen(Qt::white);
            painter.setFont(font);
            if (method == 0)
                layout.draw(&painter, {});
            else if (method == 1)
                for (const auto &glyph : layout.glyphRuns())
                    painter.drawGlyphRun({}, glyph);
            else
                painter.drawText(QPointF(0, line.ascent()), text);
        }
        qint64 mass = 0;
        for (int y = 0; y < image.height(); ++y)
            for (int x = 0; x < image.width(); ++x)
                mass += qAlpha(image.pixel(x, y));
        masses.append(mass);
    }
    qInfo() << "Native layout/drawGlyphRun/drawText alpha masses" << masses;
    QCOMPARE(masses[1], masses[0]);
    QCOMPARE(masses[2], masses[0]);
}

void GlyphRunTest::clippedInstancesKeepContextAndOrderWithoutUploads()
{
    QQuickWindow window;
    window.setColor(Qt::black);
    window.setGeometry(0, 0, 600, 240);
    GlyphRun item(window.contentItem());
    item.setSize({600, 240});
    QFont font("Times New Roman");
    font.setPixelSize(72);
    font.setItalic(true);
    item.setFont(font);
    const QString text = QString::fromUtf8("ffi سلام");
    item.setRuns(
        {QVariantMap{{"key", "clip"}, {"text", text}, {"maxBlur", 0}, {"maxHaloRadius", 40}}});
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QTRY_VERIFY(item.textureUploads() > 0);
    const auto metric = item.metrics().first().toMap();
    const double width = metric.value("width").toDouble();
    const auto revision = item.maskRevision(), uploads = item.textureUploads();
    const auto piece = [&](const QString &id, double x, double w, const QColor &color) {
        return QVariantMap{
            {"key", "clip"},
            {"instance", id},
            {"x", 80},
            {"y", 40},
            {"color", color},
            {"clip", QVariantMap{{"x", x}, {"y", -100}, {"width", w}, {"height", 300}}}};
    };
    const double split = width * .37;
    // Clip is evaluated in the complete run's coordinates before scale/rotation.
    auto rotated = piece("rotated", 0, split, Qt::red);
    rotated["rotation"] = 12;
    rotated["scale"] = .8;
    item.setPoses({rotated});
    QVERIFY(item.errorString().isEmpty());
    QCoreApplication::processEvents();
    const auto rotatedActual = window.grabWindow();
    QImage rotatedExpected(rotatedActual.size(), QImage::Format_RGB32);
    rotatedExpected.setDevicePixelRatio(window.devicePixelRatio());
    rotatedExpected.fill(Qt::black);
    QTextLayout rotatedLayout(text, font);
    rotatedLayout.beginLayout();
    auto rotatedLine = rotatedLayout.createLine();
    rotatedLine.setLineWidth(1000000);
    rotatedLayout.endLayout();
    // GlyphRun rotates an immutable mask. Rotate the same untransformed native
    // ink here; drawing vector glyphs after rotation would rasterize new coverage.
    QImage rotatedInk(rotatedActual.size(), QImage::Format_ARGB32_Premultiplied);
    rotatedInk.setDevicePixelRatio(window.devicePixelRatio());
    rotatedInk.fill(Qt::transparent);
    {
        QPainter painter(&rotatedInk);
        painter.setPen(Qt::red);
        rotatedLayout.draw(&painter, {});
    }
    {
        QPainter painter(&rotatedExpected);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        const auto baseline = metric.value("baseline").toDouble();
        painter.translate(80, 40 + baseline);
        painter.rotate(12);
        painter.scale(.8, .8);
        painter.translate(0, -baseline);
        painter.setClipRect(QRectF(0, -100, split, 300));
        painter.setPen(Qt::red);
        painter.drawImage(QPointF(0, 0), rotatedInk);
    }
    qint64 rotatedError = 0, rotatedEnergy = 0;
    for (int y = 0; y < rotatedActual.height(); ++y)
        for (int x = 0; x < rotatedActual.width(); ++x) {
            rotatedError +=
                std::abs(qRed(rotatedActual.pixel(x, y)) - qRed(rotatedExpected.pixel(x, y)));
            rotatedEnergy += qRed(rotatedExpected.pixel(x, y));
        }
    qInfo() << "Rotated same-ink clip RGB error" << window.devicePixelRatio()
            << double(rotatedError) / rotatedEnergy;
    QVERIFY2(double(rotatedError) / rotatedEnergy < .16,
             "Clip moved into post-transform coordinates");
    auto empty = piece("empty", 0, 0, Qt::red);
    item.setPoses({empty});
    QCoreApplication::processEvents();
    const auto emptyFrame = window.grabWindow();
    qint64 emptyEnergy = 0;
    for (int y = 0; y < emptyFrame.height(); ++y)
        for (int x = 0; x < emptyFrame.width(); ++x)
            emptyEnergy += qRed(emptyFrame.pixel(x, y));
    QCOMPARE(emptyEnergy, 0);

    item.setPoses(
        {piece("left", 0, split, Qt::red), piece("right", split, width - split, Qt::blue)});
    QVERIFY2(item.errorString().isEmpty(), qPrintable(item.errorString()));
    QCoreApplication::processEvents();
    auto actual = window.grabWindow();
    QImage expected(actual.size(), QImage::Format_RGB32);
    expected.setDevicePixelRatio(window.devicePixelRatio());
    expected.fill(Qt::black);
    QTextLayout layout(text, font);
    layout.beginLayout();
    auto line = layout.createLine();
    line.setLineWidth(1000000);
    layout.endLayout();
    {
        QPainter painter(&expected);
        painter.translate(80, 40);
        painter.setClipRect(QRectF(0, -100, split, 300));
        painter.setPen(Qt::red);
        layout.draw(&painter, {});
        painter.setClipping(false);
        painter.setClipRect(QRectF(split, -100, width - split, 300));
        painter.setPen(Qt::blue);
        layout.draw(&painter, {});
    }
    qint64 error = 0, energy = 0;
    for (int y = 0; y < actual.height(); ++y)
        for (int x = 0; x < actual.width(); ++x) {
            const auto a = actual.pixel(x, y), b = expected.pixel(x, y);
            error += std::abs(qRed(a) - qRed(b)) + std::abs(qBlue(a) - qBlue(b));
            energy += qRed(b) + qBlue(b);
        }
    QVERIFY2(double(error) / energy < .16,
             qPrintable(QString("Clipped contextual RGB error %1").arg(double(error) / energy)));
    auto halo = piece("halo", 0, width / 2, Qt::transparent);
    halo["halos"] = QVariantList{QVariantMap{{"radius", 20}, {"color", "red"}}};
    item.setPoses({halo});
    QVERIFY(item.errorString().isEmpty());
    QCoreApplication::processEvents();
    actual = window.grabWindow();
    qint64 inside = 0, outside = 0;
    const double density = window.devicePixelRatio();
    for (int y = 0; y < actual.height(); ++y)
        for (int x = 0; x < actual.width(); ++x) {
            const auto value = qRed(actual.pixel(x, y));
            if (x >= 80 * density && x < (80 + width / 2) * density)
                inside += value;
            else
                outside += value;
        }
    QVERIFY(inside > 10000);
    QVERIFY2(outside < 1000, qPrintable(QString("Halo leaked outside clip: %1").arg(outside)));
    auto red = piece("red", -100, width + 200, Qt::red),
         blue = piece("blue", -100, width + 200, Qt::blue);
    item.setPoses({red, blue});
    QCoreApplication::processEvents();
    const auto blueOnTop = window.grabWindow();
    item.setPoses({blue, red});
    QCoreApplication::processEvents();
    const auto redOnTop = window.grabWindow();
    qint64 topBlue = 0, topRed = 0;
    for (int y = 0; y < actual.height(); ++y)
        for (int x = 0; x < actual.width(); ++x) {
            topBlue += qBlue(blueOnTop.pixel(x, y));
            topRed += qRed(redOnTop.pixel(x, y));
        }
    QVERIFY(topBlue > 10000 && topRed > 10000);
    QVERIFY(blueOnTop != redOnTop);
    QCOMPARE(item.maskRevision(), revision);
    QCOMPARE(item.textureUploads(), uploads);
    for (int frame = 0; frame < 120; ++frame) {
        auto moving = piece("left", frame * .1, width / 2, Qt::red);
        item.setPoses({moving, piece("right", width / 2, width / 2, Qt::blue)});
        QCoreApplication::processEvents();
    }
    QCOMPARE(item.maskRevision(), revision);
    QCOMPARE(item.textureUploads(), uploads);
}

void GlyphRunTest::sharedArabicClusterSupportsTwoClippedInstances()
{
    QQuickWindow window;
    window.setColor(Qt::black);
    window.setGeometry(0, 0, 300, 160);
    GlyphRun item(window.contentItem());
    item.setSize({300, 160});
    QFont font("Times New Roman");
    font.setPixelSize(72);
    item.setFont(font);
    const auto text = QString::fromUtf8("لا");
    item.setRuns({QVariantMap{{"key", "arabic"}, {"text", text}, {"maxBlur", 0}}});
    QVERIFY(item.errorString().isEmpty());
    const auto metric = item.metrics().first().toMap();
    const auto graphemes = metric.value("graphemes").toList();
    QCOMPARE(graphemes.size(), 2);
    QCOMPARE(graphemes[0].toMap().value("clusterStart").toInt(), 0);
    QCOMPARE(graphemes[1].toMap().value("clusterStart").toInt(), 0);
    const qreal width = metric.value("width").toDouble(), split = width / 2;
    item.setPoses(
        {QVariantMap{
             {"key", "arabic"},
             {"start", 0},
             {"instance", "left"},
             {"x", 50},
             {"y", 20},
             {"color", "red"},
             {"clip", QVariantMap{{"x", 0}, {"y", -100}, {"width", split}, {"height", 300}}}},
         QVariantMap{
             {"key", "arabic"},
             {"start", 0},
             {"instance", "right"},
             {"x", 50},
             {"y", 20},
             {"color", "blue"},
             {"clip",
              QVariantMap{{"x", split}, {"y", -100}, {"width", width - split}, {"height", 300}}}}});
    QVERIFY2(item.errorString().isEmpty(), qPrintable(item.errorString()));
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QTRY_VERIFY(item.textureUploads() > 0);
    auto actual = window.grabWindow();
    QImage expected(actual.size(), QImage::Format_RGB32);
    expected.setDevicePixelRatio(window.devicePixelRatio());
    expected.fill(Qt::black);
    QTextLayout layout(text, font);
    layout.beginLayout();
    auto line = layout.createLine();
    line.setLineWidth(1000000);
    layout.endLayout();
    {
        QPainter painter(&expected);
        painter.translate(50, 20);
        painter.setClipRect(QRectF(0, -100, split, 300));
        painter.setPen(Qt::red);
        layout.draw(&painter, {});
        painter.setClipping(false);
        painter.setClipRect(QRectF(split, -100, width - split, 300));
        painter.setPen(Qt::blue);
        layout.draw(&painter, {});
    }
    qint64 error = 0, energy = 0, red = 0, blue = 0;
    for (int y = 0; y < actual.height(); ++y)
        for (int x = 0; x < actual.width(); ++x) {
            const auto a = actual.pixel(x, y), b = expected.pixel(x, y);
            error += std::abs(qRed(a) - qRed(b)) + std::abs(qBlue(a) - qBlue(b));
            energy += qRed(b) + qBlue(b);
            red += qRed(a);
            blue += qBlue(a);
        }
    QVERIFY(red > 1000 && blue > 1000);
    QVERIFY2(double(error) / energy < .16,
             qPrintable(QString("Arabic shared-cluster error %1").arg(double(error) / energy)));
}

void GlyphRunTest::instanceAndClipRejectionsKeepTheAcceptedFrame()
{
    GlyphRun item;
    QFont font("Arial");
    font.setPixelSize(8);
    item.setFont(font);
    item.setRuns({QVariantMap{{"key", "many"}, {"text", QString(2048, 'i')}, {"maxBlur", 0}}});
    QVERIFY2(item.errorString().isEmpty(), qPrintable(item.errorString()));
    const QVariantList valid{QVariantMap{{"key", "many"}, {"instance", "one"}, {"color", "red"}}};
    item.setPoses(valid);
    QVERIFY(item.errorString().isEmpty());
    const auto revision = item.maskRevision();
    for (const auto &bad : {QVariantMap{{"x", 0}, {"y", 0}, {"width", -1}, {"height", 10}},
                            QVariantMap{{"x", 0}, {"y", 0}, {"width", 1000001}, {"height", 10}},
                            QVariantMap{{"x", std::numeric_limits<double>::quiet_NaN()},
                                        {"y", 0},
                                        {"width", 10},
                                        {"height", 10}}}) {
        item.setPoses({QVariantMap{{"key", "many"}, {"clip", bad}}});
        QVERIFY(!item.errorString().isEmpty());
        QCOMPARE(item.poses(), valid);
        QCOMPARE(item.maskRevision(), revision);
    }
    item.setPoses({QVariantMap{{"key", "many"}}, QVariantMap{{"key", "many"}}});
    QVERIFY(!item.errorString().isEmpty());
    QCOMPARE(item.poses(), valid);
    item.setPoses({valid.first(), valid.first()});
    QVERIFY(!item.errorString().isEmpty());
    QCOMPARE(item.poses(), valid);
    QVariantList three;
    for (int i = 0; i < 3; ++i)
        three.append(QVariantMap{{"key", "many"}, {"instance", QString::number(i)}});
    item.setPoses(three);
    QVERIFY2(item.errorString().isEmpty(),
             "Three complete 2048-fragment instances fit the explicit 8 MiB budget");
    item.setPoses(valid);
    item.setPoses({QVariantMap{{"key", "many"}, {"instance", 42}}});
    QVERIFY(!item.errorString().isEmpty());
    QCOMPARE(item.poses(), valid);
    item.setPoses({QVariantMap{{"key", "many"}, {"instance", QString(129, 'a')}}});
    QVERIFY(!item.errorString().isEmpty());
    QCOMPARE(item.poses(), valid);
    QVariantList expanded;
    for (int i = 0; i < 2048; ++i)
        expanded.append(QVariantMap{{"key", "many"}, {"instance", QString::number(i)}});
    item.setPoses(expanded);
    QVERIFY2(!item.errorString().isEmpty(),
             "Unbounded whole-run instance multiplication must reject");
    QCOMPARE(item.poses(), valid);
    QCOMPARE(item.maskRevision(), revision);
    item.setPoses({QVariantMap{
        {"key", "many"}, {"clip", QVariantMap{{"x", 0}, {"y", 0}, {"width", 0}, {"height", 0}}}}});
    QVERIFY(item.errorString().isEmpty());
}

void GlyphRunTest::measurementOnlyHasNoMasksAndTransitionsAtomically()
{
    QQuickWindow window;
    window.setGeometry(0, 0, 500, 300);
    GlyphRun item(window.contentItem());
    item.setSize({500, 300});
    item.setMeasurementOnly(true);
    QCOMPARE(item.measurementOnly(), true);
    QFont font("Arial");
    font.setPixelSize(32);
    item.setFont(font);
    const QVariantList oversized{
        QVariantMap{{"key", "measure"},
                    {"text", QString::fromUtf8("星 a office 👩‍🚀")},
                    {"size", 16384}}};
    item.setRuns(oversized);
    QVERIFY2(item.errorString().isEmpty(), qPrintable(item.errorString()));
    QVERIFY(item.metrics().first().toMap().value("width").toDouble() > 16384);
    QCOMPARE(item.cacheBytes(), 0);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QTest::qWait(30);
    window.grabWindow();
    QCOMPARE(item.cacheBytes(), 0);
    QCOMPARE(item.textureUploads(), 0);
    QCOMPARE(item.renderBatches(), 0);
    const auto revision = item.maskRevision();
    const auto metrics = item.metrics();
    item.setMeasurementOnly(false);
    QVERIFY(!item.errorString().isEmpty());
    QCOMPARE(item.measurementOnly(), true);
    QCOMPARE(item.runs(), oversized);
    QCOMPARE(item.metrics(), metrics);
    QCOMPARE(item.maskRevision(), revision);
    QCOMPARE(item.cacheBytes(), 0);
    item.setPoses({QVariantMap{{"key", "measure"}, {"x", 10}}});
    QVERIFY(!item.errorString().isEmpty());
    QVERIFY(item.poses().isEmpty());
    item.setRuns({QVariantMap{{"key", "measure"}, {"text", "W"}, {"size", 16385}}});
    QVERIFY(!item.errorString().isEmpty());
    QCOMPARE(item.runs(), oversized);
    item.setRuns({QVariantMap{{"key", "measure"}, {"text", QString(8193, 'a')}, {"size", 1000}}});
    QVERIFY(!item.errorString().isEmpty());
    QCOMPARE(item.runs(), oversized);
    item.setRuns({QVariantMap{{"key", "measure"}, {"text", "MW"}, {"size", 60}, {"maxBlur", 0}}});
    item.setMeasurementOnly(false);
    QVERIFY(item.errorString().isEmpty());
    QCOMPARE(item.measurementOnly(), false);
    item.setPoses({QVariantMap{{"key", "measure"}, {"color", "red"}}});
    QTRY_VERIFY(item.textureUploads() > 0);
    item.setMeasurementOnly(true);
    QCOMPARE(item.cacheBytes(), 0);
    QVERIFY(item.poses().isEmpty());
    QTRY_COMPARE(item.renderBatches(), 0);
    item.setVisible(false);
    item.setVisible(true);
    QCOMPARE(item.cacheBytes(), 0);
    item.setRuns(oversized);
    QVERIFY(item.errorString().isEmpty());
    QFont huge("Arial");
    huge.setPixelSize(16384);
    item.setFont(huge);
    QVERIFY(item.errorString().isEmpty());
    QCOMPARE(item.font(), huge);
    QCOMPARE(item.cacheBytes(), 0);
}

void GlyphRunTest::colorGlyphsKeepNativeRgbaAndOrdinaryFallbackTint()
{
    QQuickWindow window;
    window.setColor(Qt::black);
    window.setGeometry(0, 0, 700, 180);
    GlyphRun item(window.contentItem());
    item.setSize({700, 180});
    QFont font;
    font.setFamilies({"Arial", "Apple Color Emoji", "PingFang SC"});
    font.setPixelSize(64);
    item.setFont(font);
    const QString text = QString::fromUtf8("A👩‍🚀 é👨‍👩‍👧‍👦B");
    item.setRuns({QVariantMap{{"key", "mixed"}, {"text", text}, {"maxBlur", 0}}});
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QTRY_VERIFY(item.textureUploads() > 0);
    const auto revision = item.maskRevision(), uploads = item.textureUploads();
    for (const double opacity : {1., .4}) {
        item.setPoses({QVariantMap{
            {"key", "mixed"}, {"x", 30}, {"y", 20}, {"color", "red"}, {"opacity", opacity}}});
        QCoreApplication::processEvents();
        const auto actual = window.grabWindow();
        QImage expected(actual.size(), QImage::Format_RGB32);
        expected.setDevicePixelRatio(window.devicePixelRatio());
        expected.fill(Qt::black);
        QTextLayout layout(text, font);
        layout.beginLayout();
        auto line = layout.createLine();
        line.setLineWidth(1000000);
        layout.endLayout();
        {
            QPainter painter(&expected);
            painter.setOpacity(opacity);
            painter.setPen(Qt::red);
            layout.draw(&painter, {30, 20});
        }
        qint64 error = 0, energy = 0, coloredPixels = 0;
        for (int y = 0; y < actual.height(); ++y)
            for (int x = 0; x < actual.width(); ++x) {
                const QColor a(actual.pixel(x, y)), b(expected.pixel(x, y));
                error += std::abs(a.red() - b.red()) + std::abs(a.green() - b.green()) +
                         std::abs(a.blue() - b.blue());
                energy += b.red() + b.green() + b.blue();
                if (a.blue() > 20 && a.green() > 20 && a.red() > a.blue() + 5)
                    ++coloredPixels;
            }
        QVERIFY(coloredPixels > 20);
        QVERIFY2(
            double(error) / energy < .16,
            qPrintable(
                QString("Mixed color glyph RGB relative error: %1").arg(double(error) / energy)));
    }
    QCOMPARE(item.maskRevision(), revision);
    QCOMPARE(item.textureUploads(), uploads);
}

void GlyphRunTest::largeHaloBudgetsKeepEnergyMomentsAndFrameCache()
{
    GlyphRun validator;
    validator.setRuns(
        {QVariantMap{{"key", "high"}, {"text", "M"}, {"maxBlur", 0}, {"maxHaloRadius", 1024}}});
    QVERIFY2(validator.errorString().isEmpty(), qPrintable(validator.errorString()));
    QCOMPARE(validator.supportedMaxHaloRadius(), 1024);
    QQuickWindow window;
    window.setColor(Qt::black);
    window.setMinimumSize({1024, 1024});
    window.setGeometry(0, 0, 1024, 1024);
    GlyphRun item(window.contentItem());
    item.setSize({1024, 1024});
    QFont font;
    font.setFamilies({"Helvetica Neue", "Inter", "Noto Sans CJK SC", "Noto Sans JP",
                      "Source Han Sans SC", "PingFang SC", "Hiragino Sans GB", "Microsoft YaHei",
                      "Helvetica Neue", "Arial", "sans-serif"});
    font.setPixelSize(152);
    font.setWeight(QFont::Weight(780));
    item.setFont(font);
    item.setMaximumRasterScale(2.24);
    item.setHaloSampling("canvas");
    item.setRuns({QVariantMap{{"key", "high"},
                              {"text", QString::fromUtf8("星")},
                              {"size", 151.54071925781247},
                              {"maxBlur", 0},
                              {"maxHaloRadius", 512}}});
    QVERIFY2(item.errorString().isEmpty(), qPrintable(item.errorString()));
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QTRY_VERIFY_WITH_TIMEOUT(!item.preparing(), 10000);
    QVERIFY2(item.errorString().isEmpty(), qPrintable(item.errorString()));
    QTRY_VERIFY_WITH_TIMEOUT(item.ready(), 10000);
    QTRY_VERIFY(item.textureUploads() > 0);
    const auto revision = item.maskRevision(), uploads = item.textureUploads();
    const auto metric = item.metrics().first().toMap();
    const double dpr = window.devicePixelRatio();
    item.setPoses({QVariantMap{
        {"key", "high"},
        {"x", 512},
        {"y", 512 - metric.value("baseline").toDouble() +
                  .22 *
                      (metric.value("emAscent").toDouble() - metric.value("emDescent").toDouble()) /
                      2},
        {"scale", .22},
        {"color", QColor::fromRgbF(1, 1, 1, .756)}}});
    QCoreApplication::processEvents();
    const auto bodyOnly = window.grabWindow();
    QCOMPARE(bodyOnly.size(), QSize(qRound(1024 * dpr), qRound(1024 * dpr)));
    double bodyMass = 0;
    for (int y = 0; y < bodyOnly.height(); ++y)
        for (int x = 0; x < bodyOnly.width(); ++x)
            bodyMass += qRed(bodyOnly.pixel(x, y));
    qInfo() << "Native unshadowed body" << dpr << bodyMass;
    QTextLayout inspect(QString::fromUtf8("星"), font);
    inspect.beginLayout();
    auto inspectLine = inspect.createLine();
    inspectLine.setLineWidth(1000000);
    inspect.endLayout();
    for (const auto &g : inspect.glyphRuns())
        qInfo() << "Native base-font shaping reference" << g.rawFont().familyName()
                << g.rawFont().weight() << g.rawFont().pixelSize() << "metrics" << metric;

    QVector<double> measuredMass, measuredSigma;
    // Chrome 156.0.8078.73, default getContext("2d"), GraphiteDawnMetal.
    // Controlled source primitive projection: font151.540719px/weight780,
    // white fill .756, white shadow, CTM(.22*dpr,512*dpr), raw blur 127.8/170.4/213.
    // This retains the fractional primitive inputs; the complete live Fume stage
    // rounds shadowBlur to whole pixels and has its own independent source oracle.
    // shadowBlur is twice sigma and does not follow the CTM. Provenance and the
    // rejected willReadFrequently:true CPU values are preserved in
    // tests/fixtures/glyph-run/folia-default-canvas-halo.json.
    const double referenceEnergy[2][3] = {{211305, 239852, 243720}, {811642, 840944, 740731}};
    const double referenceSigma[2][3] = {
        {40.706573261671394, 55.31905957300954, 62.094425289832934},
        {23.529131864391168, 30.27929371534908, 33.022507487145376}};
    const int densityIndex = qRound(dpr) - 1;
    QVERIFY(densityIndex >= 0 && densityIndex < 2);
    for (int index = 0; index < 3; ++index) {
        const double gain = .6 + index * .2;
        const double radius = 213. * gain / (2 * .22 * dpr);
        const double baseline = metric.value("baseline").toDouble(),
                     middle = (metric.value("emAscent").toDouble() -
                               metric.value("emDescent").toDouble()) /
                              2;
        item.setPoses({QVariantMap{
            {"key", "high"},
            {"x", 512},
            {"y", 512 - baseline + .22 * middle},
            {"scale", .22},
            {"color", QColor::fromRgbF(1, 1, 1, .756)},
            {"halos", QVariantList{QVariantMap{
                          {"radius", radius}, {"opacity", .756}, {"color", "white"}}}}}});
        QVERIFY2(item.errorString().isEmpty(), qPrintable(item.errorString()));
        QCoreApplication::processEvents();
        const auto image = window.grabWindow();
        QCOMPARE(image.size(), QSize(qRound(1024 * dpr), qRound(1024 * dpr)));
        double mass = 0, first = 0, second = 0;
        for (int y = 0; y < image.height(); ++y)
            for (int x = 0; x < image.width(); ++x) {
                const double value = qRed(image.pixel(x, y));
                mass += value;
                first += x * value;
                second += x * x * value;
            }
        const double sigma = std::sqrt(second / mass - std::pow(first / mass, 2)) / dpr;
        qInfo() << "Chrome source halo" << dpr << gain << "native energy" << mass << "reference"
                << referenceEnergy[densityIndex][index] << "sigma" << sigma << "reference"
                << referenceSigma[densityIndex][index];
        measuredMass.append(mass);
        measuredSigma.append(sigma);
    }
    for (int index = 0; index < 3; ++index) {
        QVERIFY2(std::abs(measuredMass[index] / referenceEnergy[densityIndex][index] - 1) < .15,
                 "Source Canvas energy differs by more than 15%");
        QVERIFY2(std::abs(measuredSigma[index] / referenceSigma[densityIndex][index] - 1) < .15,
                 "Source Canvas second moment differs by more than 15%");
    }
    for (int frame = 0; frame < 120; ++frame) {
        item.setPoses({QVariantMap{
            {"key", "high"},
            {"x", 512},
            {"y", 400},
            {"scale", .22},
            {"color", "blue"},
            {"halos", QVariantList{QVariantMap{
                          {"radius", 100. + frame * 3.}, {"color", "red"}, {"opacity", .8}}}}}});
        QCoreApplication::processEvents();
        const auto frameImage = window.grabWindow();
        QCOMPARE(frameImage.size(), QSize(qRound(1024 * dpr), qRound(1024 * dpr)));
        QVERIFY2(item.errorString().isEmpty(), qPrintable(item.errorString()));
        QVERIFY(item.ready());
    }
    QCOMPARE(item.maskRevision(), revision);
    QCOMPARE(item.textureUploads(), uploads);
    QVERIFY(item.cacheBytes() <= 64LL * 1024 * 1024);
    item.setPoses(
        {QVariantMap{{"key", "high"}, {"halos", QVariantList{QVariantMap{{"radius", 513}}}}}});
    QVERIFY(!item.errorString().isEmpty());
    // Static's precomputed halo atlas retains its original capacity rejection.
    // Canvas has a different bounded allocation path and asynchronous failures;
    // it must not be used to assert a synchronous static-atlas rejection.
    const auto accepted = validator.runs();
    validator.setRuns({QVariantMap{
        {"key", "tooMuch"}, {"text", QString(200, 'W')}, {"size", 100}, {"maxHaloRadius", 1024}}});
    QVERIFY(!validator.errorString().isEmpty());
    QCOMPARE(validator.runs(), accepted);
    item.setVisible(false);
    QTRY_COMPARE(item.cacheBytes(), 0);
}

void GlyphRunTest::paragraphsUseNativeWrapBidiAndBaselineOffsets()
{
    GlyphRun item;
    QFont font;
    font.setFamilies({"Arial", "PingFang SC", "Apple Color Emoji"});
    font.setPixelSize(24);
    item.setFont(font);
    const QString text = QString::fromUtf8(
        "office سلام 中文👨‍👩‍👧‍👦\nsecond\n\nverylongsoftwordwithoutspaces");
    const QVariantList runs{QVariantMap{
        {"key", "p"}, {"text", text}, {"maxBlur", 0}, {"maxWidth", 145}, {"lineHeight", 38}}};
    item.setRuns(runs);
    QVERIFY2(item.errorString().isEmpty(), qPrintable(item.errorString()));
    auto shapedText = text;
    shapedText.replace('\n', QChar(0x2028));
    QTextLayout layout(shapedText, font);
    QTextOption option;
    option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    option.setFlags(QTextOption::IncludeTrailingSpaces);
    layout.setTextOption(option);
    layout.beginLayout();
    QList<QTextLine> lines;
    qreal width = 0, y = 0;
    while (true) {
        auto line = layout.createLine();
        if (!line.isValid())
            break;
        line.setLineWidth(145);
        line.setPosition({0, y});
        y += 38;
        width = std::max(width, line.naturalTextWidth());
        lines.append(line);
    }
    layout.endLayout();
    const auto metric = item.metrics().first().toMap();
    QCOMPARE(metric.value("lineCount").toInt(), lines.size());
    QVERIFY(lines.size() > 5);
    QVERIFY(std::abs(metric.value("height").toDouble() - y) < .01);
    QVERIFY(std::abs(metric.value("width").toDouble() - width) < .01);
    const auto graphemes = metric.value("graphemes").toList();
    for (const auto &value : graphemes) {
        const auto g = value.toMap();
        const auto line = layout.lineForTextPosition(g.value("start").toInt());
        QVERIFY(line.isValid());
        QVERIFY(std::abs(g.value("x").toDouble() - line.cursorToX(g.value("start").toInt())) < .01);
        QVERIFY(std::abs(g.value("y").toDouble() -
                         (line.y() + line.ascent() - lines.first().ascent())) < .01);
    }
    const auto revision = item.maskRevision();
    for (const auto &bad :
         {QVariantMap{{"maxWidth", 0}}, QVariantMap{{"maxWidth", 4097}},
          QVariantMap{{"lineHeight", 0}}, QVariantMap{{"lineHeight", 513}},
          QVariantMap{{"lineHeight", std::numeric_limits<double>::quiet_NaN()}}}) {
        auto invalid = runs.first().toMap();
        for (auto it = bad.begin(); it != bad.end(); ++it)
            invalid[it.key()] = it.value();
        item.setRuns({invalid});
        QVERIFY(!item.errorString().isEmpty());
        QCOMPARE(item.maskRevision(), revision);
        QCOMPARE(item.runs(), runs);
    }
    item.setRuns(oneRun("old behavior"));
    QVERIFY(item.errorString().isEmpty());
    QCOMPARE(item.metrics().first().toMap().value("lineCount").toInt(), 1);
    for (const auto &g : item.metrics().first().toMap().value("graphemes").toList())
        QCOMPARE(g.toMap().value("y").toDouble(), 0);
    for (const auto &sample : {QString(), QString("a\n"), QString("a\r\nb"), QString("a\rb"),
                               QString::fromUtf8("a\xe2\x80\xa9"
                                                 "b")}) {
        item.setRuns(
            {QVariantMap{{"key", "p"}, {"text", sample}, {"maxWidth", 145}, {"maxBlur", 0}}});
        QVERIFY2(item.errorString().isEmpty(), qPrintable(item.errorString()));
        QCOMPARE(item.metrics().first().toMap().value("lineCount").toInt(),
                 sample.isEmpty() ? 1 : 2);
    }
}

void GlyphRunTest::paragraphRenderingAndClusterPosesShareNativeLineCoordinates()
{
    QQuickWindow window;
    window.setColor(Qt::black);
    window.setGeometry(0, 0, 640, 480);
    GlyphRun item(window.contentItem());
    item.setSize({640, 480});
    QFont font;
    font.setFamilies({"Arial", "PingFang SC", "Apple Color Emoji"});
    font.setPixelSize(26);
    item.setFont(font);
    const QString text =
        QString::fromUtf8("中文 office سلام\r\nA👩‍🚀é\n最后一行");
    item.setRuns({QVariantMap{
        {"key", "p"}, {"text", text}, {"maxBlur", 0}, {"maxWidth", 200}, {"lineHeight", 42}}});
    QVERIFY2(item.errorString().isEmpty(), qPrintable(item.errorString()));
    item.setPoses({QVariantMap{{"key", "p"}, {"x", 40}, {"y", 30}, {"color", "white"}}});
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QTRY_VERIFY(item.textureUploads() > 0);
    const auto whole = window.grabWindow();
    QImage expected(whole.size(), QImage::Format_RGB32);
    expected.setDevicePixelRatio(window.devicePixelRatio());
    expected.fill(Qt::black);
    auto normalized = text;
    normalized.replace('\r', QChar(0x2060));
    normalized.replace('\n', QChar(0x2028));
    QTextLayout reference(normalized, font);
    QTextOption option;
    option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    option.setFlags(QTextOption::IncludeTrailingSpaces);
    reference.setTextOption(option);
    reference.beginLayout();
    qreal lineY = 0;
    while (true) {
        auto line = reference.createLine();
        if (!line.isValid())
            break;
        line.setLineWidth(200);
        line.setPosition({0, lineY});
        lineY += 42;
    }
    reference.endLayout();
    {
        QPainter painter(&expected);
        painter.setPen(Qt::white);
        reference.draw(&painter, {40, 30});
    }
    qint64 error = 0, expectedInk = 0;
    for (int y = 0; y < whole.height(); ++y)
        for (int x = 0; x < whole.width(); ++x) {
            error += std::abs(qRed(whole.pixel(x, y)) - qRed(expected.pixel(x, y)));
            expectedInk += qRed(expected.pixel(x, y));
        }
    QVERIFY(expectedInk > 10000);
    QVERIFY2(
        double(error) / expectedInk < .16,
        qPrintable(
            QString("Paragraph relative coverage error: %1").arg(double(error) / expectedInk)));
    QVariantList poses;
    QSet<int> seen;
    for (const auto &value : item.metrics().first().toMap().value("graphemes").toList()) {
        const int start = value.toMap().value("clusterStart").toInt();
        if (!seen.contains(start)) {
            seen.insert(start);
            poses.append(QVariantMap{
                {"key", "p"}, {"start", start}, {"x", 40}, {"y", 30}, {"color", "white"}});
        }
    }
    item.setPoses(poses);
    QVERIFY2(item.errorString().isEmpty(), qPrintable(item.errorString()));
    QTest::qWait(40);
    const auto individual = window.grabWindow();
    QCOMPARE(individual, whole);
    const auto revision = item.maskRevision(), uploads = item.textureUploads();
    for (int frame = 0; frame < 120; ++frame) {
        auto pose = poses.first().toMap();
        pose["y"] = 30 + frame % 4;
        poses[0] = pose;
        item.setPoses(poses);
        QCoreApplication::processEvents();
    }
    QCOMPARE(item.maskRevision(), revision);
    QCOMPARE(item.textureUploads(), uploads);
    QVERIFY(item.cacheBytes() <= 64LL * 1024 * 1024);
    item.setVisible(false);
    QTRY_COMPARE(item.cacheBytes(), 0);
}

static QVariantList oneRun(const QString &text)
{
    return {QVariantMap{{"key", "line"}, {"text", text}}};
}

void GlyphRunTest::shapesWholeRunsWithTheCompleteFontChain()
{
    GlyphRun item;
    QFont font;
    font.setFamilies({QGuiApplication::font().family(), "Arial", "PingFang SC"});
    font.setPixelSize(48);
    font.setWeight(QFont::DemiBold);
    item.setFont(font);
    const QString text = QString::fromUtf8("سلام office 中文 é");
    item.setRuns(oneRun(text));
    QCOMPARE(item.font().families(), font.families());
    QVERIFY2(item.errorString().isEmpty(), qPrintable(item.errorString()));
    QCOMPARE(item.metrics().size(), 1);
    QTextLayout layout(text, font);
    layout.beginLayout();
    auto line = layout.createLine();
    line.setLineWidth(1000000);
    layout.endLayout();
    const auto metric = item.metrics().first().toMap();
    QVERIFY(std::abs(metric.value("width").toDouble() - line.naturalTextWidth()) < .01);
    QVERIFY(std::abs(metric.value("baseline").toDouble() - line.ascent()) < .01);
    QVERIFY(!metric.value("graphemes").toList().isEmpty());
    QVERIFY(item.cacheBytes() > 0);
    item.setRuns({QVariantMap{{"key", "override"},
                              {"text", text},
                              {"size", 24},
                              {"weight", 700},
                              {"spacing", 1.3},
                              {"maxBlur", 8}}});
    QFont overrideFont = font;
    overrideFont.setPixelSize(24);
    overrideFont.setWeight(QFont::Bold);
    overrideFont.setLetterSpacing(QFont::AbsoluteSpacing, 1.3);
    QTextLayout overrideLayout(text, overrideFont);
    overrideLayout.beginLayout();
    auto overrideLine = overrideLayout.createLine();
    overrideLine.setLineWidth(1000000);
    overrideLayout.endLayout();
    QVERIFY(std::abs(item.metrics().first().toMap().value("width").toDouble() -
                     overrideLine.naturalTextWidth()) < .01);
    QCOMPARE(item.font().families(), font.families());
}

void GlyphRunTest::metricsKeepGraphemesAndLigaturesIntact()
{
    GlyphRun item;
    const QString text = QString::fromUtf8("A👨‍👩‍👧‍👦é office سلام");
    item.setRuns(oneRun(text));
    const auto graphemes = item.metrics().first().toMap().value("graphemes").toList();
    QTextBoundaryFinder finder(QTextBoundaryFinder::Grapheme, text);
    int start = 0, index = 0;
    while (finder.toNextBoundary() > 0) {
        const int end = finder.position();
        const auto metric = graphemes[index++].toMap();
        QCOMPARE(metric.value("start").toInt(), start);
        QCOMPARE(metric.value("length").toInt(), end - start);
        QVERIFY(metric.value("clusterStart").toInt() <= start);
        QVERIFY(std::isfinite(metric.value("advance").toDouble()));
        start = end;
    }
    QCOMPARE(index, graphemes.size());
}

void GlyphRunTest::rejectsInputsAtomically()
{
    GlyphRun item;
    item.setRuns(oneRun("valid"));
    const auto revision = item.maskRevision();
    const auto original = item.runs();
    item.setRuns(oneRun(QString(GlyphRun::MaxTextUnits + 1, QLatin1Char('a'))));
    QVERIFY(!item.errorString().isEmpty());
    QCOMPARE(item.maskRevision(), revision);
    QCOMPARE(item.runs(), original);
    QVariantList tooMany;
    for (int index = 0; index <= GlyphRun::MaxRuns; ++index)
        tooMany.append(QVariantMap{{"key", QString::number(index)}, {"text", "a"}});
    item.setRuns(tooMany);
    QVERIFY(!item.errorString().isEmpty());
    QCOMPARE(item.runs(), original);
    item.setRuns(oneRun("first\nsecond"));
    QVERIFY(!item.errorString().isEmpty());
    QCOMPARE(item.runs(), original);
    item.setRuns({QVariantMap{{"key", "large"}, {"text", QString(100, 'W')}, {"size", 256}}});
    QVERIFY(!item.errorString().isEmpty());
    QCOMPARE(item.runs(), original);
    QCOMPARE(item.maskRevision(), revision);
    item.setRuns(original);
    QVERIFY(item.errorString().isEmpty());
    item.setRuns({QVariantMap{{"key", "duplicate"}, {"text", "a"}},
                  QVariantMap{{"key", "duplicate"}, {"text", "b"}}});
    QVERIFY(!item.errorString().isEmpty());
    QCOMPARE(item.runs(), original);
    item.setPoses({QVariantMap{{"key", "line"}, {"x", std::numeric_limits<double>::quiet_NaN()}}});
    QVERIFY(!item.errorString().isEmpty());
    QVERIFY(item.poses().isEmpty());
    item.setRuns(oneRun(QString::fromUtf8("👩‍🚀")));
    item.setPoses({QVariantMap{{"key", "line"}, {"start", 1}}});
    QVERIFY(!item.errorString().isEmpty());
    item.setRuns({});
    QVERIFY(item.metrics().isEmpty());
    QCOMPARE(item.cacheBytes(), 0);
}

void GlyphRunTest::renderedInkMatchesContextualShaping()
{
    QQuickWindow window;
    window.setColor(Qt::black);
    window.setGeometry(0, 0, 640, 160);
    GlyphRun item(window.contentItem());
    item.setSize({640, 160});
    QFont font = QGuiApplication::font();
    font.setPixelSize(40);
    item.setFont(font);
    const QString text = QString::fromUtf8("office سلام 中文 é");
    item.setRuns(oneRun(text));
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QTRY_VERIFY_WITH_TIMEOUT(item.textureUploads() > 0, 5000);
    const auto actual = window.grabWindow().convertToFormat(QImage::Format_RGB32);
    QVERIFY(!actual.isNull());
    QImage expected(actual.size(), QImage::Format_RGB32);
    expected.setDevicePixelRatio(window.devicePixelRatio());
    expected.fill(Qt::black);
    QTextLayout layout(text, font);
    layout.beginLayout();
    auto line = layout.createLine();
    line.setLineWidth(1000000);
    layout.endLayout();
    {
        QPainter painter(&expected);
        painter.setPen(Qt::white);
        layout.draw(&painter, {});
    }
    qint64 error = 0, expectedInk = 0, actualInk = 0;
    for (int y = 0; y < actual.height(); ++y) {
        for (int x = 0; x < actual.width(); ++x) {
            const int a = qRed(actual.pixel(x, y)), b = qRed(expected.pixel(x, y));
            error += std::abs(a - b);
            actualInk += a;
            expectedInk += b;
        }
    }
    QVERIFY(actualInk > 10000);
    QVERIFY(expectedInk > 10000);
    // Hardware and QPainter coverage can differ, but dropped contextual glyphs,
    // duplicate ligatures and misplaced baselines have much larger ink error.
    QVERIFY2(double(error) / expectedInk < .16,
             qPrintable(QString("Relative coverage error: %1").arg(double(error) / expectedInk)));
}

void GlyphRunTest::framePosesKeepMasksAndTextures()
{
    QQuickWindow window;
    window.setGeometry(0, 0, 320, 200);
    GlyphRun item(window.contentItem());
    item.setWidth(320);
    item.setHeight(200);
    item.setRuns(oneRun("Frame stable"));
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QTRY_VERIFY_WITH_TIMEOUT(item.textureUploads() > 0, 5000);
    const auto revision = item.maskRevision(), uploads = item.textureUploads();
    for (int frame = 0; frame < 120; ++frame) {
        item.setPoses({QVariantMap{{"key", "line"},
                                   {"x", frame * .25},
                                   {"scale", 1. + frame * .001},
                                   {"rotation", frame * .1},
                                   {"opacity", .8},
                                   {"blur", 3.},
                                   {"glow", .7},
                                   {"color", QColor(Qt::cyan)}}});
        QCoreApplication::processEvents();
    }
    window.grabWindow();
    QCOMPARE(item.maskRevision(), revision);
    QCOMPARE(item.textureUploads(), uploads);
    QVERIFY(item.renderBatches() > 0);
    QVERIFY(item.renderBatches() <= GlyphRun::MaxPages);
}

void GlyphRunTest::blurTwentyPixelsExceedsTheSmallFontRadius()
{
    QQuickWindow window;
    window.setColor(Qt::black);
    window.setGeometry(0, 0, 512, 256);
    GlyphRun item(window.contentItem());
    item.setSize({512, 256});
    QFont font = QGuiApplication::font();
    font.setPixelSize(28);
    item.setFont(font);
    item.setRuns(oneRun("H"));
    QCOMPARE(item.supportedMaxBlur(), 32);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QTRY_VERIFY_WITH_TIMEOUT(item.textureUploads() > 0, 5000);
    const auto uploads = item.textureUploads(), revision = item.maskRevision();
    auto variance = [&](double blur) {
        item.setPoses({QVariantMap{{"key", "line"}, {"x", 220.}, {"y", 100.}, {"blur", blur}}});
        QCoreApplication::processEvents();
        const auto image = window.grabWindow();
        double mass = 0, first = 0, second = 0;
        for (int y = 0; y < image.height(); ++y) {
            for (int x = 0; x < image.width(); ++x) {
                const double ink = qRed(image.pixel(x, y));
                mass += ink;
                first += x * ink;
                second += x * x * ink;
            }
        }
        return mass > 0 ? (second / mass - std::pow(first / mass, 2)) /
                              std::pow(window.devicePixelRatio(), 2)
                        : 0.;
    };
    const double narrow = variance(8), broad = variance(20);
    QVERIFY(narrow > 0);
    QVERIFY2(broad > narrow * 1.8, qPrintable(QString("8px=%1, 20px=%2").arg(narrow).arg(broad)));
    QCOMPARE(item.maskRevision(), revision);
    QCOMPARE(item.textureUploads(), uploads);
    const auto poses = item.poses();
    item.setPoses({QVariantMap{{"key", "line"}, {"blur", 33.}}});
    QVERIFY(!item.errorString().isEmpty());
    QCOMPARE(item.poses(), poses);
}

void GlyphRunTest::hiddenAndDestroyedReleaseResources()
{
    QQuickWindow window;
    window.setGeometry(0, 0, 320, 200);
    auto *item = new GlyphRun(window.contentItem());
    item->setSize({320, 200});
    item->setRuns(oneRun("Release resources"));
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QTRY_VERIFY_WITH_TIMEOUT(item->textureUploads() > 0, 5000);
    item->setVisible(false);
    QCOMPARE(item->cacheBytes(), 0);
    item->setVisible(true);
    QVERIFY(item->cacheBytes() > 0);
    item->setPoses({QVariantMap{{"key", "line"}, {"x", 15.}, {"color", "#ff0000"}}});
    QVERIFY2(item->errorString().isEmpty(), qPrintable(item->errorString()));
    const auto poses = item->poses();
    item->setVisible(false);
    item->setVisible(true);
    QCOMPARE(item->poses(), poses);
    window.hide();
    QCOMPARE(item->cacheBytes(), 0);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QVERIFY(item->cacheBytes() > 0);
    QCOMPARE(item->poses(), poses);
    QPointer<GlyphRun> guard(item);
    delete item;
    QVERIFY(guard.isNull());
    window.grabWindow();
}

void GlyphRunTest::largeIndependentHalosKeepTheirRadiusAndFrameCache()
{
    GlyphRun limits;
    QCOMPARE(limits.property("supportedMaxHaloRadius").toInt(), 1024);
    QQuickWindow window;
    window.setColor(Qt::black);
    window.setGeometry(0, 0, 1000, 1000);
    GlyphRun item(window.contentItem());
    item.setSize({1000, 1000});
    item.setRuns({QVariantMap{
        {"key", "halo"}, {"text", "H"}, {"size", 72}, {"maxBlur", 0}, {"maxHaloRadius", 160}}});
    QVERIFY2(item.errorString().isEmpty(), qPrintable(item.errorString()));
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QTRY_VERIFY_WITH_TIMEOUT(item.textureUploads() > 0, 5000);
    auto haloPose = [](double radius, const QColor &color) {
        const QVariantMap layer{{"radius", radius}, {"opacity", 1.}, {"color", color}};
        return QVariantList{QVariantMap{{"key", "halo"},
                                        {"x", 460.},
                                        {"y", 420.},
                                        {"color", QColor(Qt::transparent)},
                                        {"halos", QVariantList{layer}}}};
    };
    auto measure = [&](double radius) {
        item.setPoses(haloPose(radius, Qt::white));
        QCoreApplication::processEvents();
        const auto image = window.grabWindow();
        double mass = 0, first = 0, second = 0;
        for (int y = 0; y < image.height(); ++y)
            for (int x = 0; x < image.width(); ++x) {
                const double coverage = qRed(image.pixel(x, y));
                mass += coverage;
                first += x * coverage;
                second += x * x * coverage;
            }
        return mass > 0 ? (second / mass - std::pow(first / mass, 2)) /
                              std::pow(window.devicePixelRatio(), 2)
                        : 0.;
    };
    const auto uploads = item.textureUploads(), revision = item.maskRevision();
    const double small = measure(40), large = measure(158.4);
    QVERIFY2(large > 10000 && large > small * 4,
             qPrintable(QString("40px variance=%1, 158.4px variance=%2").arg(small).arg(large)));
    for (int frame = 0; frame < 120; ++frame) {
        item.setPoses(haloPose(40 + frame * .5, QColor(Qt::red)));
        QCoreApplication::processEvents();
    }
    const auto redImage = window.grabWindow();
    qint64 red = 0, green = 0;
    for (int y = 0; y < redImage.height(); ++y)
        for (int x = 0; x < redImage.width(); ++x) {
            red += qRed(redImage.pixel(x, y));
            green += qGreen(redImage.pixel(x, y));
        }
    QVERIFY(red > 10000);
    QVERIFY(green < red * .01);
    QCOMPARE(item.maskRevision(), revision);
    QCOMPARE(item.textureUploads(), uploads);
    const QVariantList layers{
        QVariantMap{{"radius", 14.}, {"opacity", 1.}, {"color", QColor(Qt::red)}},
        QVariantMap{{"radius", 40.}, {"opacity", 1.}, {"color", QColor(Qt::green)}},
        QVariantMap{{"radius", 158.4}, {"opacity", 1.}, {"color", QColor(Qt::blue)}}};
    item.setPoses({QVariantMap{{"key", "halo"},
                               {"x", 460.},
                               {"y", 420.},
                               {"color", QColor(Qt::transparent)},
                               {"halos", layers}}});
    const auto colored = window.grabWindow();
    qint64 channels[3] = {};
    for (int y = 0; y < colored.height(); ++y)
        for (int x = 0; x < colored.width(); ++x) {
            const auto pixel = colored.pixel(x, y);
            channels[0] += qRed(pixel);
            channels[1] += qGreen(pixel);
            channels[2] += qBlue(pixel);
        }
    QVERIFY(channels[0] > 10000 && channels[1] > 10000 && channels[2] > 10000);
    const auto accepted = item.poses();
    item.setPoses(haloPose(161, Qt::white));
    QVERIFY(!item.errorString().isEmpty());
    QCOMPARE(item.poses(), accepted);
    const auto run = item.runs();
    item.setRuns({QVariantMap{{"key", "over-budget"},
                              {"text", QString(500, 'H')},
                              {"size", 72},
                              {"maxBlur", 0},
                              {"maxHaloRadius", 160}}});
    QVERIFY(!item.errorString().isEmpty());
    QCOMPARE(item.runs(), run);
    QVERIFY(item.cacheBytes() <= qint64(64) * 1024 * 1024);
    item.setVisible(false);
    QCOMPARE(item.cacheBytes(), 0);
}

void GlyphRunTest::retinaLongLinesKeepClearInkAndBoundedEffects()
{
    // DPI is fixed before QGuiApplication is created, including on a 1x CI screen.
    if (!qEnvironmentVariableIsSet("ECHO_GLYPH_RETINA_CHILD")) {
        QProcess child;
        auto environment = QProcessEnvironment::systemEnvironment();
        environment.insert("ECHO_GLYPH_RETINA_CHILD", "1");
        environment.insert("QT_SCALE_FACTOR", "2");
        child.setProcessEnvironment(environment);
        child.start(QCoreApplication::applicationFilePath(),
                    {"retinaLongLinesKeepClearInkAndBoundedEffects", "-o", "-,txt"});
        QVERIFY(child.waitForFinished(60000));
        QVERIFY2(child.exitStatus() == QProcess::NormalExit && child.exitCode() == 0,
                 child.readAllStandardOutput().constData());
        return;
    }
    QQuickWindow window;
    window.setColor(Qt::black);
    window.setGeometry(0, 0, 1800, 260);
    QVERIFY(window.effectiveDevicePixelRatio() >= 2);
    GlyphRun item(window.contentItem());
    item.setSize(window.size());
    QFont font;
    font.setFamilies({"Arial", "PingFang SC"});
    font.setPixelSize(66);
    font.setWeight(QFont::Bold);
    item.setFont(font);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    const QStringList texts{QString::fromUtf8("今天我们一起去海边看日落再沿着长长的街道慢慢走吧"),
                            QString::fromUtf8("今晚 office 中文 lyric 海边 é!")};
    QCOMPARE(texts.first().size(), 24);
    for (const auto &text : texts) {
        item.setRuns({QVariantMap{{"key", "line"},
                                  {"text", text},
                                  {"size", 66},
                                  {"weight", 700},
                                  {"maxBlur", 32},
                                  {"maxHaloRadius", 160}}});
        QVERIFY2(item.errorString().isEmpty(), qPrintable(item.errorString()));
        QCOMPARE(item.metrics().size(), 1);
        item.setPoses({QVariantMap{{"key", "line"}, {"x", 30.}, {"y", 80.}}});
        const auto actual = window.grabWindow().convertToFormat(QImage::Format_RGB32);
        QImage expected(actual.size(), QImage::Format_RGB32);
        expected.setDevicePixelRatio(window.effectiveDevicePixelRatio());
        expected.fill(Qt::black);
        QTextLayout layout(text, font);
        layout.beginLayout();
        auto line = layout.createLine();
        line.setLineWidth(1000000);
        layout.endLayout();
        {
            QPainter painter(&expected);
            painter.setPen(Qt::white);
            layout.draw(&painter, {30, 80});
        }
        qint64 error = 0, expectedInk = 0, actualInk = 0;
        for (int y = 0; y < actual.height(); ++y)
            for (int x = 0; x < actual.width(); ++x) {
                const int a = qRed(actual.pixel(x, y)), b = qRed(expected.pixel(x, y));
                error += std::abs(a - b);
                actualInk += a;
                expectedInk += b;
            }
        QVERIFY(actualInk > 100000);
        QVERIFY2(
            expectedInk > 0 && double(error) / expectedInk < .16,
            qPrintable(
                QString("Retina clear-ink coverage error=%1").arg(double(error) / expectedInk)));
        QVERIFY(item.cacheBytes() <= qint64(64) * 1024 * 1024);
        const auto revision = item.maskRevision(), uploads = item.textureUploads();
        for (int frame = 0; frame < 120; ++frame) {
            item.setPoses(
                {QVariantMap{{"key", "line"},
                             {"x", 30. + frame * .01},
                             {"y", 80.},
                             {"blur", 20.},
                             {"halos", QVariantList{QVariantMap{{"radius", frame % 2 ? 158.4 : 40.},
                                                                {"color", QColor(Qt::cyan)}}}}}});
            QCoreApplication::processEvents();
        }
        window.grabWindow();
        QVERIFY2(item.errorString().isEmpty(), qPrintable(item.errorString()));
        QCOMPARE(item.maskRevision(), revision);
        QCOMPARE(item.textureUploads(), uploads);
    }
}

void GlyphRunTest::metricsReportActualRunInkRelativeToTheBaseline()
{
    GlyphRun item;
    QFont font;
    font.setFamilies({"Arial", "PingFang SC", "Apple Color Emoji"});
    font.setPixelSize(64);
    item.setFont(font);
    QCOMPARE(item.font().families(), font.families());
    const QStringList texts{QStringLiteral("Agj"),
                            QStringLiteral("_"),
                            QString::fromUtf8("中文"),
                            QString::fromUtf8("👩‍🚀"),
                            QString(),
                            QString::fromUtf8(" \u200b\u200d\u2060")};
    for (const auto &text : texts) {
        item.setRuns({QVariantMap{{"key", "ink"}, {"text", text}, {"maxBlur", 0}}});
        QVERIFY2(item.errorString().isEmpty(), qPrintable(item.errorString()));
        const auto metric = item.metrics().first().toMap();
        QVERIFY(metric.contains("inkTop"));
        QVERIFY(metric.contains("inkBottom"));
        QImage image(2048, 1024, QImage::Format_ARGB32_Premultiplied);
        image.setDevicePixelRatio(4);
        image.fill(Qt::transparent);
        QTextLayout layout(text, font);
        layout.beginLayout();
        auto line = layout.createLine();
        if (line.isValid())
            line.setLineWidth(1000000);
        layout.endLayout();
        {
            QPainter painter(&image);
            painter.setPen(Qt::white);
            layout.draw(&painter, {20, 80});
        }
        int top = image.height(), bottom = -1;
        for (int y = 0; y < image.height(); ++y)
            for (int x = 0; x < image.width(); ++x)
                if (qAlpha(image.pixel(x, y)) > 0) {
                    top = std::min(top, y);
                    bottom = std::max(bottom, y);
                }
        if (bottom < 0) {
            QCOMPARE(metric.value("inkTop").toDouble(), 0.);
            QCOMPARE(metric.value("inkBottom").toDouble(), 0.);
        } else {
            const double baseline = 80 + metric.value("baseline").toDouble();
            const double expectedTop = top / 4. - baseline;
            const double expectedBottom = (bottom + 1) / 4. - baseline;
            QVERIFY2(std::abs(metric.value("inkTop").toDouble() - expectedTop) < 1.1,
                     qPrintable(QString("%1 inkTop=%2, raster=%3")
                                    .arg(text)
                                    .arg(metric.value("inkTop").toDouble())
                                    .arg(expectedTop)));
            QVERIFY2(std::abs(metric.value("inkBottom").toDouble() - expectedBottom) < 1.1,
                     qPrintable(QString("%1 inkBottom=%2, raster=%3")
                                    .arg(text)
                                    .arg(metric.value("inkBottom").toDouble())
                                    .arg(expectedBottom)));
        }
    }
}

QTEST_MAIN(GlyphRunTest)
#include "GlyphRunTest.moc"
