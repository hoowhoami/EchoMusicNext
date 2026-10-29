#include "GpuRhi_p.h"
#include <QColor>
#include <cstring>
namespace EchoMusic
{
GpuRenderer::GpuRenderer(std::shared_ptr<GpuBridge> bridge)
    : m_state(std::make_shared<GpuRenderState>(std::move(bridge))), m_snapshot(m_state->snapshot)
{
}
GpuRenderer::~GpuRenderer()
{
    m_state->release();
}
void GpuRenderer::initialize(QRhiCommandBuffer *)
{
    if (m_state->rhi != rhi()) {
        m_state->release();
        m_state->rhi = rhi();
    }
    if (!m_state->clock.isValid())
        m_state->clock.start();
}
void GpuRenderer::synchronize(QQuickRhiItem *item)
{
    auto *surface = static_cast<EchoGpuSurface *>(item);
    std::atomic_store(&surface->m_renderState, m_state);
    auto snapshot = surface->takeSnapshot();
    if (m_state->budget != snapshot.budget)
        m_state->release();
    m_state->budget = snapshot.budget;
    m_snapshot = std::move(snapshot);
    if (m_state->window != surface->window()) {
        m_state->window = surface->window();
        const auto weak = std::weak_ptr<GpuRenderState>(m_state);
        const auto release = [weak] {
            if (auto state = weak.lock()) {
                state->enabled = false;
                state->release();
            }
        };
        // NoStage jobs are dropped for unexposed windows. These render-thread
        // boundaries reclaim resources even when Qt retains its scene graph.
        QObject::connect(surface->window(), &QQuickWindow::sceneGraphAboutToStop,
                         m_state->bridge.get(), release, Qt::DirectConnection);
        QObject::connect(surface->window(), &QQuickWindow::sceneGraphInvalidated,
                         m_state->bridge.get(), release, Qt::DirectConnection);
    }
    m_state->enabled = m_snapshot.live;
}
namespace
{
QColor clearColor(const QVariantMap &pass)
{
    const auto v = pass.value("clear", QVariantList{0., 0., 0., 0.}).toList();
    if (v.size() != 4)
        return Qt::transparent;
    return QColor::fromRgbF(qBound(0., v[0].toDouble(), 64.), qBound(0., v[1].toDouble(), 64.),
                            qBound(0., v[2].toDouble(), 64.), qBound(0., v[3].toDouble(), 1.));
}
} // namespace
void GpuRenderer::render(QRhiCommandBuffer *cb)
{
    if (!m_state->enabled || !m_snapshot.live) {
        m_state->release();
        return;
    }
    auto &s = *m_state;
    if (rhi()->backend() == QRhi::Null || rhi()->isDeviceLost()) {
        s.release();
        s.bridge->report(
            {{"available", false},
             {"state", rhi()->backend() == QRhi::Null ? "unavailable" : "waiting"},
             {"compute", false},
             {"hdr", false},
             {"generation", QVariant::fromValue(s.generation)},
             {"reason", rhi()->backend() == QRhi::Null ? "Null backend has no GPU"
                                                       : "Device lost; waiting for Qt recovery"}});
        return;
    }
    // Qt Quick's embedded texture is not an independently color-managed HDR
    // presentation surface. Keep the parent swapchain and every ordinary item
    // untouched; HDR input is tone mapped inside this plugin's output quad.
    s.error.clear();
    if (!s.frameUniform) {
        s.generation = ++s.bridge->generation;
        s.objects = 1;
        s.frameUniform.reset(rhi()->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 96));
        if (!s.frameUniform->create())
            s.error = "Cannot allocate frame uniforms";
    }
    auto *updates = rhi()->nextResourceUpdateBatch();
    const bool resourcesReady = s.error.isEmpty() &&
                                s.updateResources(updates, m_snapshot.resources) &&
                                s.prepareOutput(renderTarget());
    if (resourcesReady) {
        float uniform[24] = {float(renderTarget()->pixelSize().width()),
                             float(renderTarget()->pixelSize().height()),
                             m_snapshot.pixelRatio,
                             float(s.clock.elapsed() / 1000.),
                             m_snapshot.hdr ? 0.f : -1.f,
                             1.f,
                             100.f,
                             rhi()->isYUpInFramebuffer() ? 1.f : 0.f};
        const auto correction = rhi()->clipSpaceCorrMatrix();
        std::memcpy(uniform + 8, correction.constData(), 64);
        updates->updateDynamicBuffer(s.frameUniform.get(), 0, sizeof(uniform), uniform);
    }
    cb->resourceUpdate(updates);
    const auto format = renderTarget()->renderPassDescriptor()->serializedFormat();
    if (resourcesReady &&
        (s.frameRevision != m_snapshot.frameRevision || s.outputFormat != format)) {
        if (s.buildPasses(m_snapshot.frame, s.workingTarget.get())) {
            s.frameRevision = m_snapshot.frameRevision;
            s.outputFormat = format;
        }
    }
    if (resourcesReady && s.workingNeedsClear) {
        cb->beginPass(s.workingTarget.get(), Qt::transparent, {1., 0});
        cb->endPass();
        s.workingNeedsClear = false;
    }
    bool outputDrawn = false;
    if (resourcesReady && s.error.isEmpty())
        for (auto &pass : s.passes) {
            if (!s.enabled)
                break;
            const auto &p = pass.definition;
            if (pass.compute) {
                cb->beginComputePass();
                cb->setComputePipeline(pass.compute.get());
                cb->setShaderResources(pass.bindings.get());
                const auto groups = p.value("groups").toList();
                cb->dispatch(groups[0].toInt(), groups[1].toInt(), groups[2].toInt());
                cb->endComputePass();
            } else {
                auto *target = pass.target ? pass.target.get() : s.workingTarget.get();
                cb->beginPass(target, clearColor(p), {1., 0});
                cb->setGraphicsPipeline(pass.graphics.get());
                cb->setShaderResources(pass.bindings.get());
                cb->setViewport({0, 0, float(target->pixelSize().width()),
                                 float(target->pixelSize().height())});
                const auto pipeline = s.resources.find(p.value("pipeline").toString());
                const auto layout = pipeline->second.definition.value("vertexLayout").toMap();
                for (const auto &draw : p.value("draws").toList()) {
                    const auto d = draw.toMap();
                    const auto vb = s.resources.find(d.value("vertexBuffer").toString());
                    const auto ib = s.resources.find(d.value("indexBuffer").toString());
                    QRhiCommandBuffer::VertexInput vertex;
                    int vertexCount = 0;
                    if (!layout.isEmpty()) {
                        if (vb == s.resources.end() || !vb->second.buffer ||
                            !vb->second.buffer->usage().testFlag(QRhiBuffer::VertexBuffer)) {
                            s.error = "Draw requires a live vertex buffer";
                            break;
                        }
                        const int offset = d.value("vertexOffset", 0).toInt();
                        const int first = d.value("first", 0).toInt();
                        if (offset < 0 || first < 0 ||
                            qint64(offset) + qint64(ib == s.resources.end()
                                                        ? first + d.value("count").toInt()
                                                        : 1) *
                                                 layout.value("stride").toInt() >
                                vb->second.buffer->size()) {
                            s.error = "Vertex draw range exceeds buffer";
                            break;
                        }
                        vertex = {vb->second.buffer.get(), quint32(offset)};
                        vertexCount = 1;
                    }
                    const int first = d.value("first", 0).toInt();
                    if (ib != s.resources.end()) {
                        const int stride = d.value("indexFormat", "uint16") == "uint32" ? 4 : 2;
                        if (!ib->second.buffer ||
                            !ib->second.buffer->usage().testFlag(QRhiBuffer::IndexBuffer) ||
                            first < 0 ||
                            qint64(first + d.value("count").toInt()) * stride >
                                ib->second.buffer->size()) {
                            s.error = "Index draw range exceeds buffer";
                            break;
                        }
                        cb->setVertexInput(0, vertexCount, vertexCount ? &vertex : nullptr,
                                           ib->second.buffer.get(), 0,
                                           stride == 4 ? QRhiCommandBuffer::IndexUInt32
                                                       : QRhiCommandBuffer::IndexUInt16);
                        cb->drawIndexed(d.value("count").toUInt(), d.value("instances", 1).toUInt(),
                                        quint32(first));
                    } else {
                        cb->setVertexInput(0, vertexCount, vertexCount ? &vertex : nullptr);
                        cb->draw(d.value("count").toUInt(), d.value("instances", 1).toUInt(),
                                 quint32(first));
                    }
                }
                cb->endPass();
                if (p.value("target", "surface") == "surface")
                    outputDrawn = true;
            }
        }
    if (outputDrawn && s.error.isEmpty())
        s.composite(cb, renderTarget());
    if (!outputDrawn || !s.error.isEmpty()) {
        cb->beginPass(renderTarget(), Qt::transparent, {1., 0});
        cb->endPass();
    }
    if (s.error.isEmpty())
        s.readback(cb, colorTexture() ? colorTexture() : resolveTexture(),
                   std::exchange(m_snapshot.readbacks, {}));
    else {
        for (const auto &request : m_snapshot.readbacks)
            s.bridge->complete(request.second, {}, s.error);
        m_snapshot.readbacks.clear();
    }
    if (!s.reads.empty())
        update();
    ++s.frames;
    // No GUI feedback loop: resource writes schedule frames; reports never do.
    if (s.frames == 1 || s.frames % 30 == 0 || s.reportHdrRequested != m_snapshot.hdr ||
        !s.error.isEmpty() || s.generation != s.reportGeneration) {
        s.reportGeneration = s.generation;
        s.reportHdrRequested = m_snapshot.hdr;
        s.bridge->report(
            {{"available", true},
             {"state", s.error.isEmpty() ? "ready" : "error"},
             {"backend", rhi()->backendName()},
             {"compute", rhi()->isFeatureSupported(QRhi::Compute)},
             {"floatTextures",
              rhi()->isTextureFormatSupported(QRhiTexture::RGBA16F, QRhiTexture::RenderTarget)},
             {"hdr", false},
             {"hdrRequested", m_snapshot.hdr},
             {"localHdrSupported", false},
             {"dynamicRange", "sdr"},
             {"headroom", 1.},
             {"sdrWhiteLevel", 100.},
             {"reason", s.error.isEmpty()
                            ? (m_snapshot.hdr ? "local-hdr-presentation-unavailable" : QString())
                            : s.error},
             {"nativeResources", s.objects.load()},
             {"generation", QVariant::fromValue(s.generation)},
             {"frames", QVariant::fromValue(s.frames)},
             {"pipelineBuilds", QVariant::fromValue(s.pipelineBuilds)},
             {"nativeBudgetUsed", s.budget ? s.budget->native.used.load() : 0},
             {"targetBudgetUsed", s.budget ? s.budget->targets.used.load() : 0}});
    }
}
} // namespace EchoMusic
