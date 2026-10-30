#include "GlyphRun.h"
#include "GlyphAtlas_p.h"
#include "GlyphRunCanvas_p.h"
#include <QDataStream>
#include <QBuffer>
#include <QCryptographicHash>
#include <QPointer>
#include <QThreadPool>
#include <deque>
#include <functional>
#include <QColor>
#include <QFontInfo>
#include <QRawFont>
#include <QGuiApplication>
#include <QGlyphRun>
#include <QImage>
#include <QPainter>
#include <QPainterPathStroker>
#include <QQuickWindow>
#include <QRunnable>
#include <QSGGeometryNode>
#include <QSGMaterial>
#include <QSGMaterialShader>
#include <QSGTexture>
#include <QTextBoundaryFinder>
#include <QTextLayout>
#include <QTransform>
#include <QMutex>
#include <QMutexLocker>
#include <qfloat16.h>
#include <QtEndian>
#include <atomic>
#include <cmath>
#include <cstring>
#include <limits>

namespace EchoMusic
{
namespace
{
struct TexturePage
{
    QSGTexture *texture = nullptr;
    std::shared_ptr<std::atomic<bool>> valid;
    QMetaObject::Connection stopping;
    void release()
    {
        if (valid)
            *valid = false;
        delete std::exchange(texture, nullptr);
    }
    ~TexturePage()
    {
        QObject::disconnect(stopping);
        release();
    }
};
struct StaticAtlasCache
{
    QList<std::weak_ptr<TexturePage>> pages;
    std::shared_ptr<std::atomic<bool>> valid = std::make_shared<std::atomic<bool>>(true);
};
std::shared_ptr<TexturePage> staticAtlasPage(const std::shared_ptr<StaticAtlasCache> &cache,
                                             QQuickWindow *window, const QList<QImage> &images,
                                             int index, bool &uploaded)
{
    if (cache->pages.size() < images.size())
        cache->pages.resize(images.size());
    auto page = cache->pages[index].lock();
    if (!page || !page->texture) {
        page = std::make_shared<TexturePage>();
        page->valid = cache->valid;
        page->texture = window->createTextureFromImage(images[index]);
        if (page->texture)
            page->texture->setFiltering(QSGTexture::Linear);
        const auto weak = std::weak_ptr<TexturePage>(page);
        page->stopping = QObject::connect(
            window, &QQuickWindow::sceneGraphAboutToStop, window,
            [weak] {
                if (auto retained = weak.lock())
                    retained->release();
            },
            Qt::DirectConnection);
        cache->pages[index] = page;
        uploaded = true;
    }
    return page;
}
class ReleasePages final : public QRunnable
{
public:
    // Never retain a GPU page on the GUI thread: dropped jobs then own only weak refs.
    QList<std::weak_ptr<TexturePage>> pages;
    void run() override
    {
        for (const auto &weak : pages)
            if (auto page = weak.lock())
                page->release();
    }
};
class RunMaterial final : public QSGMaterial
{
public:
    std::shared_ptr<TexturePage> page, bodyEffects, halo;
    bool screen = false;
    RunMaterial()
    {
        setFlag(Blending);
    }
    QSGMaterialType *type() const override
    {
        static QSGMaterialType type;
        return &type;
    }
    QSGMaterialShader *createShader(QSGRendererInterface::RenderMode) const override;
    int compare(const QSGMaterial *other) const override
    {
        const auto *material = static_cast<const RunMaterial *>(other);
        if (screen != material->screen)
            return screen ? 1 : -1;
        if (page == material->page && bodyEffects == material->bodyEffects &&
            halo == material->halo)
            return 0;
        if (page != material->page)
            return std::less<const TexturePage *>()(page.get(), material->page.get()) ? -1 : 1;
        if (bodyEffects != material->bodyEffects)
            return std::less<const TexturePage *>()(bodyEffects.get(), material->bodyEffects.get())
                       ? -1
                       : 1;
        return std::less<const TexturePage *>()(halo.get(), material->halo.get()) ? -1 : 1;
    }
};
class RunShader final : public QSGMaterialShader
{
public:
    RunShader()
    {
        setFlag(UpdatesGraphicsPipelineState);
        setShaderFileName(VertexStage, ":/qt/qml/EchoMusic/PluginSdk/shaders/glyph-run.vert.qsb");
        setShaderFileName(FragmentStage, ":/qt/qml/EchoMusic/PluginSdk/shaders/glyph-run.frag.qsb");
    }
    bool updateGraphicsPipelineState(RenderState &, GraphicsPipelineState *state,
                                     QSGMaterial *material, QSGMaterial *) override
    {
        state->blendEnable = true;
        state->separateBlendFactors = true;
        state->srcColor = GraphicsPipelineState::One;
        state->dstColor = static_cast<RunMaterial *>(material)->screen
                              ? GraphicsPipelineState::OneMinusSrcColor
                              : GraphicsPipelineState::OneMinusSrcAlpha;
        state->srcAlpha = GraphicsPipelineState::One;
        state->dstAlpha = GraphicsPipelineState::OneMinusSrcAlpha;
        state->opColor = GraphicsPipelineState::BlendOp::Add;
        state->opAlpha = GraphicsPipelineState::BlendOp::Add;
        return true;
    }
    bool updateUniformData(RenderState &state, QSGMaterial *, QSGMaterial *) override
    {
        const auto matrix = state.combinedMatrix();
        const float opacity = state.opacity();
        auto *data = state.uniformData();
        memcpy(data->data(), matrix.constData(), 64);
        memcpy(data->data() + 64, &opacity, 4);
        return true;
    }
    void updateSampledImage(RenderState &state, int binding, QSGTexture **texture,
                            QSGMaterial *material, QSGMaterial *) override
    {
        if (binding < 1 || binding > 3)
            return;
        const auto *run = static_cast<RunMaterial *>(material);
        auto *page = (binding == 1   ? run->page
                      : binding == 2 ? run->halo
                                     : run->bodyEffects)
                         ->texture;
        if (page)
            page->commitTextureOperations(state.rhi(), state.resourceUpdateBatch());
        *texture = page;
    }
};
QSGMaterialShader *RunMaterial::createShader(QSGRendererInterface::RenderMode) const
{
    return new RunShader;
}
struct Vertex
{
    float position[2], uv[4], bounds[4], color[4], effects[4];
    float haloUv[2], haloBounds[4], haloRadii[4], haloColor[3][4];
    float bodyEffectsUv[4], bodyEffectsBounds[4];
    float runLocal[2], clipBounds[4];
};
const QSGGeometry::AttributeSet &attributes()
{
    static const QSGGeometry::Attribute values[] = {
        QSGGeometry::Attribute::create(0, 2, QSGGeometry::FloatType, true),
        QSGGeometry::Attribute::create(1, 4, QSGGeometry::FloatType),
        QSGGeometry::Attribute::create(2, 4, QSGGeometry::FloatType),
        QSGGeometry::Attribute::create(3, 4, QSGGeometry::FloatType),
        QSGGeometry::Attribute::create(4, 4, QSGGeometry::FloatType),
        QSGGeometry::Attribute::create(5, 2, QSGGeometry::FloatType),
        QSGGeometry::Attribute::create(6, 4, QSGGeometry::FloatType),
        QSGGeometry::Attribute::create(8, 4, QSGGeometry::FloatType),
        QSGGeometry::Attribute::create(9, 4, QSGGeometry::FloatType),
        QSGGeometry::Attribute::create(10, 4, QSGGeometry::FloatType),
        QSGGeometry::Attribute::create(11, 4, QSGGeometry::FloatType),
        QSGGeometry::Attribute::create(12, 4, QSGGeometry::FloatType),
        QSGGeometry::Attribute::create(13, 4, QSGGeometry::FloatType),
        QSGGeometry::Attribute::create(14, 2, QSGGeometry::FloatType),
        QSGGeometry::Attribute::create(15, 4, QSGGeometry::FloatType)};
    static const QSGGeometry::AttributeSet set{15, sizeof(Vertex), values};
    return set;
}
struct Batch
{
    int body = 0, bodyEffects = -1, halo = -1;
    QList<int> draws;
    QSGGeometryNode *node = nullptr;
};
class RunRoot final : public QSGNode
{
public:
    quint64 revision = ~quint64(0);
    quint64 topologyRevision = ~quint64(0);
    QList<Batch> batches;
    QList<std::shared_ptr<TexturePage>> bodies, effects;
};
struct Halo
{
    qreal radius = 0, opacity = 1;
    QColor color = Qt::white;
};
struct Pose
{
    QString instance;
    int order = -1;
    qreal x = 0, y = 0, scale = 1, rotation = 0, opacity = 1, blur = 0, glow = 0;
    QColor color = Qt::white, tint = Qt::white;
    QList<Halo> halos;
    bool clipped = false;
    QRectF clip;
};
struct Draw
{
    int fragment = 0;
    bool individual = false;
    Pose pose;
};
struct Fragment
{
    QString key;
    int start = 0, page = 0, bodyEffectsPage = -1, haloPage = -1;
    qreal radius = 32, haloRadius = 0, haloDensity = 0, runBaseline = 0;
    bool colorInk = false;
    QRectF rect, inkBounds, bodyEffectsRect, haloRect;
    QPointF anchor;
    QRect pixels, bodyEffectsPixels, haloPixels;
    QList<QGlyphRun> glyphs;
    QPainterPath strokeMask;
};
QString poseKey(const QString &key, int start)
{
    return key + QChar(0x1f) + QString::number(start);
}
bool finiteRange(const QVariantMap &map, const char *key, qreal min, qreal max)
{
    if (!map.contains(key))
        return true;
    bool ok = false;
    const double value = map.value(key).toDouble(&ok);
    return ok && std::isfinite(value) && value >= min && value <= max;
}
QPointF primaryEmMetrics(const QFont &font, qreal requestedPixels = 0)
{
    const auto primary = QRawFont::fromFont(font);
    const float emHeight = requestedPixels > 0 ? requestedPixels : primary.pixelSize();
    float ascent = primary.ascent(), descent = primary.descent();
    // Blink's alphabetic em box uses the PRIMARY face's normalized OS/2 ratio,
    // independently of the fonts that happen to supply this text's fallback glyphs.
    // OS/2 is a tiny metrics table; no bitmap/colour font table is loaded here.
    const auto os2 = primary.fontTable("OS/2");
    if (os2.size() >= 72) {
        const int typoAscent = qFromBigEndian<qint16>(os2.constData() + 68);
        const int typoDescent = -int(qFromBigEndian<qint16>(os2.constData() + 70));
        if (typoAscent > 0 && typoDescent >= 0) {
            ascent = typoAscent;
            descent = typoDescent;
        }
    }
    const float height = ascent + descent;
    if (!std::isfinite(emHeight) || emHeight <= 0 || !std::isfinite(height) || height <= 0 ||
        ascent < 0 || ascent > height)
        return {};
    const float normalizedAscent = std::round(ascent * emHeight / height * 64) / 64;
    return {normalizedAscent, std::round(emHeight * 64) / 64 - normalizedAscent};
}
// Three separable box passes, only while immutable mask pages are being built.
QVector<float> softCoverage(const QImage &mask, int requestedRadius, qreal sigma = 0)
{
    const int w = mask.width(), h = mask.height();
    int lowerRadius = requestedRadius, upperRadius = requestedRadius, lowerPasses = 3;
    if (sigma > 0) {
        // Three unequal odd box widths match the requested Gaussian variance.
        int lowerWidth = qFloor(std::sqrt(4 * sigma * sigma + 1));
        if (lowerWidth % 2 == 0)
            --lowerWidth;
        lowerWidth = std::max(1, lowerWidth);
        lowerRadius = (lowerWidth - 1) / 2;
        upperRadius = lowerRadius + 1;
        lowerPasses = std::clamp(
            qRound((12 * sigma * sigma - 3 * lowerWidth * lowerWidth - 12 * lowerWidth - 9) /
                   (-4.0 * lowerWidth - 4)),
            0, 3);
    }
    QVector<float> source(w * h), scratch(w * h);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            source[y * w + x] = qAlpha(mask.pixel(x, y));
    for (int pass = 0; pass < 3; ++pass) {
        const int radius = pass < lowerPasses ? lowerRadius : upperRadius;
        const int kernel = radius * 2 + 1;
        for (int y = 0; y < h; ++y) {
            float sum = 0;
            for (int x = 0; x <= radius && x < w; ++x)
                sum += source[y * w + x];
            for (int x = 0; x < w; ++x) {
                scratch[y * w + x] = sum / kernel;
                if (x - radius >= 0)
                    sum -= source[y * w + x - radius];
                if (x + radius + 1 < w)
                    sum += source[y * w + x + radius + 1];
            }
        }
        for (int x = 0; x < w; ++x) {
            float sum = 0;
            for (int y = 0; y <= radius && y < h; ++y)
                sum += scratch[y * w + x];
            for (int y = 0; y < h; ++y) {
                source[y * w + x] = sum / kernel;
                if (y - radius >= 0)
                    sum -= scratch[(y - radius) * w + x];
                if (y + radius + 1 < h)
                    sum += scratch[(y + radius + 1) * w + x];
            }
        }
    }
    return source;
}

struct PreparationToken
{
    std::atomic<bool> cancelled{false};
    std::shared_ptr<GlyphCanvas::Memory> memory;
};
class PreparationQueue final : public QObject
{
public:
    using Completion = std::function<void()>;
    using Work = std::function<Completion()>;
    struct Job
    {
        QPointer<QObject> owner;
        std::shared_ptr<PreparationToken> token;
        Work work;
    };
    QThreadPool pool;
    std::deque<Job> queue;
    bool running = false, closing = false;
    std::shared_ptr<PreparationToken> runningToken;
    explicit PreparationQueue(QObject *parent) : QObject(parent)
    {
        pool.setMaxThreadCount(1);
        pool.setExpiryTimeout(0);
        connect(qApp, &QCoreApplication::aboutToQuit, this, [this] {
            closing = true;
            if (runningToken)
                runningToken->cancelled = true;
            for (auto &job : queue)
                job.token->cancelled = true;
            queue.clear();
            // Only application shutdown joins the worker. Hiding/destroying a
            // GlyphRun cancels its lease and never waits on this pool.
            pool.waitForDone();
        });
    }
    static PreparationQueue *instance()
    {
        static QPointer<PreparationQueue> value;
        if (!value)
            value = new PreparationQueue(qApp);
        return value;
    }
    bool enqueue(Job job)
    {
        for (auto it = queue.begin(); it != queue.end();) {
            if (it->owner.isNull() || it->token->cancelled)
                it = queue.erase(it);
            else
                ++it;
        }
        if (closing || queue.size() + (running ? 1 : 0) >= 32)
            return false;
        queue.push_back(std::move(job));
        start();
        return true;
    }
    void start()
    {
        if (running || closing || queue.empty())
            return;
        auto job = std::move(queue.front());
        queue.pop_front();
        running = true;
        runningToken = job.token;
        pool.start(QRunnable::create([this, job = std::move(job)]() mutable {
            Completion done;
            if (!job.token->cancelled)
                done = job.work();
            // Drop input/font-serialization closures and the running lease on
            // this worker, after every font-engine local has been destroyed.
            job.work = {};
            QMetaObject::invokeMethod(
                this,
                [this, owner = job.owner, token = std::move(job.token), done = std::move(done)] {
                    running = false;
                    runningToken.reset();
                    if (!closing && owner && !token->cancelled && done)
                        done();
                    start();
                },
                Qt::QueuedConnection);
        }));
    }
};

struct LayoutBuildResult
{
    QVariantList metrics;
    QList<Fragment> fragments;
    QList<QImage> atlases, effectsAtlases;
    QHash<QString, QSet<int>> boundaries;
    QHash<QString, int> blurLimits, haloLimits;
};
bool buildGlyphLayout(const QVariantList &runs, const QFont &baseFont, bool measurementOnly,
                      qreal dpr, bool visible, bool keepGlyphs, LayoutBuildResult &result,
                      QString &error, bool canvasPreparation = false)
{
    auto fail = [&error](const QString &message) {
        error = message;
        return false;
    };
    if (runs.size() > GlyphRun::MaxRuns)
        return fail(QStringLiteral("Too many runs (maximum 128)"));
    const qreal basePx = QFontInfo(baseFont).pixelSize();
    const int maximumSize = measurementOnly ? 16384 : GlyphRun::MaxVisibleFontSize;
    if (!std::isfinite(basePx) || basePx <= 0 || basePx > maximumSize)
        return fail(QStringLiteral("Font pixel size must be in (0, %1]").arg(maximumSize));
    int textUnits = 0;
    QSet<QString> keys;
    for (const auto &value : runs) {
        const auto run = value.toMap();
        const QString key = run.value("key").toString();
        if (key.isEmpty() || key.size() > 128 || key.contains(QChar(0x1f)) || keys.contains(key))
            return fail(
                QStringLiteral("Run keys must be unique nonempty strings (maximum 128 units)"));
        if (run.value("text").metaType().id() != QMetaType::QString)
            return fail(QStringLiteral("Run text must be a string"));
        const QString text = run.value("text").toString();
        if (!run.contains("maxWidth") &&
            (text.contains('\n') || text.contains('\r') || text.contains(QChar(0x2028)) ||
             text.contains(QChar(0x2029))))
            return fail(QStringLiteral("Each run must contain one unwrapped text line"));
        if (!finiteRange(run, "maxWidth", 1, 4096) ||
            !finiteRange(run, "lineHeight", std::numeric_limits<qreal>::min(), 512))
            return fail(QStringLiteral(
                "Paragraph maxWidth must be in [1, 4096] and lineHeight in (0, 512]"));
        if (run.contains("strokeBitmap") &&
            (run.value("strokeBitmap").metaType().id() != QMetaType::QString ||
             (run.value("strokeBitmap").toString() != "reject" &&
              run.value("strokeBitmap").toString() != "fill")))
            return fail(QStringLiteral("strokeBitmap must be reject or fill"));
        keys.insert(key);
        textUnits += text.size();
        if (textUnits > GlyphRun::MaxTextUnits)
            return fail(QStringLiteral("Text exceeds 8192 UTF-16 units"));
        if (!finiteRange(run, "size", 1. / 64, maximumSize) ||
            !finiteRange(run, "weight", 1, 1000) || !finiteRange(run, "spacing", -16, 64) ||
            !finiteRange(run, "strokeWidth", 0, 8) ||
            (run.contains("maxBlur") && run.value("maxBlur").toInt() != 0 &&
             run.value("maxBlur").toInt() != 8 && run.value("maxBlur").toInt() != 16 &&
             run.value("maxBlur").toInt() != 32) ||
            (run.contains("maxBlur") &&
             run.value("maxBlur").toDouble() != run.value("maxBlur").toInt()) ||
            !finiteRange(run, "maxBlur", 0, 32) ||
            (run.contains("maxHaloRadius") && run.value("maxHaloRadius").toDouble() != 0 &&
             run.value("maxHaloRadius").toDouble() != 40 &&
             run.value("maxHaloRadius").toDouble() != 80 &&
             run.value("maxHaloRadius").toDouble() != 160 &&
             run.value("maxHaloRadius").toDouble() != 512 &&
             run.value("maxHaloRadius").toDouble() != 1024))
            return fail(QStringLiteral("Run font override outside its supported range"));
    }
    QList<Fragment> fragments;
    QVariantList metrics;
    QHash<QString, QSet<int>> boundariesByKey;
    QHash<QString, int> blurLimits, haloLimits;
    // Blur radii are logical pixels. Only their already smooth coverage is
    // sampled at lower density; clear ink always retains the full window DPR.
    const qreal bodyDensity = std::min(dpr * .25, .5);
    const qreal haloDensity = std::min(dpr * .125, .25);
    for (const auto &value : runs) {
        const auto run = value.toMap();
        const QString key = run.value("key").toString(), text = run.value("text").toString();
        QFont font = baseFont;
        const qreal requestedSize = run.value("size", basePx).toDouble();
        const bool fractional = requestedSize != qRound(requestedSize);
        const int referenceSize =
            fractional ? std::max(64, qCeil(requestedSize)) : qRound(requestedSize);
        const qreal layoutScale = fractional ? requestedSize / referenceSize : 1;
        if (run.contains("size"))
            font.setPixelSize(referenceSize);
        if (run.contains("weight"))
            font.setWeight(QFont::Weight(run.value("weight").toInt()));
        if (run.contains("spacing"))
            font.setLetterSpacing(QFont::AbsoluteSpacing, run.value("spacing").toDouble());
        if (fractional) {
            if (font.letterSpacingType() == QFont::AbsoluteSpacing)
                font.setLetterSpacing(QFont::AbsoluteSpacing, font.letterSpacing() / layoutScale);
            font.setWordSpacing(font.wordSpacing() / layoutScale);
        }
        const auto em = primaryEmMetrics(font, fractional ? requestedSize : 0);
        // QFont rounds pixel/point sizes on this platform. Preserve complete native
        // contextual shaping and fallback at a reference size; only noninteger runs
        // scale positions and selected QRawFonts to their actual requested size.
        const auto nativeGlyphs = [layoutScale](QList<QGlyphRun> glyphs) {
            if (layoutScale == 1)
                return glyphs;
            for (auto &glyph : glyphs) {
                auto raw = glyph.rawFont();
                raw.setPixelSize(raw.pixelSize() * layoutScale);
                glyph.setRawFont(raw);
                auto positions = glyph.positions();
                for (auto &position : positions)
                    position *= layoutScale;
                glyph.setPositions(positions);
                QRectF bounds;
                const auto indexes = glyph.glyphIndexes();
                for (qsizetype index = 0; index < indexes.size(); ++index) {
                    const auto ink = raw.boundingRect(indexes[index]);
                    if (!ink.isEmpty())
                        bounds |= ink.translated(positions[index]);
                }
                glyph.setBoundingRect(bounds);
            }
            return glyphs;
        };
        const bool wrapped = run.contains("maxWidth");
        QString layoutText = text;
        if (wrapped) {
            // QTextLayout is a paragraph engine: U+2028 requests a hard line.
            // Keep UTF-16 offsets intact, including a single CRLF grapheme.
            for (qsizetype index = 0; index < layoutText.size(); ++index) {
                const QChar ch = layoutText[index];
                if (ch == '\r')
                    layoutText[index] = index + 1 < text.size() && text[index + 1] == '\n'
                                            ? QChar(0x2060)
                                            : QChar(0x2028);
                else if (ch == '\n' || ch == QChar(0x2029))
                    layoutText[index] = QChar(0x2028);
            }
        }
        QTextLayout layout(layoutText, font);
        if (wrapped) {
            QTextOption option;
            option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
            option.setFlags(QTextOption::IncludeTrailingSpaces);
            layout.setTextOption(option);
        }
        layout.beginLayout();
        QList<QTextLine> lines;
        qreal paragraphWidth = 0, paragraphHeight = 0, paragraphAdvance = 0;
        while (true) {
            auto nextLine = layout.createLine();
            if (!nextLine.isValid())
                break;
            nextLine.setLineWidth((wrapped ? run.value("maxWidth").toDouble() : 1000000) /
                                  layoutScale);
            nextLine.setPosition({0, paragraphHeight});
            paragraphHeight += wrapped && run.contains("lineHeight")
                                   ? run.value("lineHeight").toDouble() / layoutScale
                                   : nextLine.height();
            paragraphWidth = std::max(paragraphWidth, nextLine.naturalTextWidth());
            paragraphAdvance = std::max(paragraphAdvance, nextLine.horizontalAdvance());
            lines.append(nextLine);
            if (!wrapped)
                break;
        }
        layout.endLayout();
        const qreal baseline = lines.isEmpty() ? 0 : lines.first().ascent();
        QVariantList graphemes;
        QList<int> boundaries{0};
        QTextBoundaryFinder finder(QTextBoundaryFinder::Grapheme, text);
        while (finder.toNextBoundary() > 0)
            boundaries.append(finder.position());
        QList<int> clusters{0};
        for (int i = 1; i < boundaries.size(); ++i) {
            const int end = boundaries[i];
            const auto glyphs = layout.glyphRuns(clusters.last(), end - clusters.last());
            const bool split = !layout.isValidCursorPosition(end) ||
                               std::any_of(glyphs.begin(), glyphs.end(), [](const QGlyphRun &g) {
                                   return g.flags().testFlag(QGlyphRun::SplitLigature);
                               });
            if (!split || end == text.size())
                clusters.append(end);
        }
        QSet<int> safeStarts;
        for (int i = 0; i + 1 < clusters.size(); ++i)
            safeStarts.insert(clusters[i]);
        boundariesByKey.insert(key, safeStarts);
        int cluster = 0;
        for (int i = 0; i + 1 < boundaries.size(); ++i) {
            const int start = boundaries[i], end = boundaries[i + 1];
            while (cluster + 1 < clusters.size() && clusters[cluster + 1] <= start)
                ++cluster;
            const auto line = layout.lineForTextPosition(start);
            const qreal x = line.isValid() ? line.cursorToX(start) : 0;
            const bool hardBreak = text.mid(start, end - start).contains('\n') ||
                                   text.mid(start, end - start).contains('\r') ||
                                   text.mid(start, end - start).contains(QChar(0x2028)) ||
                                   text.mid(start, end - start).contains(QChar(0x2029));
            const qreal next = line.isValid() && !hardBreak ? line.cursorToX(end) : x;
            graphemes.append(QVariantMap{
                {"start", start},
                {"length", end - start},
                {"x", x * layoutScale},
                {"y", line.isValid() ? (line.y() + line.ascent() - baseline) * layoutScale : 0},
                {"advance", (next - x) * layoutScale},
                {"clusterStart", clusters[cluster]}});
        }
        QRectF runInk;
        for (const auto &glyphs : nativeGlyphs(layout.glyphRuns())) {
            const auto indexes = glyphs.glyphIndexes();
            const auto positions = glyphs.positions();
            const auto rawFont = glyphs.rawFont();
            for (qsizetype index = 0; index < indexes.size(); ++index) {
                const auto bounds = rawFont.boundingRect(indexes[index]);
                if (!bounds.isEmpty())
                    runInk |= bounds.translated(positions[index]);
            }
        }
        metrics.append(QVariantMap{
            {"key", key},
            {"width", paragraphWidth * layoutScale},
            {"advance", paragraphAdvance * layoutScale},
            {"height", paragraphHeight * layoutScale},
            {"lineCount", std::max(1, int(lines.size()))},
            {"baseline", baseline * layoutScale},
            {"emAscent", em.x()},
            {"emDescent", em.y()},
            {"inkLeft", runInk.isEmpty() ? 0 : runInk.left()},
            {"inkRight", runInk.isEmpty() ? 0 : runInk.right()},
            {"inkTop", runInk.isEmpty() ? 0 : runInk.top() - baseline * layoutScale},
            {"inkBottom", runInk.isEmpty() ? 0 : runInk.bottom() - baseline * layoutScale},
            {"graphemes", graphemes}});
        if (measurementOnly)
            continue;
        const qreal radius = canvasPreparation ? 0 : run.value("maxBlur", 32).toInt();
        blurLimits.insert(key, int(radius));
        const int haloRadius = canvasPreparation ? 0 : run.value("maxHaloRadius", 0).toInt();
        haloLimits.insert(key, haloRadius);
        for (int i = 0; i + 1 < clusters.size(); ++i) {
            auto glyphs =
                nativeGlyphs(layout.glyphRuns(clusters[i], clusters[i + 1] - clusters[i]));
            for (const auto &glyph : glyphs) {
                struct InkPart
                {
                    QGlyphRun glyph;
                    QRectF bounds;
                    QPainterPath stroke;
                };
                QList<InkPart> parts;
                const qreal strokeWidth = run.value("strokeWidth", 0).toDouble();
                if (strokeWidth == 0) {
                    parts.append({glyph, glyph.boundingRect(), {}});
                } else {
                    QPainterPath outline;
                    QList<quint32> bitmapIndexes;
                    QList<QPointF> bitmapPositions;
                    QRectF bitmapBounds;
                    bool bitmapPart = false, hasPart = false;
                    const auto flush = [&]() {
                        if (bitmapPart && !bitmapIndexes.isEmpty()) {
                            auto bitmap = glyph;
                            bitmap.setGlyphIndexes(bitmapIndexes);
                            bitmap.setPositions(bitmapPositions);
                            parts.append({bitmap, bitmapBounds, {}});
                            bitmapIndexes.clear();
                            bitmapPositions.clear();
                            bitmapBounds = {};
                        } else if (!outline.isEmpty()) {
                            QPainterPathStroker stroker;
                            stroker.setWidth(strokeWidth);
                            stroker.setCapStyle(Qt::FlatCap);
                            stroker.setJoinStyle(Qt::MiterJoin);
                            stroker.setMiterLimit(10);
                            const auto stroke = stroker.createStroke(outline);
                            parts.append({{}, stroke.boundingRect(), stroke});
                            outline = {};
                        }
                    };
                    const auto indexes = glyph.glyphIndexes();
                    const auto positions = glyph.positions();
                    const auto raw = glyph.rawFont();
                    for (qsizetype index = 0; index < indexes.size(); ++index) {
                        const auto path = raw.pathForGlyph(indexes[index]);
                        const auto ink = raw.boundingRect(indexes[index]);
                        if (path.isEmpty() && ink.isEmpty())
                            continue;
                        const bool bitmap = path.isEmpty();
                        if (bitmap && run.value("strokeBitmap", "reject").toString() != "fill")
                            return fail("A stroke-only glyph has ink but no native outline path");
                        // Preserve native glyph order even within a font containing both formats.
                        // Coloured bitmap ink and tintable outlines stay separate atlas fragments.
                        if (hasPart && bitmap != bitmapPart)
                            flush();
                        bitmapPart = bitmap;
                        hasPart = true;
                        if (bitmap) {
                            bitmapIndexes.append(indexes[index]);
                            bitmapPositions.append(positions[index]);
                            bitmapBounds |= ink.translated(positions[index]);
                        } else
                            outline.addPath(path.translated(positions[index]));
                    }
                    flush();
                }
                for (auto &part : parts) {
                    const QRectF bounds = part.bounds;
                    if (bounds.isEmpty())
                        continue;
                    const QRect pixels = bounds.adjusted(-2, -2, 2, 2).toAlignedRect();
                    Fragment fragment;
                    fragment.strokeMask = std::move(part.stroke);
                    fragment.key = key;
                    fragment.start = clusters[i];
                    fragment.radius = radius;
                    fragment.rect = pixels;
                    fragment.inkBounds = bounds;
                    fragment.haloRadius = haloRadius;
                    fragment.haloDensity =
                        haloRadius > 160 ? std::min(haloDensity, 32. / haloRadius) : haloDensity;
                    fragment.runBaseline = baseline * layoutScale;
                    if (radius > 0) {
                        const qreal padding = radius * 3 + 2;
                        fragment.bodyEffectsRect =
                            bounds.adjusted(-padding, -padding, padding, padding).toAlignedRect();
                        fragment.bodyEffectsPixels.setSize(
                            {qCeil(fragment.bodyEffectsRect.width() * bodyDensity),
                             qCeil(fragment.bodyEffectsRect.height() * bodyDensity)});
                        if (fragment.bodyEffectsPixels.width() + 2 > GlyphRun::PageSize ||
                            fragment.bodyEffectsPixels.height() + 2 > GlyphRun::PageSize)
                            return fail("A body blur tile exceeds the atlas page limit");
                    }
                    if (haloRadius > 0) {
                        fragment.haloRect = bounds
                                                .adjusted(-haloRadius * 3 - 2, -haloRadius * 3 - 2,
                                                          haloRadius * 3 + 2, haloRadius * 3 + 2)
                                                .toAlignedRect();
                        fragment.haloPixels.setSize(
                            {qCeil(fragment.haloRect.width() * fragment.haloDensity),
                             qCeil(fragment.haloRect.height() * fragment.haloDensity)});
                        if (fragment.haloPixels.width() + 2 > GlyphRun::PageSize ||
                            fragment.haloPixels.height() + 2 > GlyphRun::PageSize)
                            return fail("A halo tile exceeds the atlas page limit");
                    }
                    const auto clusterLine = layout.lineForTextPosition(clusters[i]);
                    fragment.anchor =
                        (clusterLine.isValid() ? QPointF((clusterLine.cursorToX(clusters[i]) +
                                                          clusterLine.cursorToX(clusters[i + 1])) /
                                                             2,
                                                         clusterLine.y() + clusterLine.ascent())
                                               : QPointF(0, baseline)) *
                        layoutScale;
                    fragment.glyphs = {part.glyph};
                    fragment.pixels.setSize(
                        {qCeil(pixels.width() * dpr), qCeil(pixels.height() * dpr)});
                    if (fragment.pixels.width() + 2 > GlyphRun::MaxInkPageSize ||
                        fragment.pixels.height() + 2 > GlyphRun::MaxInkPageSize)
                        return fail(QStringLiteral(
                            "A shaped cluster exceeds the 2048-pixel ink atlas page"));
                    fragments.append(std::move(fragment));
                    if (fragments.size() > GlyphRun::MaxFragments)
                        return fail(QStringLiteral("Too many shaped fragments (maximum 2048)"));
                }
            }
        }
    }
    QList<QSize> pageSizes;
    QSize used;
    int x = 0, y = 0, shelf = 0, page = 0;
    int inkPageLimit = GlyphRun::PageSize;
    const auto finishInkPage = [&] {
        if (!used.isEmpty())
            pageSizes.append(used);
        x = y = shelf = 0;
        used = {};
    };
    for (auto &fragment : fragments) {
        const QSize size = fragment.pixels.size() + QSize(2, 2);
        const int requiredLimit = std::max(size.width(), size.height()) > GlyphRun::PageSize
                                      ? GlyphRun::MaxInkPageSize
                                      : GlyphRun::PageSize;
        if (requiredLimit > inkPageLimit) {
            finishInkPage();
            inkPageLimit = requiredLimit;
        }
        if (x + size.width() > inkPageLimit) {
            x = 0;
            y += shelf;
            shelf = 0;
        }
        if (y + size.height() > inkPageLimit) {
            finishInkPage();
            inkPageLimit = requiredLimit;
        }
        if (pageSizes.size() >= GlyphRun::MaxPages)
            return fail(QStringLiteral("Glyph masks exceed the eight-page texture budget"));
        fragment.page = int(pageSizes.size());
        fragment.pixels.moveTopLeft({x + 1, y + 1});
        x += size.width();
        shelf = std::max(shelf, size.height());
        used = used.expandedTo({x, y + shelf});
    }
    finishInkPage();
    qint64 inkBytes = 0;
    for (const auto &size : pageSizes)
        inkBytes += qint64(size.width()) * size.height() * 4;
    if (inkBytes > qint64(GlyphRun::MaxPages) * GlyphRun::PageSize * GlyphRun::PageSize * 4)
        return fail(QStringLiteral("Glyph masks exceed the 32 MiB ink texture budget"));
    // Clear ink and both effects share fixed budgets independently. Body blur
    // and halo tiles use the same four half-float pages, never separate pools.
    QList<QSize> effectsPageSizes;
    x = y = shelf = page = 0;
    used = {};
    const auto placeEffect = [&](QRect &pixels, int &targetPage) {
        const QSize size = pixels.size() + QSize(2, 2);
        if (x + size.width() > GlyphRun::PageSize) {
            x = 0;
            y += shelf;
            shelf = 0;
        }
        if (y + size.height() > GlyphRun::PageSize) {
            effectsPageSizes.append(used);
            ++page;
            x = y = shelf = 0;
            used = {};
        }
        if (page >= GlyphRun::MaxEffectsPages)
            return false;
        targetPage = page;
        pixels.moveTopLeft({x + 1, y + 1});
        x += size.width();
        shelf = std::max(shelf, size.height());
        used = used.expandedTo({x, y + shelf});
        return true;
    };
    for (auto &fragment : fragments)
        if (fragment.radius > 0 &&
            !placeEffect(fragment.bodyEffectsPixels, fragment.bodyEffectsPage))
            return fail("Body blur and halos exceed the four-page effects texture budget");
    for (auto &fragment : fragments)
        if (fragment.haloRadius > 0 && !placeEffect(fragment.haloPixels, fragment.haloPage))
            return fail("Body blur and halos exceed the four-page effects texture budget");
    if (!used.isEmpty())
        effectsPageSizes.append(used);
    QList<QImage> effectsAtlases;
    QList<QImage> atlases;
    if (visible) {
        for (const auto &size : pageSizes) {
            QImage image(size, QImage::Format_RGBA8888_Premultiplied);
            image.fill(Qt::transparent);
            atlases.append(std::move(image));
        }
        for (const auto &size : effectsPageSizes) {
            QImage image(size, QImage::Format_RGBA16FPx4_Premultiplied);
            image.fill(Qt::transparent);
            effectsAtlases.append(std::move(image));
        }
        const auto rasterMask = [](const Fragment &fragment, const QRectF &rect, const QSize &size,
                                   qreal density, const QColor &pen = Qt::white) {
            QImage mask(size, QImage::Format_ARGB32_Premultiplied);
            mask.setDevicePixelRatio(density);
            mask.fill(Qt::transparent);
            QPainter painter(&mask);
            painter.setPen(pen);
            if (!fragment.strokeMask.isEmpty()) {
                painter.setRenderHint(QPainter::Antialiasing);
                painter.fillPath(fragment.strokeMask.translated(-rect.topLeft()), pen);
            } else {
                for (const auto &glyph : fragment.glyphs)
                    painter.drawGlyphRun(-rect.topLeft(), glyph);
            }
            return mask;
        };
        const auto writeEffects = [&](const Fragment &fragment, bool halo, const QImage &inkMask) {
            const int targetPage = halo ? fragment.haloPage : fragment.bodyEffectsPage;
            if (targetPage < 0)
                return;
            const auto &pixels = halo ? fragment.haloPixels : fragment.bodyEffectsPixels;
            const auto &rect = halo ? fragment.haloRect : fragment.bodyEffectsRect;
            const qreal density = halo ? fragment.haloDensity : bodyDensity;
            const qreal ceiling = halo ? fragment.haloRadius : fragment.radius;
            const qreal haloLevels[] = {16, 40, 80, 160};
            const qreal mediumHaloLevels[] = {16, 128, 256, 512};
            const qreal largeHaloLevels[] = {16, 128, 512, 1024};
            const qreal bodyLevels[] = {0, 8, 16, 32};
            const auto *levels = halo ? (fragment.haloRadius > 512   ? largeHaloLevels
                                         : fragment.haloRadius > 160 ? mediumHaloLevels
                                                                     : haloLevels)
                                      : bodyLevels;
            auto mask = rasterMask(fragment, rect, pixels.size(), density);
            if (halo && fragment.haloRadius > 160) {
                // Integrate high-DPR ink into the very low-frequency tile. Drawing
                // a hinted 3px glyph directly would change its area and halo energy.
                QVector<float> coverage(mask.width() * mask.height());
                const qreal ratio = density / dpr;
                for (int sy = 0; sy < inkMask.height(); ++sy)
                    for (int sx = 0; sx < inkMask.width(); ++sx) {
                        const int alpha = qAlpha(inkMask.pixel(sx, sy));
                        if (!alpha)
                            continue;
                        const qreal x0 = (fragment.rect.x() - rect.x()) * density + sx * ratio;
                        const qreal y0 = (fragment.rect.y() - rect.y()) * density + sy * ratio;
                        for (int ty = qFloor(y0); ty < qCeil(y0 + ratio); ++ty)
                            for (int tx = qFloor(x0); tx < qCeil(x0 + ratio); ++tx)
                                if (tx >= 0 && ty >= 0 && tx < mask.width() && ty < mask.height()) {
                                    const qreal area = (std::min(x0 + ratio, qreal(tx + 1)) -
                                                        std::max(x0, qreal(tx))) *
                                                       (std::min(y0 + ratio, qreal(ty + 1)) -
                                                        std::max(y0, qreal(ty)));
                                    coverage[ty * mask.width() + tx] += float(alpha * area);
                                }
                    }
                for (int py = 0; py < mask.height(); ++py)
                    for (int px = 0; px < mask.width(); ++px) {
                        const int alpha =
                            std::clamp(qRound(coverage[py * mask.width() + px]), 0, 255);
                        mask.setPixel(px, py, qRgba(alpha, alpha, alpha, alpha));
                    }
            }
            QVector<float> channels[4];
            for (int level = 0; level < 4; ++level) {
                if (level > 0 && levels[level - 1] >= ceiling)
                    channels[level] = channels[level - 1];
                else {
                    const int radius =
                        levels[level] == 0
                            ? 0
                            : std::max(1, qRound(std::min(levels[level], ceiling) * density));
                    channels[level] = softCoverage(mask, radius,
                                                   halo && fragment.haloRadius > 160
                                                       ? std::min(levels[level], ceiling) * density
                                                       : 0);
                }
            }
            auto &atlas = effectsAtlases[targetPage];
            for (int py = 0; py < mask.height(); ++py) {
                auto *row =
                    reinterpret_cast<qfloat16 *>(atlas.scanLine(py + pixels.y())) + pixels.x() * 4;
                for (int px = 0; px < mask.width(); ++px)
                    for (int channel = 0; channel < 4; ++channel)
                        row[px * 4 + channel] =
                            qfloat16(channels[channel][py * mask.width() + px] / 255.f);
            }
        };
        for (auto &fragment : fragments) {
            const auto mask = rasterMask(fragment, fragment.rect, fragment.pixels.size(), dpr);
            for (int py = 0; py < mask.height() && !fragment.colorInk; ++py)
                for (int px = 0; px < mask.width(); ++px) {
                    const auto pixel = mask.pixel(px, py);
                    if (qRed(pixel) != qAlpha(pixel) || qGreen(pixel) != qAlpha(pixel) ||
                        qBlue(pixel) != qAlpha(pixel)) {
                        fragment.colorInk = true;
                        break;
                    }
                }
            if (!fragment.colorInk) {
                // Detect a fixed white palette too, without copying large font
                // tables or guessing from family names. Qt renders sbix/COLR/CBDT.
                const auto black =
                    rasterMask(fragment, fragment.rect, fragment.pixels.size(), dpr, Qt::black);
                fragment.colorInk =
                    black == mask &&
                    std::any_of(mask.constBits(), mask.constBits() + mask.sizeInBytes(),
                                [](uchar value) { return value != 0; });
            }
            auto &atlas = atlases[fragment.page];
            for (int py = 0; py < mask.height(); ++py) {
                uchar *dst = atlas.scanLine(py + fragment.pixels.y()) + fragment.pixels.x() * 4;
                for (int px = 0; px < mask.width(); ++px) {
                    const auto pixel = mask.pixel(px, py);
                    dst[px * 4] = fragment.colorInk ? qRed(pixel) : qAlpha(pixel);
                    dst[px * 4 + 1] = fragment.colorInk ? qGreen(pixel) : 0;
                    dst[px * 4 + 2] = fragment.colorInk ? qBlue(pixel) : 0;
                    dst[px * 4 + 3] = fragment.colorInk ? qAlpha(pixel) : 255;
                }
            }
            writeEffects(fragment, false, mask);
            writeEffects(fragment, true, mask);
        }
    }
    if (!keepGlyphs)
        for (auto &fragment : fragments) {
            fragment.glyphs.clear();
            fragment.strokeMask = {};
        }
    result.metrics = std::move(metrics);
    result.fragments = std::move(fragments);
    result.atlases = std::move(atlases);
    result.effectsAtlases = std::move(effectsAtlases);
    result.boundaries = std::move(boundariesByKey);
    result.blurLimits = std::move(blurLimits);
    result.haloLimits = std::move(haloLimits);
    return true;
}

} // namespace

struct GlyphRun::Data
{
    QFont font;
    QVariantList runs, poses, metrics;
    QString error;
    QList<Fragment> fragments;
    QList<QImage> atlases, effectsAtlases;
    QList<Draw> draws;
    QHash<QString, QSet<int>> boundaries;
    QHash<QString, int> blurLimits, haloLimits;
    QMutex gpuMutex;
    QList<std::weak_ptr<TexturePage>> gpuPages;
    quint64 revision = 0;
    quint64 topologyRevision = 0;
    std::atomic<quint64> uploads{0};
    std::atomic<int> batches{0};
    qreal dpr = 1;
    bool released = false;
    bool measurementOnly = false;
    bool atlasOnly = false;
    QSet<const QObject *> atlasConsumers;
    QString atlasDomain;
    std::shared_ptr<StaticAtlasCache> staticCache;
    std::shared_ptr<const GlyphAtlasPreparation> atlasSnapshot;
    bool screen = false;
    QMetaObject::Connection windowVisibility;
    QString haloSampling = QStringLiteral("static");
    qreal maximumRasterScale = 32;
    bool preparing = false;
    bool publishingPreparation = false;
    quint64 preparationGeneration = 0;
    std::shared_ptr<PreparationToken> preparation;
    std::shared_ptr<const GlyphCanvas::Prepared> canvas;
    std::shared_ptr<GlyphCanvas::Status> canvasStatus = std::make_shared<GlyphCanvas::Status>();
};
GlyphRun::GlyphRun(QQuickItem *parent) : QQuickItem(parent), d(std::make_unique<Data>())
{
    d->font = QGuiApplication::font();
    d->font.setPixelSize(32);
    setFlag(ItemHasContents);
    const auto observeWindow = [this](QQuickWindow *target) {
        disconnect(d->windowVisibility);
        if (target) {
            d->windowVisibility = connect(target, &QWindow::visibilityChanged, this,
                                          [this](QWindow::Visibility visibility) {
                                              const bool visible = visibility != QWindow::Hidden &&
                                                                   visibility != QWindow::Minimized;
                                              if (!visible) {
                                                  releaseResources();
                                                  emit layoutChanged();
                                                  emit renderingChanged();
                                              } else if (isVisible() && d->released) {
                                                  rebuildForEnvironment();
                                              }
                                          });
        }
        const qreal dpr = window() ? window()->effectiveDevicePixelRatio() : 1;
        if (!qFuzzyCompare(d->dpr, dpr) && isVisible())
            rebuildForEnvironment();
    };
    connect(this, &QQuickItem::windowChanged, this, observeWindow);
    observeWindow(window());
}
GlyphRun::~GlyphRun()
{
    // Cleanup must not re-enter QML, but consumers such as ShaderEffectSource
    // still need QObject::destroyed to clear their source-item listener.
    blockSignals(true);
    disconnect(this, &QQuickItem::windowChanged, this, nullptr);
    disconnect(d->windowVisibility);
    cancelPreparation();
    releaseResources();
}
bool GlyphRun::atlasOnly() const
{
    return d->atlasOnly;
}
void GlyphRun::setAtlasOnly(bool value)
{
    if (value == d->atlasOnly)
        return;
    d->atlasOnly = value;
    QPointer<GlyphRun> self(this);
    releaseResources();
    if (!self)
        return;
    rebuildForEnvironment();
    if (self)
        emit layoutChanged();
}
void GlyphRun::setAtlasDomain(const QString &domain)
{
    if (d->atlasDomain == domain)
        return;
    if (d->atlasSnapshot)
        *d->atlasSnapshot->valid = false;
    d->atlasSnapshot.reset();
    d->atlasDomain = domain;
    ++d->topologyRevision;
    update();
}
void GlyphRun::setAtlasConsumerActive(const QObject *consumer, bool active)
{
    if (active)
        d->atlasConsumers.insert(consumer);
    else
        d->atlasConsumers.remove(consumer);
    if (!d->atlasOnly)
        return;
    if (d->atlasConsumers.isEmpty()) {
        if (!d->released || d->preparing) {
            QPointer<GlyphRun> self(this);
            releaseResources();
            if (self)
                emit layoutChanged();
        }
    } else if (d->released && !d->preparing)
        rebuildForEnvironment();
}
void GlyphRun::retireAtlasPreparation()
{
    d->runs.clear();
    d->poses.clear();
    d->atlasConsumers.clear();
    QPointer<GlyphRun> self(this);
    releaseResources();
    if (self)
        emit layoutChanged();
}
std::shared_ptr<const GlyphAtlasPreparation> GlyphRun::atlasPreparation() const
{
    if (d->released || d->preparing || d->measurementOnly)
        return {};
    if (d->atlasSnapshot && d->atlasSnapshot->revision == d->revision && *d->atlasSnapshot->valid)
        return d->atlasSnapshot;
    if (d->atlasSnapshot)
        *d->atlasSnapshot->valid = false;
    auto out = std::make_shared<GlyphAtlasPreparation>();
    out->revision = d->revision;
    out->pixelRatio = d->dpr;
    out->metrics = d->metrics;
    QVariantList fragments;
    for (const auto &f : d->fragments) {
        int end = 0;
        for (const auto &r : d->runs) {
            const auto run = r.toMap();
            if (run["key"].toString() == f.key) {
                end = run["text"].toString().size();
                break;
            }
        }
        for (int boundary : d->boundaries.value(f.key))
            if (boundary > f.start && boundary < end)
                end = boundary;
        fragments.append(QVariantMap{{"key", f.key},
                                     {"start", f.start},
                                     {"length", end - f.start},
                                     {"anchor", QVariantList{f.anchor.x(), f.anchor.y()}},
                                     {"baseline", f.runBaseline}});
    }
    if (d->haloSampling == "canvas") {
        auto input = d->canvas;
        if (!input)
            return {};
        out->acquire = [input, fragments, domain = d->atlasDomain](
                           QQuickWindow *w, QRhiResourceUpdateBatch *updates, QString &error) {
            return GlyphCanvas::acquireAtlas(w, domain, input, fragments, updates, error);
        };
    } else {
        // Static atlas pages are immutable once prepared; native draws and RHI consumers
        // share QSGTexture ownership via a weak per-generation render-thread cache.
        auto images = d->atlases;

        if (!d->staticCache || !*d->staticCache->valid)
            d->staticCache = std::make_shared<StaticAtlasCache>();
        out->valid = d->staticCache->valid;
        auto cache = d->staticCache;
        auto fields = d->fragments;
        for (auto &fragment : fields)
            fragment.glyphs.clear();
        out->acquire = [images, fragments, fields,
                        cache](QQuickWindow *w, QRhiResourceUpdateBatch *updates, QString &error) {
            NativeGlyphAtlas result;
            struct Hold
            {
                QList<std::shared_ptr<TexturePage>> pages;
            };
            auto hold = std::make_shared<Hold>();
            if (cache->pages.size() < images.size())
                cache->pages.resize(images.size());
            for (int i = 0; i < images.size(); ++i) {
                bool uploaded = false;
                auto page = staticAtlasPage(cache, w, images, i, uploaded);
                result.uploads += uploaded;
                if (!page->texture) {
                    error = "Cannot allocate native glyph texture";
                    result.retained = hold;
                    return result;
                }
                page->texture->commitTextureOperations(w->rhi(), updates);
                result.textures.push_back(page->texture->rhiTexture());
                hold->pages.append(page);
            }
            for (int i = 0; i < fields.size(); ++i) {
                auto f = fragments[i].toMap();
                const auto &src = fields[i];
                const auto size = images[src.page].size();
                f["levels"] = QVariantList{
                    QVariantMap{{"page", src.page},
                                {"uv", QVariantList{double(src.pixels.x()) / size.width(),
                                                    double(src.pixels.y()) / size.height(),
                                                    double(src.pixels.width()) / size.width(),
                                                    double(src.pixels.height()) / size.height()}},
                                {"rect", QVariantList{src.rect.x(), src.rect.y(), src.rect.width(),
                                                      src.rect.height()}},
                                {"color", src.colorInk},
                                {"coverage", src.colorInk ? "a" : "r"}}};
                result.fragments.append(f);
            }
            result.retained = hold;
            return result;
        };
    }
    d->atlasSnapshot = out;
    return out;
}
QFont GlyphRun::font() const
{
    return d->font;
}
bool GlyphRun::measurementOnly() const
{
    return d->measurementOnly;
}
void GlyphRun::setMeasurementOnly(bool enabled)
{
    if (enabled != d->measurementOnly)
        rebuild(d->runs, d->font, enabled);
    else
        setError({});
}
QVariantList GlyphRun::runs() const
{
    return d->runs;
}
QString GlyphRun::composition() const
{
    return d->screen ? QStringLiteral("screen") : QStringLiteral("source-over");
}
void GlyphRun::setComposition(const QString &value)
{
    if (value != "screen" && value != "source-over") {
        setError("Composition must be source-over or screen");
        return;
    }
    const bool screen = value == "screen";
    if (d->screen != screen) {
        d->screen = screen;
        update();
        emit compositionChanged();
    }
    setError({});
}
QVariantList GlyphRun::poses() const
{
    return d->poses;
}
QVariantList GlyphRun::metrics() const
{
    return d->metrics;
}
QString GlyphRun::errorString() const
{
    return d->error;
}
quint64 GlyphRun::maskRevision() const
{
    return d->revision;
}
quint64 GlyphRun::textureUploads() const
{
    return d->haloSampling == "canvas" ? d->canvasStatus->uploads.load()
                                       : d->uploads.load(std::memory_order_relaxed);
}
int GlyphRun::renderBatches() const
{
    return d->haloSampling == "canvas" ? d->canvasStatus->batches.load()
                                       : d->batches.load(std::memory_order_relaxed);
}
qint64 GlyphRun::cacheBytes() const
{
    qint64 bytes = d->canvas ? d->canvas->bytes : 0;
    for (const auto &atlas : d->atlases)
        bytes += atlas.sizeInBytes();
    for (const auto &atlas : d->effectsAtlases)
        bytes += atlas.sizeInBytes();
    return bytes;
}
QString GlyphRun::haloSampling() const
{
    return d->haloSampling;
}
void GlyphRun::setHaloSampling(const QString &value)
{
    if (value != "static" && value != "canvas") {
        setError("haloSampling must be static or canvas");
        return;
    }
    if (value == d->haloSampling)
        return;
    cancelPreparation();
    d->haloSampling = value;
    rebuildForEnvironment();
}
qreal GlyphRun::maximumRasterScale() const
{
    return d->maximumRasterScale;
}
void GlyphRun::setMaximumRasterScale(qreal value)
{
    if (!std::isfinite(value) || value <= 0 || value > 32) {
        setError("maximumRasterScale must be in (0, 32]");
        return;
    }
    if (value == d->maximumRasterScale)
        return;
    d->maximumRasterScale = value;
    if (d->haloSampling == "canvas")
        rebuildForEnvironment();
    else
        emit layoutChanged();
}
bool GlyphRun::preparing() const
{
    return d->preparing;
}
bool GlyphRun::ready() const
{
    return !d->released && !d->preparing && d->error.isEmpty() &&
           (d->measurementOnly || (d->atlasOnly && (d->canvas || !d->atlases.isEmpty())) ||
            (d->haloSampling == "canvas" ? d->canvasStatus->ready.load()
                                         : d->runs.isEmpty() || d->batches.load() > 0));
}
qint64 GlyphRun::gpuCacheBytes() const
{
    return d->haloSampling == "canvas" ? d->canvasStatus->gpuBytes.load() : cacheBytes();
}
QString GlyphRun::sharedCacheKey() const
{
    return d->canvasStatus->sharedKey;
}
qint64 GlyphRun::sharedCacheBytes() const
{
    return d->canvasStatus->sharedBytes.load();
}
void GlyphRun::cancelPreparation()
{
    ++d->preparationGeneration;
    if (d->preparation)
        d->preparation->cancelled = true;
    d->preparation.reset();
    if (d->preparing) {
        d->preparing = false;
        if (!d->publishingPreparation) {
            emit preparationChanged();
            emit renderingChanged();
        }
    }
}
bool GlyphRun::prepareCanvas(const QVariantList &runs, const QFont &font, bool clearError)
{
    // Check structural/input budgets before copying or serializing GUI values.
    if (runs.size() > MaxRuns) {
        setError("Too many runs (maximum 128)");
        return false;
    }
    qsizetype textUnits = 0, inputUnits = 0;
    for (const auto &entry : runs) {
        if (entry.metaType().id() != QMetaType::QVariantMap) {
            setError("Each run must be an object");
            return false;
        }
        const auto run = entry.toMap();
        if (run.size() > 32 || run.value("text").metaType().id() != QMetaType::QString) {
            setError("Canvas run structure exceeds its input budget");
            return false;
        }
        textUnits += run.value("text").toString().size();
        static const QSet<QString> strings{"key", "text", "strokeBitmap"};
        static const QSet<QString> numbers{"size",    "weight",        "spacing",  "strokeWidth",
                                           "maxBlur", "maxHaloRadius", "maxWidth", "lineHeight"};
        for (auto it = run.cbegin(); it != run.cend(); ++it) {
            const auto type = it.value().metaType().id();
            inputUnits += it.key().size();
            if (strings.contains(it.key()) && type == QMetaType::QString) {
                inputUnits += it.value().toString().size();
            } else if (numbers.contains(it.key()) &&
                       (type == QMetaType::Int || type == QMetaType::UInt ||
                        type == QMetaType::LongLong || type == QMetaType::ULongLong ||
                        type == QMetaType::Float || type == QMetaType::Double) &&
                       std::isfinite(it.value().toDouble())) {
                // These are the only numeric fields consumed by native layout.
            } else {
                setError("Canvas run contains an unknown field or a non-scalar field type");
                return false;
            }
        }
        if (textUnits > MaxTextUnits || inputUnits > 65536) {
            setError("Canvas input exceeds 8192 text or 65536 scalar UTF-16 units");
            return false;
        }
    }
    qsizetype fontUnits = font.styleName().size();
    for (const auto &family : font.families())
        fontUnits += family.size();
    if (fontUnits > 65536) {
        setError("Canvas font strings exceed their serialization budget");
        return false;
    }
    if ((d->atlasOnly && d->atlasConsumers.isEmpty()) || !isVisible() ||
        (window() && (window()->visibility() == QWindow::Hidden ||
                      window()->visibility() == QWindow::Minimized))) {
        // Hidden items retain only requested configuration. In particular they
        // must not enqueue a new font/raster job after hiding canceled one.
        const bool posesChanged = runs != d->runs && !d->poses.isEmpty();
        d->font = font;
        d->runs = runs;
        d->measurementOnly = false;
        if (posesChanged)
            d->poses.clear();
        const bool errorChanged = clearError && !d->error.isEmpty();
        d->publishingPreparation = true;
        if (clearError)
            setError({});
        d->publishingPreparation = false;
        QPointer<GlyphRun> self(this);
        const auto generation = d->preparationGeneration + 1;
        releaseResources();
        if (!self || generation != self->d->preparationGeneration)
            return true;
        emit self->layoutChanged();
        if (!self || generation != self->d->preparationGeneration)
            return true;
        if (posesChanged)
            emit self->posesChanged();
        if (!self || generation != self->d->preparationGeneration)
            return true;
        if (errorChanged)
            emit self->errorChanged();
        if (!self || generation != self->d->preparationGeneration)
            return true;
        emit self->renderingChanged();
        return true;
    }
    // Superseding a generation is one state transition. Publishing an
    // intermediate preparing=false would permit a receiver to install a newer
    // request before this older call has finished installing its own state.
    const bool wasPreparing = d->preparing;
    ++d->preparationGeneration;
    if (d->preparation)
        d->preparation->cancelled = true;
    d->preparation.reset();
    const auto generation = d->preparationGeneration;
    const auto failPreparation = [this](const QString &message) {
        d->preparing = false;
        d->publishingPreparation = true;
        const bool changed = d->error != message;
        setError(message);
        d->publishingPreparation = false;
        QPointer<GlyphRun> self(this);
        const auto failedGeneration = d->preparationGeneration;
        emit preparationChanged();
        if (!self || failedGeneration != d->preparationGeneration)
            return;
        if (changed)
            emit errorChanged();
        if (!self || failedGeneration != d->preparationGeneration)
            return;
        emit renderingChanged();
    };
    QByteArray serializedFont;
    QDataStream fontStream(&serializedFont, QIODevice::WriteOnly);
    fontStream << font;
    auto token = std::make_shared<PreparationToken>();
    token->memory = std::make_shared<GlyphCanvas::Memory>();
    if (!token->memory->resize(serializedFont.size() + inputUnits * 4 + runs.size() * 4096 +
                               4096)) {
        failPreparation("Canvas preparation exceeds the shared 128 MiB native budget");
        return false;
    }
    const auto ratio = window() ? window()->effectiveDevicePixelRatio() : 1;
    QPointer<GlyphRun> self(this);
    auto work = [self, token, generation, serializedFont, runs, ratio, clearError, textUnits,
                 maximumScale = d->maximumRasterScale]() {
        LayoutBuildResult layout;
        auto prepared = std::make_shared<GlyphCanvas::Prepared>();
        prepared->memory = std::make_shared<GlyphCanvas::Memory>();
        QString error;
        GlyphCanvas::Memory scratch;
        // One running job, never every queued job, reserves raster temporaries
        // (2 x 2048² RGBA8) plus 8 MiB for bounded path/metrics construction.
        if (!scratch.resize(40LL * 1024 * 1024))
            error = "Canvas scratch exceeds the shared native budget";
        {
            // QRawFont is thread-affine. Reconstruct the complete QFont and all
            // QTextLayouts on this worker; only pixels/paths/scalars escape.
            QFont workerFont;
            QDataStream reader(serializedFont);
            reader >> workerFont;
            if (error.isEmpty() &&
                !buildGlyphLayout(runs, workerFont, false, ratio, false, true, layout, error, true))
                layout = {};
            // One worker is the only writer of this weak pixel-intern table.
            // Weak entries never keep an unreferenced image or lease alive.
            static QHash<QByteArray, std::weak_ptr<const GlyphCanvas::Ink>> inkCache;
            for (auto it = inkCache.begin(); it != inkCache.end();) {
                if (it.value().expired())
                    it = inkCache.erase(it);
                else
                    ++it;
            }
            static QHash<QByteArray, std::weak_ptr<const GlyphCanvas::Outline>> outlineCache;
            for (auto it = outlineCache.begin(); it != outlineCache.end();) {
                if (it.value().expired())
                    it = outlineCache.erase(it);
                else
                    ++it;
            }
            qint64 metadataBytes =
                sizeof(GlyphCanvas::Prepared) + textUnits * 512 + runs.size() * 4096;
            if (!prepared->memory->resize(metadataBytes))
                error = "Canvas metadata exceeds the shared native budget";
            qsizetype pathElements = 0;
            for (auto &fragment : layout.fragments) {
                if (token->cancelled || !error.isEmpty())
                    break;
                GlyphCanvas::Fragment output;
                output.bounds = fragment.inkBounds;
                const auto maximumExtent =
                    std::hypot(output.bounds.width(), output.bounds.height()) * maximumScale *
                        ratio +
                    2;
                if (maximumExtent > 1024) {
                    error = "Canvas maximumRasterScale exceeds the four-target source capacity";
                    break;
                }
                QPointF outlineOrigin = fragment.inkBounds.topLeft();
                bool hasOrigin = false;
                QPainterPath outline = fragment.strokeMask;
                if (!outline.isEmpty())
                    outline.translate(-outlineOrigin);
                if (outline.isEmpty()) {
                    for (const auto &glyph : fragment.glyphs) {
                        const auto raw = glyph.rawFont();
                        const auto ids = glyph.glyphIndexes();
                        const auto positions = glyph.positions();
                        if (!hasOrigin && !positions.isEmpty()) {
                            outlineOrigin = positions.first();
                            hasOrigin = true;
                        }
                        for (qsizetype i = 0; i < ids.size(); ++i) {
                            const auto path = raw.pathForGlyph(ids[i]);
                            if (pathElements + path.elementCount() > 8LL * 1024 * 1024 / 32) {
                                error = "Canvas path construction exceeds its 8 MiB scratch budget";
                                break;
                            }
                            pathElements += path.elementCount();
                            outline.addPath(path.translated(positions[i] - outlineOrigin));
                        }
                    }
                }
                if (!error.isEmpty())
                    break;
                output.pathOrigin = outlineOrigin;
                QByteArray outlineBytes;
                QDataStream outlineStream(&outlineBytes, QIODevice::WriteOnly);
                outlineStream << outline;
                const auto outlineKey =
                    QCryptographicHash::hash(outlineBytes, QCryptographicHash::Sha256);
                auto sharedOutline = outlineCache.value(outlineKey).lock();
                if (!sharedOutline) {
                    auto value = std::make_shared<GlyphCanvas::Outline>();
                    value->memory = std::make_shared<GlyphCanvas::Memory>();
                    if (!value->memory->resize(sizeof(GlyphCanvas::Outline) +
                                               outline.elementCount() * 32 + outlineKey.size())) {
                        error = "Canvas outlines exceed the shared native budget";
                        break;
                    }
                    value->path = std::move(outline);
                    value->key = outlineKey;
                    sharedOutline = value;
                    outlineCache.insert(outlineKey, sharedOutline);
                }
                output.outline = std::move(sharedOutline);
                for (int level = 0; level < 4; ++level) {
                    const qreal density = ratio / (1 << level);
                    // Identical full-context glyphs, rasterized at four fixed
                    // densities. No font reconstruction or CPU blur per pose.
                    const QRectF rect = fragment.rect;
                    const QSize size(qCeil(rect.width() * density), qCeil(rect.height() * density));
                    const qint64 bytes = qint64(size.width()) * size.height() * 4;
                    auto memory = std::make_shared<GlyphCanvas::Memory>();
                    if (bytes > GlyphCanvas::InkBudget || !memory->resize(bytes)) {
                        error = "Canvas ink exceeds the shared native budget";
                        break;
                    }
                    QImage image(size, QImage::Format_ARGB32_Premultiplied);
                    image.setDevicePixelRatio(density);
                    image.fill(Qt::transparent);
                    const auto paint = [&](QImage &target, QColor color) {
                        QPainter painter(&target);
                        painter.setPen(color);
                        if (!fragment.strokeMask.isEmpty()) {
                            painter.setRenderHint(QPainter::Antialiasing);
                            painter.fillPath(fragment.strokeMask.translated(-rect.topLeft()),
                                             color);
                        } else
                            for (const auto &glyph : fragment.glyphs)
                                painter.drawGlyphRun(-rect.topLeft(), glyph);
                    };
                    paint(image, Qt::white);
                    if (level == 0) {
                        for (int y = 0; y < image.height() && !output.color; ++y)
                            for (int x = 0; x < image.width(); ++x) {
                                const auto p = image.pixel(x, y);
                                if (qRed(p) != qAlpha(p) || qGreen(p) != qAlpha(p) ||
                                    qBlue(p) != qAlpha(p)) {
                                    output.color = true;
                                    break;
                                }
                            }
                        if (!output.color) {
                            QImage black(size, QImage::Format_ARGB32_Premultiplied);
                            black.setDevicePixelRatio(density);
                            black.fill(Qt::transparent);
                            paint(black, Qt::black);
                            output.color = black == image &&
                                           std::any_of(image.constBits(),
                                                       image.constBits() + image.sizeInBytes(),
                                                       [](uchar v) { return v != 0; });
                        }
                    }
                    image = image.convertToFormat(QImage::Format_RGBA8888_Premultiplied);
                    QCryptographicHash hash(QCryptographicHash::Sha256);
                    hash.addData(QByteArray::number(size.width()) + ":" +
                                 QByteArray::number(size.height()) + ":");
                    hash.addData(QByteArrayView(reinterpret_cast<const char *>(image.constBits()),
                                                image.sizeInBytes()));
                    const auto key = hash.result();
                    auto existing = inkCache.value(key).lock();
                    if (!existing) {
                        auto ink = std::make_shared<GlyphCanvas::Ink>();
                        ink->image = std::move(image);
                        ink->memory = std::move(memory);
                        ink->key = key;
                        existing = ink;
                        inkCache.insert(key, existing);
                    }
                    output.ink[level] = std::move(existing);
                    // QImage rounds its physical allocation up. Map all of
                    // those pixels back at the requested density; squeezing
                    // them into the unrounded logical box distorts small mips.
                    output.rect[level] = QRectF(rect.topLeft(), QSizeF(size) / density);
                }
                metadataBytes += sizeof(GlyphCanvas::Fragment);
                if (!prepared->memory->resize(metadataBytes)) {
                    error = "Canvas paths exceed the shared native budget";
                    break;
                }
                prepared->fragments.append(std::move(output));
            }
            // Destruction of every worker-created QRawFont happens here.
            for (auto &fragment : layout.fragments) {
                fragment.glyphs.clear();
                fragment.strokeMask = {};
            }
        }
        for (const auto &entry : runs) {
            const auto run = entry.toMap();
            const auto key = run["key"].toString();
            layout.blurLimits[key] = run.value("maxBlur", 32).toInt();
            layout.haloLimits[key] = run.value("maxHaloRadius", 0).toInt();
        }
        QSet<const GlyphCanvas::Ink *> unique;
        prepared->bytes = prepared->memory->size();
        // Immutable pixels/outlines are shared and are reported once by the
        // window ledger, rather than charged again for every item/context.
        Q_UNUSED(unique);
        return PreparationQueue::Completion([self, token, generation, layout = std::move(layout),
                                             prepared = std::move(prepared), error, ratio,
                                             clearError]() mutable {
            if (!self || token->cancelled || generation != self->d->preparationGeneration)
                return;
            auto &data = *self->d;
            const auto previousError = data.error;
            data.publishingPreparation = true;
            data.preparing = false;
            data.preparation.reset();
            if (!error.isEmpty()) {
                self->setError(error);
                data.metrics.clear();
                data.fragments.clear();
                data.boundaries.clear();
                data.blurLimits.clear();
                data.haloLimits.clear();
                data.draws.clear();
                data.canvas.reset();
                data.atlases.clear();
                data.effectsAtlases.clear();
                data.released = true;
                data.canvasStatus->ready = false;
                data.canvasStatus->batches = 0;
                if (data.atlasSnapshot)
                    *data.atlasSnapshot->valid = false;
                data.atlasSnapshot.reset();
                data.staticCache.reset();
                ++data.revision;
            } else {
                data.metrics = std::move(layout.metrics);
                data.fragments = std::move(layout.fragments);
                data.boundaries = std::move(layout.boundaries);
                data.blurLimits = std::move(layout.blurLimits);
                data.haloLimits = std::move(layout.haloLimits);
                data.canvas = std::move(prepared);
                data.atlases.clear();
                data.effectsAtlases.clear();
                data.dpr = ratio;
                data.released = false;
                if (data.atlasSnapshot)
                    *data.atlasSnapshot->valid = false;
                data.atlasSnapshot.reset();
                data.staticCache.reset();
                ++data.revision;
                data.draws.clear();
                for (int i = 0; i < data.fragments.size(); ++i)
                    data.draws.append({i, false, {}});
                ++data.topologyRevision;
                const auto poses = data.poses;
                data.poses.clear();
                if (clearError)
                    self->setError({});
                self->applyPoses(poses, false);
            }
            data.publishingPreparation = false;
            self->update();
            // Publish only after the complete generation has been committed.
            // A synchronous receiver can replace/destroy this item at any signal.
            const bool errorChanged = previousError != data.error;
            emit self->layoutChanged();
            if (!self || generation != self->d->preparationGeneration)
                return;
            emit self->posesChanged();
            if (!self || generation != self->d->preparationGeneration)
                return;
            emit self->preparationChanged();
            if (!self || generation != self->d->preparationGeneration)
                return;
            if (errorChanged)
                emit self->errorChanged();
            if (!self || generation != self->d->preparationGeneration)
                return;
            emit self->renderingChanged();
        });
    };
    if (!PreparationQueue::instance()->enqueue({this, token, std::move(work)})) {
        failPreparation("Canvas preparation queue exceeds 32 queued/running jobs");
        return false;
    }
    const bool posesChanged = runs != d->runs && !d->poses.isEmpty();
    d->font = font;
    d->runs = runs;
    if (posesChanged)
        d->poses.clear();
    d->measurementOnly = false;
    d->preparation = std::move(token);
    d->preparing = true;
    d->canvasStatus->ready = false;
    const bool errorChanged = clearError && !d->error.isEmpty();
    d->publishingPreparation = true;
    if (clearError)
        setError({});
    d->publishingPreparation = false;
    if (!wasPreparing)
        emit preparationChanged();
    if (!self || generation != self->d->preparationGeneration)
        return true;
    if (posesChanged)
        emit self->posesChanged();
    if (!self || generation != self->d->preparationGeneration)
        return true;
    if (errorChanged)
        emit self->errorChanged();
    if (!self || generation != self->d->preparationGeneration)
        return true;
    emit self->renderingChanged();
    return true;
}
void GlyphRun::setError(const QString &message)
{
    if (d->error == message)
        return;
    d->error = message;
    if (!d->publishingPreparation) {
        emit errorChanged();
        emit renderingChanged();
    }
}
void GlyphRun::setFont(const QFont &font)
{
    if (font != d->font)
        rebuild(d->runs, font);
    else
        setError({});
}
void GlyphRun::setRuns(const QVariantList &runs)
{
    if (runs != d->runs || (d->haloSampling == "canvas" && d->released))
        rebuild(runs, d->font);
    else
        setError({});
}
bool GlyphRun::rebuild(const QVariantList &runs, const QFont &baseFont)
{
    return rebuild(runs, baseFont, d->measurementOnly);
}
void GlyphRun::rebuildForEnvironment()
{
    if (d->haloSampling == "canvas" && !d->measurementOnly) {
        // Asynchronous completion restores poses after the new layout exists.
        prepareCanvas(d->runs, d->font, false);
        return;
    }
    const auto poses = d->poses;
    if (rebuild(d->runs, d->font, d->measurementOnly, false))
        applyPoses(poses, false);
}
bool GlyphRun::rebuild(const QVariantList &runs, const QFont &baseFont, bool measurementOnly,
                       bool clearErrorOnSuccess)
{
    const qreal dpr = window() ? window()->effectiveDevicePixelRatio() : 1;
    if (d->haloSampling == "canvas" && !measurementOnly)
        return prepareCanvas(runs, baseFont, clearErrorOnSuccess);
    cancelPreparation();
    d->canvas.reset();
    LayoutBuildResult built;
    QString error;
    if (!buildGlyphLayout(runs, baseFont, measurementOnly, dpr,
                          isVisible() && (!d->atlasOnly || !d->atlasConsumers.isEmpty()), false,
                          built, error)) {
        setError(error);
        return false;
    }
    d->font = baseFont;
    d->measurementOnly = measurementOnly;
    d->runs = runs;
    d->metrics = std::move(built.metrics);
    d->fragments = std::move(built.fragments);
    d->atlases = std::move(built.atlases);
    d->effectsAtlases = std::move(built.effectsAtlases);
    d->boundaries = std::move(built.boundaries);
    d->blurLimits = std::move(built.blurLimits);
    d->haloLimits = std::move(built.haloLimits);
    d->dpr = dpr;
    d->released = !isVisible() || (d->atlasOnly && d->atlasConsumers.isEmpty());
    if (d->atlasSnapshot)
        *d->atlasSnapshot->valid = false;
    d->atlasSnapshot.reset();
    d->staticCache.reset();
    ++d->revision;
    d->poses.clear();
    d->draws.clear();
    for (int index = 0; index < d->fragments.size(); ++index)
        d->draws.append({index, false, {}});
    ++d->topologyRevision;
    if (clearErrorOnSuccess)
        setError({});
    update();
    emit layoutChanged();
    emit posesChanged();
    return true;
}
void GlyphRun::setPoses(const QVariantList &poses)
{
    applyPoses(poses, true);
}
void GlyphRun::applyPoses(const QVariantList &poses, bool clearErrorOnSuccess)
{
    if (d->measurementOnly && !poses.isEmpty()) {
        setError("measurementOnly does not accept rendering poses");
        return;
    }
    if (poses == d->poses) {
        if (clearErrorOnSuccess)
            setError({});
        return;
    }
    if (poses.size() > MaxFragments) {
        setError("Too many poses (maximum 2048)");
        return;
    }
    QSet<QString> parsed;
    QHash<QString, QList<Pose>> byTarget;
    int order = 0;
    for (const auto &value : poses) {
        const auto map = value.toMap();
        const QString key = map.value("key").toString();
        const int start = map.contains("start") ? map.value("start").toInt() : -1;
        if (!d->boundaries.contains(key) || (start != -1 && !d->boundaries[key].contains(start)) ||
            (map.contains("start") && (!finiteRange(map, "start", 0, MaxTextUnits) ||
                                       map.value("start").toDouble() != start)) ||
            !finiteRange(map, "x", -1000000, 1000000) ||
            !finiteRange(map, "y", -1000000, 1000000) || !finiteRange(map, "scale", 0, 32) ||
            !finiteRange(map, "rotation", -360000, 360000) || !finiteRange(map, "opacity", 0, 1) ||
            !finiteRange(map, "blur", 0, 64) || !finiteRange(map, "glow", 0, 4) ||
            map.value("blur", 0).toDouble() > d->blurLimits.value(key) ||
            (map.value("glow", 0).toDouble() > 0 && d->blurLimits.value(key) == 0)) {
            setError("Pose references an unknown run/cluster or contains an invalid number");
            return;
        }
        Pose pose;
        if (map.contains("instance") &&
            map.value("instance").metaType().id() != QMetaType::QString) {
            setError("Pose instance must be a string");
            return;
        }
        pose.instance = map.value("instance").toString();
        if (pose.instance.size() > 128 || pose.instance.contains(QChar(0x1f)) ||
            pose.instance.contains(QChar(0x1e))) {
            setError("Pose instance exceeds 128 UTF-16 units or contains a reserved character");
            return;
        }
        pose.order = order++;
        if (map.contains("clip")) {
            const auto clip = map.value("clip").toMap();
            if (clip.size() != 4 || !clip.contains("x") || !clip.contains("y") ||
                !clip.contains("width") || !clip.contains("height") ||
                !finiteRange(clip, "x", -1000000, 1000000) ||
                !finiteRange(clip, "y", -1000000, 1000000) ||
                !finiteRange(clip, "width", 0, 1000000) ||
                !finiteRange(clip, "height", 0, 1000000)) {
                setError("Pose clip must contain finite bounded x/y/width/height");
                return;
            }
            pose.clipped = true;
            pose.clip = {clip.value("x").toDouble(), clip.value("y").toDouble(),
                         clip.value("width").toDouble(), clip.value("height").toDouble()};
        }
        pose.x = map.value("x", 0).toDouble();
        pose.y = map.value("y", 0).toDouble();
        pose.scale = map.value("scale", 1).toDouble();
        pose.rotation = map.value("rotation", 0).toDouble();
        pose.opacity = map.value("opacity", 1).toDouble();
        pose.blur = map.value("blur", 0).toDouble();
        pose.glow = map.value("glow", 0).toDouble();
        if (d->haloSampling == "canvas" && (pose.blur > 0 || pose.glow > 0)) {
            setError("Canvas sampling accepts clear ink and independent halos; body blur/glow use "
                     "static sampling");
            return;
        }
        if (map.contains("color")) {
            pose.color = map.value("color").value<QColor>();
            if (!pose.color.isValid()) {
                setError("Pose color is invalid");
                return;
            }
        }
        if (map.contains("tint")) {
            pose.tint = map.value("tint").value<QColor>();
            if (!pose.tint.isValid()) {
                setError("Pose tint is invalid");
                return;
            }
        }
        const auto halos = map.value("halos").toList();
        if (halos.size() > 3) {
            setError("At most three halo layers are supported");
            return;
        }
        if (!halos.isEmpty() && d->haloLimits.value(key) <= 0) {
            setError("This run has no halo texture budget");
            return;
        }
        for (const auto &entry : halos) {
            const auto layer = entry.toMap();
            if (!layer.contains("radius") ||
                !finiteRange(layer, "radius", 0, d->haloLimits.value(key)) ||
                !finiteRange(layer, "opacity", 0, 1)) {
                setError("Halo radius or opacity exceeds the declared run budget");
                return;
            }
            Halo halo;
            halo.radius = layer.value("radius").toDouble();
            halo.opacity = layer.value("opacity", 1).toDouble();
            if (layer.contains("color"))
                halo.color = layer.value("color").value<QColor>();
            if (!halo.color.isValid()) {
                setError("Halo color is invalid");
                return;
            }
            pose.halos.append(halo);
        }
        const auto target = poseKey(key, start);
        const auto id = target + QChar(0x1e) + pose.instance;
        if (parsed.contains(id)) {
            setError("Duplicate run/cluster pose");
            return;
        }
        parsed.insert(id);
        byTarget[target].append(pose);
    }
    QList<Draw> draws;
    const qint64 maximumQuads = MaxGeometryBytes / (6 * qint64(sizeof(Vertex)));
    for (int index = 0; index < d->fragments.size(); ++index) {
        const auto &fragment = d->fragments[index];
        const auto individual = byTarget.constFind(poseKey(fragment.key, fragment.start));
        const auto whole = byTarget.constFind(poseKey(fragment.key, -1));
        const auto found = individual != byTarget.cend() ? individual : whole;
        const int count = found == byTarget.cend() ? 1 : int(found->size());
        if (draws.size() + count > maximumQuads) {
            setError("Expanded pose instances exceed the 8 MiB geometry budget");
            return;
        }
        if (found == byTarget.cend())
            draws.append({index, false, {}});
        else
            for (const auto &pose : *found)
                draws.append({index, individual != byTarget.cend(), pose});
    }
    std::stable_sort(draws.begin(), draws.end(),
                     [](const Draw &a, const Draw &b) { return a.pose.order < b.pose.order; });
    bool sameTopology = draws.size() == d->draws.size();
    for (int index = 0; sameTopology && index < draws.size(); ++index)
        sameTopology = draws[index].fragment == d->draws[index].fragment;
    if (!sameTopology)
        ++d->topologyRevision;
    d->poses = poses;
    d->draws = std::move(draws);
    if (clearErrorOnSuccess)
        setError({});
    update();
    if (!d->publishingPreparation)
        emit posesChanged();
}
QSGNode *GlyphRun::updatePaintNode(QSGNode *old, UpdatePaintNodeData *)
{
    if (d->atlasOnly) {
        delete old;
        return nullptr;
    }
    if (d->haloSampling == "canvas") {
        if (d->released || !d->canvas || !isVisible()) {
            delete old;
            return nullptr;
        }
        QList<GlyphCanvas::Draw> draws;
        for (const auto &draw : d->draws) {
            const auto &fragment = d->fragments[draw.fragment];
            const auto &pose = draw.pose;
            const QPointF anchor =
                draw.individual ? fragment.anchor : QPointF(0, fragment.runBaseline);
            GlyphCanvas::Draw output;
            output.fragment = draw.fragment;
            output.transform.translate(anchor.x() + pose.x, anchor.y() + pose.y);
            output.transform.rotate(pose.rotation);
            output.transform.scale(pose.scale, pose.scale);
            output.transform.translate(-anchor.x(), -anchor.y());
            output.color = pose.color;
            output.tint = pose.tint;
            output.opacity = pose.opacity;
            output.blur = pose.blur;
            output.glow = pose.glow;
            output.clipped = pose.clipped;
            output.clip = pose.clip;
            for (const auto &halo : pose.halos)
                output.halos.append({halo.radius, halo.opacity, halo.color});
            draws.append(std::move(output));
        }
        QPointer<GlyphRun> self(this);
        const auto generation = d->preparationGeneration;
        auto *target = window();
        d->canvasStatus->changed = [self, target, generation](const QString &message) {
            QMetaObject::invokeMethod(
                target,
                [self, generation, message] {
                    if (!self || generation != self->d->preparationGeneration)
                        return;
                    if (!message.isEmpty()) {
                        auto &data = *self->d;
                        const bool errorChanged = data.error != message;
                        const bool wasPreparing = data.preparing;
                        // Finish the failed generation before notifying QML.
                        // A receiver may synchronously submit a recovery run.
                        data.publishingPreparation = true;
                        self->setError(message);
                        self->releaseResources();
                        data.publishingPreparation = false;
                        const auto retiredGeneration = data.preparationGeneration;
                        emit self->layoutChanged();
                        if (!self || retiredGeneration != self->d->preparationGeneration)
                            return;
                        if (wasPreparing)
                            emit self->preparationChanged();
                        if (!self || retiredGeneration != self->d->preparationGeneration)
                            return;
                        if (errorChanged)
                            emit self->errorChanged();
                        if (!self || retiredGeneration != self->d->preparationGeneration)
                            return;
                    }
                    emit self->renderingChanged();
                },
                Qt::QueuedConnection);
        };
        return GlyphCanvas::updateNode(old, window(), d->canvas, draws, d->canvasStatus, d->screen,
                                       d->maximumRasterScale, d->atlasDomain);
    }
    auto *root = dynamic_cast<RunRoot *>(old);
    if (!root)
        delete old;
    QMutexLocker lock(&d->gpuMutex);
    if (d->released || d->atlases.isEmpty()) {
        delete root;
        d->gpuPages.clear();
        d->batches.store(0);
        return nullptr;
    }
    if (!root)
        root = new RunRoot;
    const bool layoutChanged = root->revision != d->revision;
    if (layoutChanged || root->topologyRevision != d->topologyRevision) {
        while (auto *child = root->firstChild()) {
            root->removeChildNode(child);
            delete child;
        }
        root->batches.clear();
        if (layoutChanged) {
            d->gpuPages.clear();
            if (!d->staticCache || !*d->staticCache->valid)
                d->staticCache = std::make_shared<StaticAtlasCache>();
            auto images = d->atlases;
            images.append(d->effectsAtlases);
            int pageIndex = 0;
            auto upload = [&](const QList<QImage> &part) {
                QList<std::shared_ptr<TexturePage>> pages;
                for (int i = 0; i < part.size(); ++i) {
                    bool uploaded = false;
                    auto page =
                        staticAtlasPage(d->staticCache, window(), images, pageIndex++, uploaded);
                    pages.append(page);
                    d->gpuPages.append(page);
                    d->uploads.fetch_add(uploaded);
                }
                return pages;
            };
            root->bodies = upload(d->atlases);
            root->effects = upload(d->effectsAtlases);
        }
        // Consecutive batches preserve the explicit instance paint order even across atlas pages.
        for (int index = 0; index < d->draws.size(); ++index) {
            const auto &fragment = d->fragments[d->draws[index].fragment];
            if (root->batches.isEmpty() || root->batches.last().body != fragment.page ||
                root->batches.last().bodyEffects != fragment.bodyEffectsPage ||
                root->batches.last().halo != fragment.haloPage) {
                Batch batch;
                batch.body = fragment.page;
                batch.bodyEffects = fragment.bodyEffectsPage;
                batch.halo = fragment.haloPage;
                root->batches.append(batch);
            }
            root->batches.last().draws.append(index);
        }
        for (auto &batch : root->batches) {
            auto *node = new QSGGeometryNode;
            auto *geometry = new QSGGeometry(attributes(), int(batch.draws.size()) * 6);
            geometry->setDrawingMode(QSGGeometry::DrawTriangles);
            geometry->setVertexDataPattern(QSGGeometry::DynamicPattern);
            node->setGeometry(geometry);
            node->setFlag(QSGNode::OwnsGeometry);
            auto *material = new RunMaterial;
            material->page = root->bodies[batch.body];
            material->bodyEffects = batch.bodyEffects >= 0 ? root->effects[batch.bodyEffects]
                                                           : root->bodies[batch.body];
            material->halo = batch.halo >= 0 ? root->effects[batch.halo] : root->bodies[batch.body];
            node->setMaterial(material);
            node->setFlag(QSGNode::OwnsMaterial);
            root->appendChildNode(node);
            batch.node = node;
        }
        root->revision = d->revision;
        root->topologyRevision = d->topologyRevision;
        d->batches.store(int(root->batches.size()));
        // Basic render loops also synchronize on the GUI thread. Do not let a
        // QML receiver re-enter preparation/resource retirement under gpuMutex.
        QMetaObject::invokeMethod(this, &GlyphRun::renderingChanged, Qt::QueuedConnection);
    }
    for (const auto &batch : root->batches) {
        auto *material = static_cast<RunMaterial *>(batch.node->material());
        if (material->screen != d->screen) {
            material->screen = d->screen;
            batch.node->markDirty(QSGNode::DirtyMaterial);
        }
        auto *output = static_cast<Vertex *>(batch.node->geometry()->vertexData());
        for (const int index : batch.draws) {
            const auto &draw = d->draws[index];
            const auto &fragment = d->fragments[draw.fragment];
            const auto &pose = draw.pose;
            const QPointF anchor =
                draw.individual ? fragment.anchor : QPointF(0, fragment.runBaseline);
            QTransform transform;
            transform.translate(anchor.x() + pose.x, anchor.y() + pose.y);
            transform.rotate(pose.rotation);
            transform.scale(pose.scale, pose.scale);
            transform.translate(-anchor.x(), -anchor.y());
            qreal activeHalo = 0;
            for (const auto &halo : pose.halos)
                activeHalo = std::max(activeHalo, halo.radius);
            QRectF quad = fragment.rect;
            const qreal activeBlur =
                std::max(pose.blur, pose.glow > 0 ? std::min(fragment.radius, qreal(8)) : qreal(0));
            const qreal padding = std::max(activeHalo, activeBlur) * 3 + 2;
            quad |= fragment.inkBounds.adjusted(-padding, -padding, padding, padding);
            const QSize bodySize = d->atlases[fragment.page].size();
            const QSize bodyEffectsSize = fragment.bodyEffectsPage >= 0
                                              ? d->effectsAtlases[fragment.bodyEffectsPage].size()
                                              : bodySize;
            const QSize haloSize =
                fragment.haloPage >= 0 ? d->effectsAtlases[fragment.haloPage].size() : bodySize;
            auto vertex = [&](const QPointF &position) {
                Vertex result{};
                const auto point = transform.map(position);
                result.position[0] = float(point.x());
                result.position[1] = float(point.y());
                result.runLocal[0] = float(position.x());
                result.runLocal[1] = float(position.y());
                result.clipBounds[0] = pose.clipped ? float(pose.clip.left()) : 1;
                result.clipBounds[1] = pose.clipped ? float(pose.clip.top()) : 1;
                result.clipBounds[2] = pose.clipped ? float(pose.clip.right()) : 0;
                result.clipBounds[3] = pose.clipped ? float(pose.clip.bottom()) : 0;
                auto coordinates = [&](const QRectF &rect, const QRect &pixels, const QSize &size,
                                       float *uv, float *bounds) {
                    uv[0] = float(
                        (pixels.x() + (position.x() - rect.x()) / rect.width() * pixels.width()) /
                        size.width());
                    uv[1] = float(
                        (pixels.y() + (position.y() - rect.y()) / rect.height() * pixels.height()) /
                        size.height());
                    bounds[0] = float(qreal(pixels.x()) / size.width());
                    bounds[1] = float(qreal(pixels.y()) / size.height());
                    bounds[2] = float(qreal(pixels.x() + pixels.width()) / size.width());
                    bounds[3] = float(qreal(pixels.y() + pixels.height()) / size.height());
                };
                coordinates(fragment.rect, fragment.pixels, bodySize, result.uv, result.bounds);
                result.uv[2] = float(pose.opacity);
                result.uv[3] = float(pose.tint.redF());
                result.bodyEffectsUv[2] = float(pose.tint.greenF());
                result.bodyEffectsUv[3] = float(pose.tint.blueF());
                if (fragment.bodyEffectsPage >= 0)
                    coordinates(fragment.bodyEffectsRect, fragment.bodyEffectsPixels,
                                bodyEffectsSize, result.bodyEffectsUv, result.bodyEffectsBounds);
                if (fragment.haloPage >= 0)
                    coordinates(fragment.haloRect, fragment.haloPixels, haloSize, result.haloUv,
                                result.haloBounds);
                result.color[0] = float(pose.color.redF());
                result.color[1] = float(pose.color.greenF());
                result.color[2] = float(pose.color.blueF());
                result.color[3] = float(pose.color.alphaF());
                result.effects[0] = float(pose.blur);
                result.effects[1] = float(pose.glow);
                result.effects[2] = float(fragment.radius);
                result.effects[3] = float(fragment.haloRadius + (fragment.colorInk ? 2048 : 0));
                result.haloRadii[3] = float(pose.halos.size());
                for (int layer = 0; layer < pose.halos.size(); ++layer) {
                    const auto &halo = pose.halos[layer];
                    result.haloRadii[layer] = float(halo.radius);
                    result.haloColor[layer][0] = float(halo.color.redF());
                    result.haloColor[layer][1] = float(halo.color.greenF());
                    result.haloColor[layer][2] = float(halo.color.blueF());
                    result.haloColor[layer][3] = float(halo.color.alphaF() * halo.opacity);
                }
                return result;
            };
            const auto a = vertex(quad.topLeft()), b = vertex(quad.topRight());
            const auto c = vertex(quad.bottomLeft()), e = vertex(quad.bottomRight());
            *output++ = a;
            *output++ = c;
            *output++ = b;
            *output++ = b;
            *output++ = c;
            *output++ = e;
        }
        batch.node->geometry()->markVertexDataDirty();
        batch.node->markDirty(QSGNode::DirtyGeometry);
    }
    return root;
}
void GlyphRun::releaseResources()
{
    if (d->haloSampling == "canvas") {
        // These values are owned by the Prepared metadata lease. Clear them
        // before dropping that lease, rather than keeping unaccounted copies.
        d->metrics.clear();
        d->fragments.clear();
        d->boundaries.clear();
        d->blurLimits.clear();
        d->haloLimits.clear();
        d->draws.clear();
    }
    d->canvas.reset();
    d->canvasStatus->ready = false;
    auto *job = new ReleasePages;
    {
        QMutexLocker lock(&d->gpuMutex);
        job->pages = d->gpuPages;
        d->gpuPages.clear();
    }
    if (window() && !job->pages.isEmpty())
        window()->scheduleRenderJob(job, QQuickWindow::BeforeSynchronizingStage);
    else
        delete job;
    d->atlases.clear();
    d->effectsAtlases.clear();
    d->released = true;
    if (d->atlasSnapshot)
        *d->atlasSnapshot->valid = false;
    d->atlasSnapshot.reset();
    d->staticCache.reset();
    ++d->revision;
    d->batches.store(0);
    update();
    // Publish cancellation only after the released state is complete. A
    // synchronous receiver may show this item and start a new generation.
    QPointer<GlyphRun> self(this);
    cancelPreparation();
    if (self)
        emit atlasChanged();
}
void GlyphRun::itemChange(ItemChange change, const ItemChangeData &value)
{
    QQuickItem::itemChange(change, value);
    if (change == ItemVisibleHasChanged && d) {
        if (!value.boolValue) {
            releaseResources();
            emit layoutChanged();
            emit renderingChanged();
        } else if (d->released) {
            rebuildForEnvironment();
        }
    }
}
} // namespace EchoMusic
