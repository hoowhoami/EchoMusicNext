#include "GpuRhi_p.h"
#include "GpuVertexLayout_p.h"
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
    m_state->watchTextureSources(*surface);
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
    if (m_snapshot.submission && m_snapshot.submission->canceled) {
        for (const auto &request : m_snapshot.readbacks)
            s.bridge->complete(request.second, {}, "GPU submission canceled");
        m_snapshot.readbacks.clear();
        m_snapshot.submission.reset();
        update();
        return;
    }
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
    s.pumpReadbacks(cb);
    auto *updates = rhi()->nextResourceUpdateBatch();
    if (!s.updateGlyphAtlases(updates)) {
        cb->resourceUpdate(updates);
        cb->beginPass(renderTarget(), Qt::transparent, {1., 0});
        cb->endPass();
        const auto reason = s.error.isEmpty()
                                ? QStringLiteral("Glyph atlas metadata generation is pending")
                                : s.error;
        for (const auto &request : m_snapshot.readbacks)
            s.bridge->complete(request.second, {}, reason);
        m_snapshot.readbacks.clear();
        if (m_snapshot.submission) {
            s.error = reason;
            s.finishSubmission(cb, colorTexture() ? colorTexture() : resolveTexture());
        }
        return; // A new UV generation cannot draw with the previous vertex/command snapshot.
    }
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
    if (resourcesReady)
        for (auto &upload : s.uploads)
            if (!s.transfer(cb, upload))
                break;
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
    const bool oneShot = bool(m_snapshot.submission);
    if (resourcesReady && s.error.isEmpty())
        for (auto &pass : s.passes) {
            if (!s.enabled || (m_snapshot.submission && m_snapshot.submission->canceled))
                break;
            if (!s.resourcesValid()) {
                s.error = "Shared GPU resource or lease retired";
                break;
            }
            const auto &p = pass.definition;
            const auto type = p.value("type", "render").toString();
            if (type != "render" && type != "compute") {
                if (!s.transfer(cb, pass))
                    break;
            } else if (pass.compute) {
                cb->beginComputePass();
                cb->setComputePipeline(pass.compute.get());
                cb->setShaderResources(pass.bindings.get());
                const auto groups = p.value("groups").toList();
                cb->dispatch(groups[0].toInt(), groups[1].toInt(), groups[2].toInt());
                cb->endComputePass();
            } else {
                auto *target = pass.target ? pass.target.get() : s.workingTarget.get();
                const auto viewport =
                    p.value("viewport", QVariantList{0, 0, target->pixelSize().width(),
                                                     target->pixelSize().height()})
                        .toList();
                const auto scissor =
                    p.value("scissor", QVariantList{0, 0, target->pixelSize().width(),
                                                    target->pixelSize().height()})
                        .toList();
                bool valid = true;
                for (const auto &rectangle : {viewport, scissor})
                    valid =
                        valid &&
                        rectangle[0].toInt() + rectangle[2].toInt() <=
                            target->pixelSize().width() &&
                        rectangle[1].toInt() + rectangle[3].toInt() <= target->pixelSize().height();
                if (!valid) {
                    s.error = "Viewport/scissor exceeds render target";
                    break;
                }
                cb->beginPass(target, clearColor(p), {1., 0});
                cb->setGraphicsPipeline(pass.graphics.get());
                cb->setShaderResources(pass.bindings.get());
                cb->setViewport({float(viewport[0].toInt()),
                                 float(target->pixelSize().height() - viewport[1].toInt() -
                                       viewport[3].toInt()),
                                 float(viewport[2].toInt()), float(viewport[3].toInt())});
                cb->setScissor(
                    {scissor[0].toInt(),
                     target->pixelSize().height() - scissor[1].toInt() - scissor[3].toInt(),
                     scissor[2].toInt(), scissor[3].toInt()});
                const auto pipeline = s.resources.find(p.value("pipeline").toString());
                const auto layout = pipeline->second->definition.value("vertexLayout").toMap();
                for (const auto &draw : p.value("draws").toList()) {
                    const auto d = draw.toMap();
                    const auto indirect = d.value("indirect").toMap();
                    const bool isIndirect = d.contains("indirect");
                    if (!isIndirect && d.value("instances", 1).toInt() > 1 &&
                        !rhi()->isFeatureSupported(QRhi::Instancing)) {
                        s.error = "Instancing unsupported by device";
                        break;
                    }
                    bool conflict = false;
                    for (const auto &value : p.value("bindings").toList()) {
                        const auto b = value.toMap();
                        const auto id = b.value("resource").toString();
                        if (b.value("access", "read") != "read" &&
                            (s.resources.contains(id) &&
                             ((s.resources.contains(d.value("vertexBuffer").toString()) &&
                               s.resources.at(id) ==
                                   s.resources.at(d.value("vertexBuffer").toString())) ||
                              (s.resources.contains(d.value("indexBuffer").toString()) &&
                               s.resources.at(id) ==
                                   s.resources.at(d.value("indexBuffer").toString())) ||
                              (s.resources.contains(indirect.value("buffer").toString()) &&
                               s.resources.at(id) ==
                                   s.resources.at(indirect.value("buffer").toString())))))
                            conflict = true;
                    }
                    if (conflict) {
                        s.error = "Draw buffers cannot be shader-writable in the same render pass";
                        break;
                    }
                    const auto ib = s.resources.find(d.value("indexBuffer").toString());
                    QVector<QRhiCommandBuffer::VertexInput> vertices;
                    if (!s.vertexInputs(d, layout, vertices))
                        break;
                    bool aliasWrite = false;
                    for (const auto &stream : gpuDrawStreams(d))
                        for (const auto &value : p.value("bindings").toList()) {
                            const auto binding = value.toMap();
                            if (binding.value("access", "read") != "read" &&
                                s.resources.contains(binding.value("resource").toString()) &&
                                s.resources.at(binding.value("resource").toString()) ==
                                    s.resources.at(stream.toMap().value("buffer").toString()))
                                aliasWrite = true;
                        }
                    if (aliasWrite) {
                        s.error = "Vertex streams cannot be shader-writable in the same pass";
                        break;
                    }
                    const int vertexCount = int(vertices.size());
                    const int first = d.value("first", 0).toInt();
                    if ((d.value("firstInstance", 0).toInt() &&
                         !rhi()->isFeatureSupported(QRhi::BaseInstance)) ||
                        (d.value("baseVertex", 0).toInt() &&
                         !rhi()->isFeatureSupported(QRhi::BaseVertex))) {
                        s.error = "Base vertex/instance is unsupported by this device";
                        break;
                    }
                    auto args = s.resources.end();
                    if (isIndirect) {
                        args = s.resources.find(indirect.value("buffer").toString());
                        if (args == s.resources.end() || !args->second->buffer ||
                            !args->second->buffer->usage().testFlag(QRhiBuffer::IndirectBuffer)) {
                            s.error = "Indirect draw buffer has retired";
                            break;
                        }
                    }
                    if (ib != s.resources.end()) {
                        const int stride = d.value("indexFormat", "uint16") == "uint32" ? 4 : 2;
                        if (!ib->second->buffer ||
                            !ib->second->buffer->usage().testFlag(QRhiBuffer::IndexBuffer) ||
                            first < 0 ||
                            qint64(isIndirect ? 1 : first + d.value("count").toInt()) * stride >
                                ib->second->buffer->size()) {
                            s.error = "Index draw range exceeds buffer";
                            break;
                        }
                        cb->setVertexInput(0, vertexCount,
                                           vertexCount ? vertices.constData() : nullptr,
                                           ib->second->buffer.get(), 0,
                                           stride == 4 ? QRhiCommandBuffer::IndexUInt32
                                                       : QRhiCommandBuffer::IndexUInt16);
                        if (!isIndirect)
                            cb->drawIndexed(d.value("count").toUInt(),
                                            d.value("instances", 1).toUInt(), quint32(first),
                                            d.value("baseVertex", 0).toInt(),
                                            d.value("firstInstance", 0).toUInt());
                    } else {
                        cb->setVertexInput(0, vertexCount,
                                           vertexCount ? vertices.constData() : nullptr);
                        if (!isIndirect)
                            cb->draw(d.value("count").toUInt(), d.value("instances", 1).toUInt(),
                                     quint32(first), d.value("firstInstance", 0).toUInt());
                    }
                    if (isIndirect) {
                        const bool indexed = ib != s.resources.end();
                        const auto offset = indirect.value("offset", 0).toUInt();
                        const auto count = indirect.value("drawCount", 1).toUInt();
                        const auto stride = indirect.value("stride", indexed ? 20 : 16).toUInt();
                        // A backend without native multi-draw still executes every GPU argument
                        // record directly. Never read arguments back to emulate this on the CPU.
                        const bool multi = rhi()->isFeatureSupported(QRhi::DrawIndirectMulti);
                        for (quint32 i = 0; i < count; i += multi ? count : 1) {
                            if (indexed)
                                cb->drawIndexedIndirect(args->second->buffer.get(),
                                                        offset + i * stride, multi ? count : 1,
                                                        stride);
                            else
                                cb->drawIndirect(args->second->buffer.get(), offset + i * stride,
                                                 multi ? count : 1, stride);
                        }
                    }
                }
                cb->endPass();
            }
        }
    if (resourcesReady && s.error.isEmpty())
        s.composite(cb, renderTarget());
    if (!resourcesReady || !s.error.isEmpty()) {
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
    s.finishSubmission(cb, colorTexture() ? colorTexture() : resolveTexture());
    if (!s.reads.empty() || oneShot)
        update();
    ++s.frames;
    // No GUI feedback loop: resource writes schedule frames; reports never do.
    if (oneShot || s.frames == 1 || s.frames % 30 == 0 || s.reportHdrRequested != m_snapshot.hdr ||
        !s.error.isEmpty() || s.generation != s.reportGeneration) {
        s.reportGeneration = s.generation;
        s.reportHdrRequested = m_snapshot.hdr;
        s.bridge->report(
            {{"available", true},
             {"state", s.error.isEmpty() ? "ready" : "error"},
             {"backend", rhi()->backendName()},
             {"deviceId", gpuDeviceId(rhi())},
             {"compute", rhi()->isFeatureSupported(QRhi::Compute)},
             {"baseVertex", rhi()->isFeatureSupported(QRhi::BaseVertex)},
             {"instancing", rhi()->isFeatureSupported(QRhi::Instancing)},
             {"customInstanceStepRate", rhi()->isFeatureSupported(QRhi::CustomInstanceStepRate)},
             {"integerAttributes", rhi()->isFeatureSupported(QRhi::IntAttributes)},
             {"halfAttributes", rhi()->isFeatureSupported(QRhi::HalfAttributes)},
             {"baseInstance", rhi()->isFeatureSupported(QRhi::BaseInstance)},
             {"indirectDraw", rhi()->isFeatureSupported(QRhi::DrawIndirect)},
             {"nativeMultiDrawIndirect", rhi()->isFeatureSupported(QRhi::DrawIndirectMulti)},
             {"readbackMipmaps", rhi()->isFeatureSupported(QRhi::ReadBackNonBaseMipLevel)},
             {"uniformBufferAlignment", rhi()->ubufAlignment()},
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
             {"copyPipelineBuilds", QVariant::fromValue(s.copyPipelineBuilds)},
             {"multipleRenderTargets", rhi()->resourceLimit(QRhi::MaxColorAttachments) > 1},
             {"independentBlend", rhi()->isFeatureSupported(QRhi::PerRenderTargetBlending)},
             {"renderMipmaps", rhi()->isFeatureSupported(QRhi::RenderToNonBaseMipLevel)},
             {"textureArrays", rhi()->isFeatureSupported(QRhi::TextureArrays)},
             {"cubeTextures",
              rhi()->isTextureFormatSupported(QRhiTexture::RGBA8, QRhiTexture::CubeMap)},
             {"volumeTextures", rhi()->isFeatureSupported(QRhi::ThreeDimensionalTextures)},
             {"volumeMipmaps", rhi()->isFeatureSupported(QRhi::ThreeDimensionalTextureMipmaps)},
             {"renderVolumeSlices", rhi()->isFeatureSupported(QRhi::RenderTo3DTextureSlice)},
             {"deviceLimits",
              QVariantMap{
                  {"textureExtent", rhi()->resourceLimit(QRhi::TextureSizeMax)},
                  {"uniformBindingBytes", rhi()->resourceLimit(QRhi::MaxUniformBufferRange)},
                  {"vertexInputs", rhi()->resourceLimit(QRhi::MaxVertexInputs)},
                  {"colorAttachments", rhi()->resourceLimit(QRhi::MaxColorAttachments)},
                  {"textureArrayLayers", rhi()->resourceLimit(QRhi::TextureArraySizeMax)}}},
             {"nativeBudgetUsed", s.budget ? s.budget->native.used.load() : 0},
             {"targetBudgetUsed", s.budget ? s.budget->targets.used.load() : 0}});
    }
}
} // namespace EchoMusic
