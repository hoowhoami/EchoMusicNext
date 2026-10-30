#include "GlyphRunCanvas_p.h"
namespace EchoMusic::GlyphCanvas
{
namespace
{
std::atomic<qint64> liveBytes{0}, peakBytes{0};
}
Memory::~Memory()
{
    liveBytes.fetch_sub(bytes_, std::memory_order_relaxed);
}
bool Memory::resize(qint64 bytes)
{
    if (bytes < 0)
        return false;
    const auto change = bytes - bytes_;
    auto total = liveBytes.load(std::memory_order_relaxed);
    do {
        if (change > 0 && total > NativeBudget - change)
            return false;
    } while (!liveBytes.compare_exchange_weak(total, total + change, std::memory_order_relaxed));
    bytes_ = bytes;
    auto peak = peakBytes.load(std::memory_order_relaxed);
    const auto current = total + change;
    while (peak < current &&
           !peakBytes.compare_exchange_weak(peak, current, std::memory_order_relaxed)) {
    }
    return true;
}
qint64 Memory::total()
{
    return liveBytes.load(std::memory_order_relaxed);
}
qint64 Memory::highWater()
{
    return peakBytes.load(std::memory_order_relaxed);
}
} // namespace EchoMusic::GlyphCanvas

#include "GlyphRunCurveGeometry_p.h"
#include <QFile>
#include <QMatrix4x4>
#include <QMutexLocker>
#include <QUuid>
#include <QQuickWindow>
#include <QSGRenderNode>
#include <QtQuick/private/qquickwindow_p.h>
#include <QtQuick/private/qsgrenderer_p.h>
#include <QtQuick/private/qsgcurvefillnode_p.h>
#include <rhi/qrhi.h>
#include <cmath>
#include <cstring>
#include <map>
#include <limits>
#include <vector>
namespace EchoMusic::GlyphCanvas
{
// A halo contributes only inside its finite sampled rectangle. Use the current
// pass projection; a layer/sourceRect or RenderControl target can reveal pixels
// outside the window. Nonpositive-w and nonfinite projections stay conservative.
bool isHaloOutsideTarget(const QRectF &bounds, const QMatrix4x4 &deviceToClip)
{
    int outside = 15;
    for (const auto point :
         {bounds.topLeft(), bounds.topRight(), bounds.bottomLeft(), bounds.bottomRight()}) {
        const auto p = deviceToClip * QVector4D(float(point.x()), float(point.y()), 0, 1);
        if (!std::isfinite(p.x()) || !std::isfinite(p.y()) || !std::isfinite(p.z()) ||
            !std::isfinite(p.w()) || p.w() <= 0)
            return false;
        outside &= (p.x() < -p.w() ? 1 : 0) | (p.x() > p.w() ? 2 : 0) | (p.y() < -p.w() ? 4 : 0) |
                   (p.y() > p.w() ? 8 : 0);
    }
    return outside != 0;
}
namespace
{
constexpr int MaxFramePasses = 32;
constexpr int MaxMainPipelines = 32;
struct alignas(16) Params
{
    std::array<float, 16> mvp{}, device{}, inverse{};
    QVector4D output, input, mapping, prior, options, font;
    std::array<QVector4D, 4> rect, uv;
    std::array<QVector4D, 3> path, field;
    QVector4D quad, fill, tint;
    std::array<QVector4D, 3> shadow;
    QVector4D clip, alphas, channels;
};
static_assert(sizeof(Params) == 656);
struct alignas(16) FilterParams
{
    std::array<float, 16> mvp;
    QVector4D output, input, mapping, prior, options;
};
static_assert(sizeof(FilterParams) == 144);
struct ParamStore
{
    QRhi *rhi = nullptr;
    QByteArray bytes;
    QList<quint32> offsets;
    void clear()
    {
        bytes.clear();
        offsets.clear();
    }
    qsizetype size() const
    {
        return offsets.size();
    }
    void append(const Params &p)
    {
        appendBytes(&p, sizeof(p));
    }
    void appendFilter(const Params &p)
    {
        const FilterParams f{p.mvp, p.output, p.input, p.mapping, p.prior, p.options};
        appendBytes(&f, sizeof(f));
    }
    void appendBytes(const void *data, int size)
    {
        const int offset = rhi->ubufAligned(bytes.size());
        bytes.resize(offset + rhi->ubufAligned(size));
        memcpy(bytes.data() + offset, data, size);
        offsets.append(offset);
    }
};
bool transparent(const Draw &draw)
{
    if (draw.opacity <= 0)
        return true;
    if (draw.color.alphaF() > 0 || draw.glow > 0)
        return false;
    return std::all_of(draw.halos.cbegin(), draw.halos.cend(), [](const Layer &halo) {
        return halo.opacity <= 0 || halo.color.alphaF() <= 0;
    });
}
struct Step
{
    QSize in, out;
    QPointF delta, prior;
    float scale = 1;
};
struct Plan
{
    QRect source;
    QList<Step> steps;
    QSize low, padded, stored;
    QRectF back;
    double density = 1, sigma = 1, lowSigma = 1;
    int window = 1, border = 0, pad = 0;
    bool path = false, outline = false;
};
Plan makePlan(const Fragment &input, const QTransform &device, double sigma)
{
    Plan p;
    p.source = device.mapRect(input.bounds).toAlignedRect();
    p.sigma = std::max(.001, sigma);
    p.window = std::max(1, int(std::floor(p.sigma * 3 * std::sqrt(2 * M_PI) / 4 + .5)));
    p.border = p.window % 2 ? 3 * ((p.window - 1) / 2) : 3 * (p.window / 2) - 1;
    p.path = p.source.width() + 2 * p.border > 256 || p.source.height() + 2 * p.border > 256;
    p.outline = p.path && input.outline && !input.outline->path.isEmpty();
    const float net = p.path ? std::min(1.f, float(4 / p.sigma)) : 1;
    int count = std::ceil(std::log2(1 / net));
    if (count > 0 && net * (1 << (count - 1)) >= (count == 1 ? .999f : .9f))
        --count;
    QRectF logical(p.source);
    QRect pixels = p.source;
    float accumulated = 1;
    for (int i = 0; i < count; ++i) {
        const float scale = i + 1 < count ? .5f : net / accumulated;
        const auto c = logical.center();
        const QRectF f((logical.x() - c.x()) * scale, (logical.y() - c.y()) * scale,
                       logical.width() * scale, logical.height() * scale);
        const auto next = f.toAlignedRect();
        p.steps.append({pixels.size(), next.size(), next.topLeft() - f.topLeft(),
                        logical.topLeft() - pixels.topLeft(), scale});
        logical = f;
        pixels = next;
        accumulated *= scale;
    }
    p.low = pixels.size();
    p.lowSigma = p.sigma * accumulated;
    p.pad = p.path ? std::ceil(3 * p.lowSigma) + 1 : p.border;
    p.padded = p.low + QSize(2 * p.pad, 2 * p.pad);
    p.back = {(pixels.x() - p.pad - logical.x()) / accumulated + p.source.x(),
              (pixels.y() - p.pad - logical.y()) / accumulated + p.source.y(),
              p.padded.width() / accumulated, p.padded.height() / accumulated};
    const auto post = p.path ? 1. : std::min(1., 4. / p.sigma);
    p.stored = {qCeil(p.padded.width() * post), qCeil(p.padded.height() * post)};
    return p;
}
QShader loadShader(const QString &name)
{
    QFile f(":/qt/qml/EchoMusic/PluginSdk/shaders/glyph-canvas/" + name + ".qsb");
    return f.open(QIODevice::ReadOnly) ? QShader::fromSerialized(f.readAll()) : QShader{};
}
struct Shelf
{
    int x = 0, y = 0, h = 0;
    bool place(QSize size, int side, QRect &out)
    {
        size += QSize(2, 2);
        if (size.width() > side || size.height() > side)
            return false;
        if (x + size.width() > side) {
            x = 0;
            y += h;
            h = 0;
        }
        if (y + size.height() > side)
            return false;
        out = {x + 1, y + 1, size.width() - 2, size.height() - 2};
        x += size.width();
        h = std::max(h, size.height());
        return true;
    }
};
struct InkLocation
{
    int page = 0;
    QRect pixels;
};
struct InkPage
{
    std::unique_ptr<QRhiTexture> texture;
    QList<QRect> free{{0, 0, 1024, 1024}};
    std::shared_ptr<Memory> memory;
    bool place(QSize size, QRect &out)
    {
        size += QSize(2, 2);
        int selected = -1, area = INT_MAX;
        for (int i = 0; i < free.size(); ++i)
            if (free[i].width() >= size.width() && free[i].height() >= size.height()) {
                const int a = free[i].width() * free[i].height();
                if (a < area) {
                    selected = i;
                    area = a;
                }
            }
        if (selected < 0)
            return false;
        const auto r = free.takeAt(selected);
        const QRect right(r.x() + size.width(), r.y(), r.width() - size.width(), size.height());
        const QRect bottom(r.x(), r.y() + size.height(), r.width(), r.height() - size.height());
        if (!right.isEmpty())
            free.append(right);
        if (!bottom.isEmpty())
            free.append(bottom);
        out = {r.x() + 1, r.y() + 1, size.width() - 2, size.height() - 2};
        return true;
    }
    void release(QRect rect)
    {
        free.append(rect.adjusted(-1, -1, 1, 1));
        bool merged = true;
        while (merged) {
            merged = false;
            for (int i = 0; i < free.size() && !merged; ++i)
                for (int j = i + 1; j < free.size(); ++j) {
                    const auto a = free[i], b = free[j];
                    if ((a.y() == b.y() && a.height() == b.height() &&
                         (a.right() + 1 == b.x() || b.right() + 1 == a.x())) ||
                        (a.x() == b.x() && a.width() == b.width() &&
                         (a.bottom() + 1 == b.y() || b.bottom() + 1 == a.y()))) {
                        free[i] = a.united(b);
                        free.removeAt(j);
                        merged = true;
                        break;
                    }
                }
        }
    }
};
struct InkEntry
{
    InkLocation location;
    std::weak_ptr<const Ink> source;
};
struct Field
{
    QRect pixels;
    int channel = 0;
};
struct Curve
{
    std::unique_ptr<QSGCurveFillNode> node;
    std::shared_ptr<const Outline> source;
    std::shared_ptr<Memory> memory;
};
class Pool final : public std::enable_shared_from_this<Pool>
{
public:
    QString key =
        QStringLiteral("glyph-canvas:") + QUuid::createUuid().toString(QUuid::WithoutBraces);
    QQuickWindow *window = nullptr;
    QRhi *rhi = nullptr;
    std::array<std::unique_ptr<QRhiTexture>, 4> textures;
    std::array<std::unique_ptr<QRhiTextureRenderTarget>, 4> targets;
    std::array<std::unique_ptr<QRhiRenderPassDescriptor>, 4> passes;
    std::unique_ptr<QRhiSampler> sampler;
    std::unique_ptr<QSGRenderer> renderer;
    std::unique_ptr<QSGRootNode> root;
    QSGTransformNode *transform = nullptr;
    QSGCurveFillNode *selected = nullptr;
    QList<std::shared_ptr<InkPage>> inkPages;
    QHash<QByteArray, InkEntry> locations;
    QImage zero;
    QHash<QByteArray, std::weak_ptr<Curve>> curveCache;
    QHash<const void *, qint64> nodeCpuBytes, nodeEffectsBytes;
    std::array<Shelf, 4> fields;
    QMetaObject::Connection frameConnection, renderConnection;
    std::shared_ptr<Memory> memory = std::make_shared<Memory>();
    quint64 frame = 0;
    bool passRendered = false;
    qint64 colorBytes = 0;
    bool inkInitialized = false;
    bool initialized = false;
    ~Pool()
    {
        QObject::disconnect(frameConnection);
        QObject::disconnect(renderConnection);
        select(nullptr);
        if (renderer)
            renderer->setRootNode(nullptr);
        renderer.reset();
        root.reset();
    }
    void select(QSGCurveFillNode *node)
    {
        if (selected == node)
            return;
        if (selected)
            transform->removeChildNode(selected);
        selected = node;
        if (node)
            transform->appendChildNode(node);
    }
    qint64 bytes() const
    {
        qint64 bytes = memory->size();
        for (const auto &page : inkPages)
            bytes += page->memory->size();
        for (const auto &entry : locations)
            if (auto source = entry.source.lock())
                bytes += source->memory->size();
        for (const auto &weak : curveCache)
            if (auto curve = weak.lock())
                bytes += curve->memory->size() + curve->source->memory->size();
        for (const auto value : nodeCpuBytes)
            bytes += value;
        return bytes;
    }
    bool reserveMetadata(int additionalLocations = 0, int additionalCurves = 0,
                         int additionalNodes = 0)
    {
        // Include retained hash capacities, not only live entry counts. The
        // prospective growth allowance is reserved before any insertion.
        const qint64 metadata =
            sizeof(Pool) + 4096 + (locations.capacity() + 4LL * additionalLocations) * 512 +
            (curveCache.capacity() + 4LL * additionalCurves) * 256 +
            (nodeCpuBytes.capacity() + nodeEffectsBytes.capacity() + 8LL * additionalNodes) * 128;
        return memory->resize(colorBytes + 4LL * 1024 * 1024 + metadata);
    }
    qint64 effectsBytes() const
    {
        qint64 total = memory->size();
        for (const auto &weak : curveCache)
            if (auto curve = weak.lock())
                total += curve->memory->size();
        for (const auto value : nodeEffectsBytes)
            total += value;
        return total;
    }
    std::shared_ptr<Curve> curveFor(const std::shared_ptr<const Outline> &outline)
    {
        for (auto it = curveCache.begin(); it != curveCache.end();) {
            if (it.value().expired())
                it = curveCache.erase(it);
            else
                ++it;
        }
        auto curve = curveCache.value(outline->key).lock();
        if (curve)
            return curve;
        if (curveCache.size() >= 8192 || !reserveMetadata(0, 1))
            return {};
        // Reserve a bounded construction scratch before curve tessellation.
        Memory scratch;
        if (!scratch.resize(8LL * 1024 * 1024))
            return {};
        curve = std::make_shared<Curve>();
        curve->source = outline;
        qint64 size = 0;
        if (!outline->path.isEmpty()) {
            curve->node.reset(makeGlyphCurveNode(outline->path));
            if (const auto *g = curve->node->geometry())
                size = g->vertexCount() * g->sizeOfVertex() + g->indexCount() * g->sizeOfIndex();
        }
        if (size > 4LL * 1024 * 1024 || effectsBytes() + sizeof(Curve) + size * 2 > EffectsBudget)
            return {};
        curve->memory = std::make_shared<Memory>();
        if (!curve->memory->resize(sizeof(Curve) + size * 2))
            return {};
        curveCache[outline->key] = curve;
        return curve;
    }
    bool initInk(QQuickWindow *w)
    {
        if (inkInitialized)
            return rhi == w->rhi();
        window = w;
        rhi = w->rhi();
        if (!rhi || !reserveMetadata())
            return false;
        zero = QImage({1024, 1024}, QImage::Format_RGBA8888_Premultiplied);
        zero.fill(Qt::transparent);
        inkInitialized = true;
        return true;
    }
    bool init(QQuickWindow *w)
    {
        if (!initInk(w))
            return false;
        colorBytes = 20LL * 1024 * 1024;
        if (!rhi || !rhi->isTextureFormatSupported(QRhiTexture::RGBA16F, QRhiTexture::RenderTarget))
            return false;
        if (!reserveMetadata())
            return false;
        zero = QImage({1024, 1024}, QImage::Format_RGBA8888_Premultiplied);
        zero.fill(Qt::transparent);
        sampler.reset(rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None,
                                      QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
        if (!sampler->create())
            return false;
        for (int i = 0; i < 4; ++i) {
            textures[i].reset(rhi->newTexture(i == 3 ? QRhiTexture::RGBA16F : QRhiTexture::RGBA8,
                                              {1024, 1024}, 1, QRhiTexture::RenderTarget));
            if (!textures[i]->create())
                return false;
            targets[i].reset(
                rhi->newTextureRenderTarget(QRhiTextureRenderTargetDescription(textures[i].get()),
                                            i == 3 ? QRhiTextureRenderTarget::PreserveColorContents
                                                   : QRhiTextureRenderTarget::Flags{}));
            passes[i].reset(targets[i]->newCompatibleRenderPassDescriptor());
            targets[i]->setRenderPassDescriptor(passes[i].get());
            if (!targets[i]->create())
                return false;
        }
        auto *context = QQuickWindowPrivate::get(w)->context;
        renderer.reset(context->createRenderer(QSGRendererInterface::RenderMode2DNoDepthBuffer));
        root = std::make_unique<QSGRootNode>();
        transform = new QSGTransformNode;
        root->appendChildNode(transform);
        renderer->setRootNode(root.get());
        auto weak = weak_from_this();
        const auto advanceFrame = [weak] {
            if (auto pool = weak.lock()) {
                pool->fields = {};
                pool->passRendered = false;
                ++pool->frame;
            }
        };
        // RenderControl may render multiple scenes inside one QRhi frame.
        // Its beginFrame signal is the UBO reuse boundary. Ordinary windows
        // also support grabWindow(), whose separate QRhi frames do not emit
        // beforeFrameBegin; beforeRendering covers both their grab and swapchain frames.
        frameConnection = QObject::connect(
            w, &QQuickWindow::beforeFrameBegin, w,
            [w, advanceFrame] {
                if (QQuickWindowPrivate::get(w)->renderControl)
                    advanceFrame();
            },
            Qt::DirectConnection);
        renderConnection = QObject::connect(
            w, &QQuickWindow::beforeRendering, w,
            [w, advanceFrame] {
                if (!QQuickWindowPrivate::get(w)->renderControl)
                    advanceFrame();
            },
            Qt::DirectConnection);
        initialized = true;
        return true;
    }
    bool ink(const std::shared_ptr<const Ink> &ink, InkLocation &location,
             QRhiResourceUpdateBatch *updates, bool &uploaded)
    {
        const auto found = locations.find(ink->key);
        if (found != locations.end()) {
            location = found->location;
            found->source = ink;
            return true;
        }
        // Live source owners pin their atlas slots. Expired entries release their
        // rectangles; a stable digest prevents recycled-address false hits.
        for (auto it = locations.begin(); it != locations.end();) {
            if (it->source.expired()) {
                inkPages[it->location.page]->release(it->location.pixels);
                it = locations.erase(it);
            } else
                ++it;
        }
        if (locations.size() >= 8192 || !reserveMetadata(1))
            return false;
        QRect pixels;
        int page = 0;
        for (; page < inkPages.size(); ++page)
            if (inkPages[page]->place(ink->image.size(), pixels))
                break;
        if (page == inkPages.size()) {
            if (page >= 8 || ink->image.width() + 2 > 1024 || ink->image.height() + 2 > 1024)
                return false;
            auto target = std::make_shared<InkPage>();
            target->memory = std::make_shared<Memory>();
            if (!target->memory->resize(1024 * 1024 * 4 + sizeof(InkPage)))
                return false;
            target->texture.reset(rhi->newTexture(QRhiTexture::RGBA8, {1024, 1024}));
            if (!target->texture->create())
                return false;
            target->place(ink->image.size(), pixels);
            inkPages.append(target);
            updates->uploadTexture(target->texture.get(), zero);
        }
        // Clear the one-pixel sampling border when reusing a retired rectangle.
        QRhiTextureSubresourceUploadDescription clear(zero);
        clear.setSourceSize(pixels.size() + QSize(2, 2));
        clear.setDestinationTopLeft(pixels.topLeft() - QPoint(1, 1));
        QRhiTextureSubresourceUploadDescription sub(ink->image);
        sub.setDestinationTopLeft(pixels.topLeft());
        updates->uploadTexture(inkPages[page]->texture.get(),
                               QRhiTextureUploadDescription({QRhiTextureUploadEntry(0, 0, clear),
                                                             QRhiTextureUploadEntry(0, 0, sub)}));
        location = {page, pixels};
        locations.insert(ink->key, {location, ink});
        uploaded = true;
        return true;
    }
    bool field(QSize size, Field &out)
    {
        for (int i = 0; i < 4; ++i) {
            auto shelf = fields[i];
            QRect rect;
            if (shelf.place(size, 1024, rect)) {
                fields[i] = shelf;
                out = {rect, i};
                return true;
            }
        }
        return false;
    }
    void curve(QSGCurveFillNode *curve, const QTransform &device, const Plan &plan,
               QRhiCommandBuffer *cb)
    {
        select(curve);
        QTransform t = device;
        t *= QTransform::fromTranslate(-plan.source.x(), -plan.source.y());
        transform->setMatrix(QMatrix4x4(t));
        root->markDirty(QSGNode::DirtyForceUpdate);
        renderer->nodeChanged(root.get(), QSGNode::DirtyForceUpdate);
        renderer->setDevicePixelRatio(1);
        renderer->setDeviceRect({1024, 1024});
        renderer->setViewportRect(plan.source.size());
        QSGAbstractRenderer::MatrixTransformFlags flags;
        if (!rhi->isYUpInNDC())
            flags |= QSGAbstractRenderer::MatrixTransformFlipY;
        renderer->setProjectionMatrixToRect(QRectF(QPointF(0, 0), plan.source.size()), flags);
        renderer->setInvertFrontFace(false);
        renderer->setClearColor(Qt::transparent);
        renderer->setRenderTarget({targets[0].get(), passes[0].get(), cb});
        QQuickWindowPrivate::get(window)->context->renderNextFrame(renderer.get());
    }
};
std::shared_ptr<Pool> windowPool(QQuickWindow *window, const QString &domain)
{
    static QMutex mutex;
    static QHash<QPair<QQuickWindow *, QString>, std::weak_ptr<Pool>> pools;
    QMutexLocker lock(&mutex);
    for (auto it = pools.begin(); it != pools.end();)
        if (it.value().expired())
            it = pools.erase(it);
        else
            ++it;
    const auto key = qMakePair(window, domain);
    auto pool = pools.value(key).lock();
    if (!pool || (pool->rhi && pool->rhi != window->rhi())) {
        pool = std::make_shared<Pool>();
        pools[key] = pool;
    }
    return pool;
}
class CanvasNode final : public QSGRenderNode
{
public:
    QQuickWindow *window = nullptr;
    QString domain;
    std::shared_ptr<const Prepared> input;
    QList<Draw> draws;
    std::shared_ptr<Status> status;
    std::shared_ptr<Pool> pool;
    qreal maximumScale = 32;
    bool screen = false;
    std::shared_ptr<Memory> memory = std::make_shared<Memory>();
    // Host writes to Dynamic UBOs accumulate until the QRhi frame ends.
    // A later layer/capture must use a different retained buffer and SRBs.
    struct UniformSlot
    {
        std::unique_ptr<QRhiBuffer> uniforms;
        QHash<quint64, std::shared_ptr<QRhiShaderResourceBindings>> bindings;
        int bytes = 0;
    };
    std::vector<std::unique_ptr<UniformSlot>> uniformSlots;
    UniformSlot *uniformSlot = nullptr;
    quint64 uniformFrame = std::numeric_limits<quint64>::max();
    int nextUniformSlot = 0;
    std::array<std::unique_ptr<QRhiGraphicsPipeline>, 4> pipelines;
    std::array<std::unique_ptr<QRhiGraphicsPipeline>, 4> finalPipelines;
    struct MainPipeline
    {
        std::unique_ptr<QRhiRenderPassDescriptor> pass;
        std::unique_ptr<QRhiGraphicsPipeline> pipeline;
        int samples = 1;
    };
    std::vector<std::unique_ptr<MainPipeline>> mainPipelines;
    QRhiGraphicsPipeline *mainPipeline = nullptr;
    qint64 descriptorBytes = 0;
    QList<std::shared_ptr<Curve>> curves;
    qint64 geometryBytes = 0;
    struct Operation
    {
        int kind = 0, src = 0, dst = 0, slot = 0;
        QSize extent;
        Field field;
    };
    struct Effect
    {
        int fragment = 0;
        QTransform device;
        Plan plan;
        QList<Operation> ops;
    };
    QList<Effect> effects;
    struct Main
    {
        int slot = 0;
        quint64 binding = 0;
    };
    QList<Main> main;
    ParamStore params;
    QList<std::array<InkLocation, 4>> ink;
    int stride = 0, allocatedBytes = 0;
    QRectF bounds;
    CanvasNode(QQuickWindow *w) : window(w) {}
    ~CanvasNode() override
    {
        releaseResources();
    }
    QRectF rect() const override
    {
        return bounds;
    }
    RenderingFlags flags() const override
    {
        return NoExternalRendering | DepthAwareRendering | BoundedRectRendering;
    }
    StateFlags changedStates() const override
    {
        return ViewportState | ScissorState;
    }
    void releaseResources() override
    {
        if (pool) {
            for (auto &curve : curves)
                if (pool->selected == curve->node.get())
                    pool->select(nullptr);
            pool->nodeCpuBytes.remove(this);
            pool->nodeEffectsBytes.remove(this);
        }
        curves.clear();
        uniformSlots.clear();
        uniformSlot = nullptr;
        uniformFrame = std::numeric_limits<quint64>::max();
        nextUniformSlot = 0;
        mainPipelines.clear();
        mainPipeline = nullptr;
        descriptorBytes = 0;
        for (auto &p : pipelines)
            p.reset();
        for (auto &p : finalPipelines)
            p.reset();
        allocatedBytes = 0;
        memory->resize(0);
        pool.reset();
        if (status) {
            status->ready = false;
            status->gpuBytes = 0;
            status->sharedBytes = 0;
            status->batches = 0;
        }
    }
    void report(const QString &error)
    {
        const auto notify = status->changed;
        releaseResources();
        input.reset();
        if (notify)
            notify(error);
    }
    Params base(QSize size, const Fragment &f, const std::array<InkLocation, 4> &locations,
                const QTransform &device, qreal scale)
    {
        Params p;
        p.output = {float(size.width()), float(size.height()), 0, 0};
        p.options = {f.color ? 1.f : 0.f, float(scale), float(window->effectiveDevicePixelRatio()),
                     0};
        QMatrix4x4 ortho;
        ortho.ortho(0, size.width(), size.height(), 0, -1, 1);
        const auto m = pool->rhi->clipSpaceCorrMatrix() * ortho;
        memcpy(p.mvp.data(), m.constData(), 64);
        const QMatrix4x4 dm(device), im(device.inverted());
        memcpy(p.device.data(), dm.constData(), 64);
        memcpy(p.inverse.data(), im.constData(), 64);
        for (int i = 0; i < 4; ++i) {
            auto r = f.rect[i];
            p.rect[i] = {float(r.x()), float(r.y()), float(r.width()), float(r.height())};
            const auto q = locations[i].pixels;
            p.uv[i] = {q.x() / 1024.f, q.y() / 1024.f, q.width() / 1024.f, q.height() / 1024.f};
        }
        return p;
    }
    quint64 bindingKey(int fragment, int source, bool full = true) const
    {
        quint64 key = source | (full ? quint64(1) << 20 : 0);
        for (int i = 0; i < 4; ++i)
            key |= quint64(ink[fragment][i].page) << (3 + 3 * i);
        return key;
    }
    QRhiShaderResourceBindings *binding(int fragment, int source, bool full = true)
    {
        const auto key = bindingKey(fragment, source, full);
        auto existing = uniformSlot->bindings.value(key);
        if (existing)
            return existing.get();
        auto s =
            std::shared_ptr<QRhiShaderResourceBindings>(pool->rhi->newShaderResourceBindings());
        s->setBindings(
            {QRhiShaderResourceBinding::uniformBufferWithDynamicOffset(
                 0,
                 QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage,
                 uniformSlot->uniforms.get(), full ? sizeof(Params) : sizeof(FilterParams)),
             QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage,
                                                       pool->textures[source].get(),
                                                       pool->sampler.get()),
             QRhiShaderResourceBinding::sampledTexture(
                 2, QRhiShaderResourceBinding::FragmentStage,
                 pool->inkPages[ink[fragment][0].page]->texture.get(), pool->sampler.get()),
             QRhiShaderResourceBinding::sampledTexture(
                 3, QRhiShaderResourceBinding::FragmentStage,
                 pool->inkPages[ink[fragment][1].page]->texture.get(), pool->sampler.get()),
             QRhiShaderResourceBinding::sampledTexture(
                 4, QRhiShaderResourceBinding::FragmentStage,
                 pool->inkPages[ink[fragment][2].page]->texture.get(), pool->sampler.get()),
             QRhiShaderResourceBinding::sampledTexture(
                 5, QRhiShaderResourceBinding::FragmentStage,
                 pool->inkPages[ink[fragment][3].page]->texture.get(), pool->sampler.get())});
        if (!s->create())
            return nullptr;
        uniformSlot->bindings.insert(key, s);
        return s.get();
    }
    std::unique_ptr<QRhiGraphicsPipeline> pipeline(QString fragment, QRhiRenderPassDescriptor *rp,
                                                   int samples, bool blend, int channel = -1)
    {
        auto ps = std::unique_ptr<QRhiGraphicsPipeline>(pool->rhi->newGraphicsPipeline());
        ps->setTopology(QRhiGraphicsPipeline::TriangleStrip);
        ps->setSampleCount(samples);
        ps->setDepthTest(false);
        ps->setDepthWrite(false);
        ps->setCullMode(QRhiGraphicsPipeline::None);
        const bool full = fragment == "ink" || fragment == "compose";
        ps->setShaderStages(
            {{QRhiShaderStage::Vertex, loadShader(full ? "quad.vert" : "filter.vert")},
             {QRhiShaderStage::Fragment, loadShader(fragment + ".frag")}});
        ps->setShaderResourceBindings(binding(0, 0, full));
        ps->setRenderPassDescriptor(rp);
        QRhiGraphicsPipeline::TargetBlend b;
        if (channel >= 0)
            b.colorWrite = QRhiGraphicsPipeline::ColorMask(1 << channel);
        if (blend) {
            ps->setFlags(QRhiGraphicsPipeline::UsesScissor);
            b.enable = true;
            b.srcColor = QRhiGraphicsPipeline::One;
            b.dstColor = screen ? QRhiGraphicsPipeline::OneMinusSrcColor
                                : QRhiGraphicsPipeline::OneMinusSrcAlpha;
            b.srcAlpha = QRhiGraphicsPipeline::One;
            b.dstAlpha = QRhiGraphicsPipeline::OneMinusSrcAlpha;
        }
        ps->setTargetBlends({b});
        if (!ps->create())
            return {};
        return ps;
    }
    void prepare() override;
    void render(const RenderState *state) override
    {
        if (!status->ready || !mainPipeline)
            return;
        pool->passRendered = true;
        auto *cb = commandBuffer();
        const auto size = renderTarget()->pixelSize();
        cb->setGraphicsPipeline(mainPipeline);
        cb->setViewport({0, 0, float(size.width()), float(size.height())});
        const auto scissor = state->scissorEnabled() ? state->scissorRect()
                                                     : QRect(0, 0, size.width(), size.height());
        cb->setScissor({scissor.x(), scissor.y(), scissor.width(), scissor.height()});
        for (const auto &draw : main) {
            const QRhiCommandBuffer::DynamicOffset offset{0, params.offsets[draw.slot]};
            cb->setShaderResources(uniformSlot->bindings.value(draw.binding).get(), 1, &offset);
            cb->draw(4);
        }
    }
};
void CanvasNode::prepare()
{
    if (!input || input->fragments.isEmpty()) {
        const bool changed = !status->ready.exchange(true) || status->batches.load() != 0;
        status->batches = 0;
        if (changed && status->changed)
            status->changed({});
        return;
    }
    if (pool && pool->rhi && pool->rhi != window->rhi())
        releaseResources();
    if (!pool)
        pool = windowPool(window, domain);
    if (!pool->initialized && !pool->init(window)) {
        report("Canvas four-target initialization exceeds the native budget or is unsupported");
        return;
    }
    // All nodes prepare before their render pass. Reuse halo fields only
    // after that pass has consumed them, never between its preparing nodes.
    if (pool->passRendered) {
        pool->fields = {};
        pool->passRendered = false;
    }
    auto *updates = pool->rhi->nextResourceUpdateBatch();
    ink.resize(input->fragments.size());
    for (int i = 0; i < input->fragments.size(); ++i)
        for (int level = 0; level < 4; ++level) {
            bool uploaded = false;
            if (!input->fragments[i].ink[level] ||
                !pool->ink(input->fragments[i].ink[level], ink[i][level], updates, uploaded)) {
                updates->release();
                report("Canvas masks exceed the eight-page 32 MiB ink texture budget");
                return;
            }
            if (uploaded)
                status->uploads.fetch_add(1);
        }
    commandBuffer()->resourceUpdate(updates);
    if (curves.empty()) {
        for (const auto &fragment : input->fragments) {
            auto curve = pool->curveFor(fragment.outline);
            if (!curve) {
                report("Canvas curve preparation exceeds the bounded native budget");
                return;
            }
            curves.append(std::move(curve));
        }
    }
    const qreal dpr = window->effectiveDevicePixelRatio();
    const auto parent = matrix()->toTransform();
    const auto deviceToClip =
        *projectionMatrix() * QMatrix4x4(QTransform::fromScale(1 / dpr, 1 / dpr));
    qint64 forecast = 0, operationCount = 0, effectCount = 0, mainCount = 0;
    for (const auto &draw : draws) {
        if (transparent(draw) || !draw.transform.isInvertible())
            continue;
        const auto device = draw.transform * parent * QTransform::fromScale(dpr, dpr);
        const qreal a = device.m11() * device.m11() + device.m12() * device.m12(),
                    b = device.m21() * device.m21() + device.m22() * device.m22(),
                    c = device.m11() * device.m21() + device.m12() * device.m22();
        const qreal density = std::sqrt((a + b + std::sqrt((a - b) * (a - b) + 4 * c * c)) / 2);
        // Parent transforms are outside pose validation. Reject them before
        // blur planning converts sigma to integers or computes pyramid shifts.
        if (!std::isfinite(density) || density / dpr > maximumScale + 1e-6) {
            report("Canvas transform exceeds maximumRasterScale");
            return;
        }
        forecast += pool->rhi->ubufAligned(sizeof(Params));
        ++mainCount;
        for (const auto &halo : draw.halos)
            if (halo.radius > 0 && halo.opacity > 0 && halo.color.alphaF() > 0) {
                const auto plan =
                    makePlan(input->fragments[draw.fragment], device, halo.radius * density);
                if (isHaloOutsideTarget(plan.back, deviceToClip))
                    continue;
                const auto filters = plan.steps.size() + 2;
                forecast += filters * pool->rhi->ubufAligned(sizeof(FilterParams));
                if (!plan.outline)
                    forecast += pool->rhi->ubufAligned(sizeof(Params));
                operationCount += filters + (plan.outline ? 0 : 1);
                ++effectCount;
            }
    }
    forecast = std::max(forecast, qint64(pool->rhi->ubufAligned(sizeof(Params))));
    Memory bindingScratch;
    if (!bindingScratch.resize(input->fragments.size() * 8LL * 128 + sizeof(QSet<quint64>))) {
        report("Canvas binding forecast exceeds the native budget");
        return;
    }
    QSet<quint64> prospectiveBindings;
    for (int i = 0; i < input->fragments.size(); ++i)
        for (int source = 0; source < 4; ++source) {
            prospectiveBindings.insert(bindingKey(i, source));
            prospectiveBindings.insert(bindingKey(i, source, false));
        }
    if (uniformFrame != pool->frame) {
        uniformFrame = pool->frame;
        nextUniformSlot = 0;
    }
    const int slotIndex = nextUniformSlot++;
    if (slotIndex >= MaxFramePasses) {
        report("Canvas exceeds 32 render passes per node in one frame");
        return;
    }
    int pipelineIndex = -1;
    for (size_t i = 0; i < mainPipelines.size(); ++i)
        if (mainPipelines[i]->samples == renderTarget()->sampleCount() &&
            mainPipelines[i]->pass->isCompatible(renderTarget()->renderPassDescriptor())) {
            pipelineIndex = int(i);
            break;
        }
    if (pipelineIndex < 0 && mainPipelines.size() >= MaxMainPipelines) {
        report("Canvas exceeds 32 render target pipeline formats");
        return;
    }
    const int pipelineCount = mainPipelines.size() + (pipelineIndex < 0 ? 1 : 0);
    const int slotCount = std::max(int(uniformSlots.size()), slotIndex + 1);
    const int priorBytes =
        slotIndex < int(uniformSlots.size()) ? uniformSlots[slotIndex]->bytes : 0;
    const qint64 prospectiveGpu =
        allocatedBytes - priorBytes + std::max(qint64(priorBytes), forecast);
    const qint64 descriptors =
        pipelineCount * 1024 + draws.capacity() * sizeof(Draw) +
        operationCount * sizeof(Operation) + effectCount * sizeof(Effect) +
        mainCount * sizeof(Main) +
        slotCount * (prospectiveBindings.size() * 1024 + sizeof(UniformSlot) + sizeof(void *) * 2) +
        (operationCount + mainCount) * sizeof(quint32);
    if (!pool->reserveMetadata(0, 0, pool->nodeCpuBytes.contains(this) ? 0 : 1)) {
        report("Canvas shared metadata exceeds the native budget");
        return;
    }
    const qint64 required = prospectiveGpu + forecast + std::max(descriptorBytes, descriptors);
    if (pool->effectsBytes() - pool->nodeEffectsBytes.value(this) + required +
                bindingScratch.size() >
            EffectsBudget ||
        forecast > 8LL * 1024 * 1024 || !memory->resize(required)) {
        report("Canvas frame metadata exceeds the shared effects/native budget");
        return;
    }
    descriptorBytes = std::max(descriptorBytes, descriptors);
    pool->nodeEffectsBytes[this] = required;
    effects.clear();
    main.clear();
    params.clear();
    params.rhi = pool->rhi;
    params.bytes.reserve(forecast);
    bounds = {};
    for (const auto &draw : draws) {
        if (transparent(draw) || !draw.transform.isInvertible())
            continue;
        const auto &fragment = input->fragments[draw.fragment];
        const auto device = draw.transform * parent * QTransform::fromScale(dpr, dpr);
        const qreal a = device.m11() * device.m11() + device.m12() * device.m12(),
                    b = device.m21() * device.m21() + device.m22() * device.m22(),
                    c = device.m11() * device.m21() + device.m12() * device.m22();
        const qreal density = std::sqrt((a + b + std::sqrt((a - b) * (a - b) + 4 * c * c)) / 2),
                    scale = density / dpr;
        if (scale > maximumScale + 1e-6) {
            report("Canvas transform exceeds maximumRasterScale");
            return;
        }
        QRectF quad = fragment.rect[0];
        struct Shadow
        {
            Plan plan;
            Field field;
            QColor color;
            qreal opacity;
        };
        QList<Shadow> shadows;
        for (const auto &halo : draw.halos) {
            if (halo.radius <= 0 || halo.opacity <= 0 || halo.color.alphaF() <= 0)
                continue;
            auto p = makePlan(fragment, device, halo.radius * density);
            if (p.source.width() > 1024 || p.source.height() > 1024 || p.padded.width() > 1024 ||
                p.padded.height() > 1024) {
                report("Canvas source exceeds the four-target sampling capacity");
                return;
            }
            // Keep conservative item bounds even when this pass cannot see the
            // halo. A later capture in the same frame can use another projection.
            quad |= device.inverted().mapRect(p.back);
            if (isHaloOutsideTarget(p.back, deviceToClip))
                continue;
            Field field;
            if (!pool->field(p.stored, field)) {
                report("Canvas halos exceed the four-target 32 MiB effects texture budget");
                return;
            }
            shadows.append({p, field, halo.color, halo.opacity});
        }
        bounds |= draw.transform.mapRect(quad);
        for (const auto &shadow : shadows) {
            const auto &p = shadow.plan;
            Effect effect;
            effect.fragment = draw.fragment;
            effect.device = device;
            effect.plan = p;
            auto v = base(p.source.size(), fragment, ink[draw.fragment], device, scale);
            v.font = {float(density), float(p.source.x()), float(p.source.y()), 0};
            if (!p.outline) {
                effect.ops.append({2, 3, 0, int(params.size()), p.source.size(), {}});
                params.append(v);
            }
            int previous = 0;
            for (const auto &step : p.steps) {
                v = base(step.out, fragment, ink[draw.fragment], device, scale);
                v.input = {float(step.in.width()), float(step.in.height()), 1024, 1024};
                v.mapping = {step.scale, float(step.delta.x()), float(step.delta.y()), 0};
                v.prior = {float(step.prior.x()), float(step.prior.y()), 0, 0};
                const int output = previous == 1 ? 2 : 1;
                effect.ops.append({0, previous, output, int(params.size()), step.out, {}});
                params.appendFilter(v);
                previous = output;
            }
            v = base(p.padded, fragment, ink[draw.fragment], device, scale);
            v.input = {float(p.low.width()), float(p.low.height()), 1024, 1024};
            v.mapping.setW(p.pad);
            v.prior = {0, 0, 0, float(p.lowSigma)};
            v.options.setX(p.path ? 0 : p.window);
            const int xOutput = previous == 1 ? 2 : 1;
            effect.ops.append({1, previous, xOutput, int(params.size()), p.padded, {}});
            params.appendFilter(v);
            auto y = v;
            y.input = {float(p.padded.width()), float(p.padded.height()), 1024, 1024};
            y.mapping.setW(0);
            y.prior.setZ(1);
            y.prior.setY(1);
            y.output = {float(p.stored.width()), float(p.stored.height()), float(p.padded.width()),
                        float(p.padded.height())};
            QMatrix4x4 ortho;
            ortho.ortho(0, p.stored.width(), p.stored.height(), 0, -1, 1);
            const auto ym = pool->rhi->clipSpaceCorrMatrix() * ortho;
            memcpy(y.mvp.data(), ym.constData(), 64);
            effect.ops.append({3, xOutput, 3, int(params.size()), p.stored, shadow.field});
            params.appendFilter(y);
            effects.append(std::move(effect));
        }
        auto v = base({1, 1}, fragment, ink[draw.fragment], device, scale);
        const auto m = *projectionMatrix() * *matrix() * QMatrix4x4(draw.transform) *
                       QMatrix4x4(QTransform::fromTranslate(quad.x(), quad.y())) *
                       QMatrix4x4(QTransform::fromScale(quad.width(), quad.height()));
        memcpy(v.mvp.data(), m.constData(), 64);
        v.output = {1, 1, 0, 0};
        v.input = {0, 0, 1024, 1024};
        v.quad = {float(quad.x()), float(quad.y()), float(quad.width()), float(quad.height())};
        v.fill = {float(draw.color.redF()), float(draw.color.greenF()), float(draw.color.blueF()),
                  float(draw.color.alphaF())};
        v.tint = {float(draw.tint.redF()), float(draw.tint.greenF()), float(draw.tint.blueF()), 1};
        v.alphas = {float(draw.opacity * inheritedOpacity()), 0, float(shadows.size()),
                    draw.clipped ? 1.f : 0.f};
        v.clip = {float(draw.clip.left()), float(draw.clip.top()), float(draw.clip.right()),
                  float(draw.clip.bottom())};
        for (int i = 0; i < shadows.size(); ++i) {
            const auto &shadow = shadows[i];
            const auto &p = shadow.plan;
            const auto r = shadow.field.pixels;
            v.path[i] = {float(p.back.x()), float(p.back.y()), float(p.back.width()),
                         float(p.back.height())};
            v.field[i] = {float(r.x()), float(r.y()), float(r.width()), float(r.height())};
            v.shadow[i] = {float(shadow.color.redF()), float(shadow.color.greenF()),
                           float(shadow.color.blueF()),
                           float(shadow.color.alphaF() * shadow.opacity)};
            v.channels[i] = shadow.field.channel;
        }
        main.append({int(params.size()), bindingKey(draw.fragment, 3)});
        params.append(v);
    }
    const qint64 bufferBytes =
        std::max(pool->rhi->ubufAligned(sizeof(Params)), int(params.bytes.size()));
    if (slotIndex == int(uniformSlots.size()))
        uniformSlots.push_back(std::make_unique<UniformSlot>());
    uniformSlot = uniformSlots[slotIndex].get();
    if (!uniformSlot->uniforms || uniformSlot->bytes < bufferBytes) {
        uniformSlot->bindings.clear();
        uniformSlot->uniforms.reset(
            pool->rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, bufferBytes));
        if (!uniformSlot->uniforms->create()) {
            report("Canvas uniform allocation failed");
            return;
        }
        allocatedBytes += bufferBytes - uniformSlot->bytes;
        uniformSlot->bytes = bufferBytes;
    }
    for (int i = 0; i < input->fragments.size(); ++i)
        for (int source = 0; source < 4; ++source)
            if (!binding(i, source) || !binding(i, source, false)) {
                report("Canvas resource binding failed");
                return;
            }
    if (!pipelines[0]) {
        pipelines[0] = pipeline("rescale", pool->passes[0].get(), 1, false);
        pipelines[1] = pipeline("blur", pool->passes[0].get(), 1, false);
        pipelines[2] = pipeline("ink", pool->passes[0].get(), 1, false);
        for (int c = 0; c < 4; ++c)
            finalPipelines[c] = pipeline("blur", pool->passes[3].get(), 1, false, c);
        if (!pipelines[0] || !pipelines[1] || !pipelines[2] ||
            std::any_of(finalPipelines.cbegin(), finalPipelines.cend(),
                        [](const auto &p) { return !p; })) {
            report("Canvas shader pipeline creation failed");
            return;
        }
    }
    if (pipelineIndex < 0) {
        auto entry = std::make_unique<MainPipeline>();
        entry->pass.reset(
            renderTarget()->renderPassDescriptor()->newCompatibleRenderPassDescriptor());
        entry->samples = renderTarget()->sampleCount();
        entry->pipeline = pipeline("compose", entry->pass.get(), entry->samples, true);
        if (!entry->pipeline) {
            report("Canvas target pipeline creation failed");
            return;
        }
        pipelineIndex = mainPipelines.size();
        mainPipelines.push_back(std::move(entry));
    }
    mainPipeline = mainPipelines[pipelineIndex]->pipeline.get();
    updates = pool->rhi->nextResourceUpdateBatch();
    if (!params.bytes.isEmpty())
        updates->updateDynamicBuffer(uniformSlot->uniforms.get(), 0, params.bytes.size(),
                                     params.bytes.constData());
    commandBuffer()->resourceUpdate(updates);
    for (const auto &effect : effects) {
        if (effect.plan.outline)
            pool->curve(
                curves[effect.fragment]->node.get(),
                QTransform::fromTranslate(input->fragments[effect.fragment].pathOrigin.x(),
                                          input->fragments[effect.fragment].pathOrigin.y()) *
                    effect.device,
                effect.plan, commandBuffer());
        for (const auto &op : effect.ops) {
            auto *cb = commandBuffer();
            cb->beginPass(pool->targets[op.dst].get(), Qt::transparent, {1, 0});
            cb->setGraphicsPipeline(op.kind == 3 ? finalPipelines[op.field.channel].get()
                                                 : pipelines[op.kind].get());
            const int x = op.kind == 3 ? op.field.pixels.x() - 1 : 0,
                      y = op.kind == 3 ? op.field.pixels.y() - 1 : 0;
            const auto extent = op.extent + (op.kind == 3 ? QSize(2, 2) : QSize(0, 0));
            cb->setViewport({float(x), float(1024 - y - extent.height()), float(extent.width()),
                             float(extent.height())});
            const QRhiCommandBuffer::DynamicOffset offset{0, params.offsets[op.slot]};
            cb->setShaderResources(binding(effect.fragment, op.src, op.kind == 2), 1, &offset);
            cb->draw(4);
            cb->endPass();
        }
    }
    pool->nodeCpuBytes[this] = memory->size() - allocatedBytes;
    pool->nodeEffectsBytes[this] = memory->size();
    const auto sharedBytes = pool->bytes();
    const bool changed = !status->ready.exchange(true) || status->batches != main.size() ||
                         status->gpuBytes != allocatedBytes || status->sharedBytes != sharedBytes;
    status->batches = main.size();
    status->gpuBytes = allocatedBytes;
    pool->nodeCpuBytes[this] = memory->size() - allocatedBytes;
    pool->nodeEffectsBytes[this] = memory->size();
    status->sharedBytes = pool->bytes();
    if (changed && status->changed)
        status->changed({});
}
} // namespace
NativeGlyphAtlas acquireAtlas(QQuickWindow *window, const QString &domain,
                              const std::shared_ptr<const Prepared> &prepared,
                              const QVariantList &metadata, QRhiResourceUpdateBatch *updates,
                              QString &error)
{
    NativeGlyphAtlas result;
    if (prepared->fragments.isEmpty())
        return result;
    auto pool = windowPool(window, domain);
    if (!pool->initInk(window)) {
        error = "Cannot initialize glyph ink atlas pool";
        return result;
    }
    // Pin immutable inks while UVs are public. Expired weak entries alone may be reclaimed.
    struct Retained
    {
        std::shared_ptr<Pool> pool;
        std::shared_ptr<const Prepared> input;
    };
    result.retained = std::make_shared<Retained>(Retained{pool, prepared});
    QHash<int, int> pages;
    for (int i = 0; i < prepared->fragments.size(); ++i) {
        auto fragment = metadata.value(i).toMap();
        QVariantList levels;
        for (int level = 0; level < 4; ++level) {
            InkLocation location;
            bool uploaded = false;
            if (!prepared->fragments[i].ink[level] ||
                !pool->ink(prepared->fragments[i].ink[level], location, updates, uploaded)) {
                error = "Glyph ink atlas exceeds the eight-page 32 MiB budget";
                return result;
            }
            result.uploads += uploaded;
            if (!pages.contains(location.page)) {
                pages[location.page] = int(result.textures.size());
                result.textures.push_back(pool->inkPages[location.page]->texture.get());
            }
            const auto rect = prepared->fragments[i].rect[level];
            levels.append(QVariantMap{
                {"page", pages.value(location.page)},
                {"uv",
                 QVariantList{location.pixels.x() / 1024., location.pixels.y() / 1024.,
                              location.pixels.width() / 1024., location.pixels.height() / 1024.}},
                {"rect", QVariantList{rect.x(), rect.y(), rect.width(), rect.height()}},
                {"color", prepared->fragments[i].color},
                {"coverage", prepared->fragments[i].color ? "a" : "r"}});
        }
        fragment["levels"] = levels;
        result.fragments.append(fragment);
    }
    return result;
}
QSGNode *updateNode(QSGNode *old, QQuickWindow *window,
                    const std::shared_ptr<const Prepared> &input, const QList<Draw> &draws,
                    const std::shared_ptr<Status> &status, bool screen, qreal maximumRasterScale,
                    const QString &domain)
{
    auto *node = dynamic_cast<CanvasNode *>(old);
    if (node && (node->input != input || node->screen != screen || node->domain != domain)) {
        delete node;
        node = nullptr;
        old = nullptr;
    }
    if (!node) {
        delete old;
        node = new CanvasNode(window);
    }
    node->domain = domain;
    node->input = input;
    node->draws = draws;
    node->status = status;
    node->screen = screen;
    node->maximumScale = maximumRasterScale;
    if (node->pool && node->pool->rhi && node->pool->rhi != window->rhi())
        node->releaseResources();
    if (!node->pool)
        node->pool = windowPool(window, domain);
    status->sharedKey = node->pool->key;
    return node;
}
} // namespace EchoMusic::GlyphCanvas
