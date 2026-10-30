#pragma once
#include "EchoGpuSurface.h"
#include "../GlyphAtlas_p.h"
#include <rhi/qrhi.h>
#include <QElapsedTimer>
#include <QQuickWindow>
#include <QMatrix4x4>
#include <QMutex>
#include <QSGTextureProvider>
#include <map>
#include <vector>
namespace EchoMusic
{
inline QVariantList gpuColorTargets(const QVariantMap &pass)
{
    if (pass.contains("colorTargets"))
        return pass.value("colorTargets").toList();
    const auto id = pass.value("target", "surface").toString();
    return id == "surface" ? QVariantList{}
                           : QVariantList{QVariantMap{{"texture", id}, {"level", 0}}};
}
// Mips use at most 13 bits of extent (4096). Keep each layer's depth history
// separate so loading one layer never reuses another layer's depth contents.
inline int gpuDepthKey(const QVariantMap &view)
{
    return view.value("layer", 0).toInt() * 16 + view.value("level", 0).toInt();
}
// Keeps queued reports alive independently of either QML item or scope. Weak
// QObject references are inspected only on the GUI thread inside those reports.
struct GpuBridge : QObject
{
    QPointer<EchoGpuSurface> surface;
    std::atomic<quint64> generation = 0;
    QPointer<EchoPluginGraphics> graphics;
    void report(QVariantMap values);
    void complete(QPointer<EchoPluginOperation> operation, QVariantMap value, QString error = {});
};
QString gpuDeviceId(QRhi *rhi);
struct NativeGpuResource
{
    ~NativeGpuResource();
    bool reserve(qint64 bytes);
    std::shared_ptr<GpuMemoryBudget> sharedBudget;
    std::shared_ptr<GpuSharedResourceState> shared;
    qint64 charged = 0;
    QString identity;
    std::unique_ptr<QRhiBuffer> buffer;
    // Borrowed scenegraph textures use a no-op deleter; Qt retains ownership.
    std::shared_ptr<QRhiTexture> texture;
    std::shared_ptr<GpuResourceDescription::TextureSource> textureSource;
    std::map<int, std::unique_ptr<QRhiTexture>> depths;
    std::unique_ptr<QRhiSampler> sampler;
    quint64 revision = 0;
    bool ready = false;
    QString kind;
    QVariantMap definition;
};
struct NativeGpuPass
{
    std::unique_ptr<QRhiShaderResourceBindings> bindings;
    std::unique_ptr<QRhiGraphicsPipeline> graphics;
    std::unique_ptr<QRhiComputePipeline> compute;
    std::unique_ptr<QRhiTextureRenderTarget> target;
    std::unique_ptr<QRhiRenderPassDescriptor> descriptor;
    std::unique_ptr<QRhiBuffer> transferUniform, stagingBuffer;
    QVariantMap definition;
};
struct PendingGpuReadback
{
    ~PendingGpuReadback()
    {
        if (budget && bytes)
            budget->readbacks.release(bytes);
    }
    std::shared_ptr<GpuMemoryBudget> budget;
    qint64 bytes = 0;
    std::shared_ptr<GpuSubmission> submission;
    QRhiReadbackResult result, fence;
    std::unique_ptr<QRhiBuffer> gpuSnapshot;
    NativeGpuPass copyPass;
    bool fenceReady = false, bufferReadScheduled = false;
    QPointer<EchoPluginOperation> operation;
    bool complete = false;
};
struct GpuRenderState
{
    explicit GpuRenderState(std::shared_ptr<GpuBridge> sink) : bridge(std::move(sink)) {}
    ~GpuRenderState();
    void release();
    bool resourcesValid() const;
    std::shared_ptr<NativeGpuResource> sharedAllocation(const GpuResourceDescription &d);
    bool updateResources(QRhiResourceUpdateBatch *updates,
                         const QHash<QString, GpuResourceDescription> &descriptions);
    bool updateTextureSource(NativeGpuResource &, const GpuResourceDescription &,
                             QSGTextureProvider *, QRhiResourceUpdateBatch *, bool &topology);
    void watchTextureSources(EchoGpuSurface &);
    bool updateGlyphAtlases(QRhiResourceUpdateBatch *);
    std::map<GpuGlyphAtlasState *, std::shared_ptr<GpuGlyphAtlasAllocation>> glyphAtlases;
    bool prepareOutput(QRhiRenderTarget *output);
    void composite(QRhiCommandBuffer *cb, QRhiRenderTarget *output);
    bool vertexLayout(const QVariantMap &layout, const QShader &shader,
                      QRhiVertexInputLayout &native);
    bool vertexInputs(const QVariantMap &draw, const QVariantMap &layout,
                      QVector<QRhiCommandBuffer::VertexInput> &inputs);
    bool buildPasses(const QVariantMap &frame, QRhiRenderTarget *output);
    bool buildTransfer(NativeGpuPass &pass);
    bool buildBufferCopy(NativeGpuPass &pass, QRhiBuffer *source, QRhiBuffer *destination);
    void copyBuffer(QRhiCommandBuffer *cb, NativeGpuPass &pass, QRhiResourceUpdateBatch *updates);
    void pumpReadbacks(QRhiCommandBuffer *cb);
    bool transfer(QRhiCommandBuffer *cb, NativeGpuPass &pass);
    void finishSubmission(QRhiCommandBuffer *cb, QRhiTexture *surface);
    void readback(QRhiCommandBuffer *cb, QRhiTexture *surface,
                  const QList<QPair<QVariantMap, QPointer<EchoPluginOperation>>> &requests);
    EchoGpuSurface::Snapshot snapshot;
    std::shared_ptr<GpuBridge> bridge;
    std::shared_ptr<GpuMemoryBudget> budget;
    qint64 reservedBytes = 0;
    QRhi *rhi = nullptr;
    QPointer<QQuickWindow> window;
    std::atomic<int> objects = 0;
    std::atomic<bool> enabled = false;
    std::map<QString, std::shared_ptr<NativeGpuResource>> resources;
    QHash<QString, QPointer<QSGTextureProvider>> textureProviders;
    QHash<QString, QList<QMetaObject::Connection>> textureSourceConnections;
    std::vector<NativeGpuPass> passes, uploads;
    std::vector<std::shared_ptr<PendingGpuReadback>> reads;
    std::unique_ptr<QRhiComputePipeline> copyPipeline;
    std::unique_ptr<QRhiShaderResourceBindings> copyLayout;
    std::unique_ptr<QRhiBuffer> copySource, copyDestination, copyUniform;
    quint64 copyPipelineBuilds = 0;
    std::unique_ptr<QRhiBuffer> frameUniform;
    std::unique_ptr<QRhiTexture> workingTexture;
    std::unique_ptr<QRhiTexture> workingDepth;
    std::unique_ptr<QRhiTexture> workingMultisample;
    std::unique_ptr<QRhiTextureRenderTarget> workingTarget;
    std::unique_ptr<QRhiRenderPassDescriptor> workingDescriptor;
    std::unique_ptr<QRhiSampler> outputSampler;
    std::unique_ptr<QRhiShaderResourceBindings> outputBindings;
    std::unique_ptr<QRhiGraphicsPipeline> outputPipeline;
    std::unique_ptr<QRhiRenderPassDescriptor> outputDescriptor;
    bool reportHdrRequested = false, workingNeedsClear = false, workingWithDepth = false;
    quint64 pipelineBuilds = 0;
    quint64 reportGeneration = 0;
    quint64 frameRevision = 0, generation = 0, frames = 0;
    QVector<quint32> outputFormat;
    QElapsedTimer clock;
    QString error;
};
class GpuRenderer final : public QQuickRhiItemRenderer
{
public:
    explicit GpuRenderer(std::shared_ptr<GpuBridge> bridge);
    ~GpuRenderer() override;
    void initialize(QRhiCommandBuffer *) override;
    void synchronize(QQuickRhiItem *item) override;
    void render(QRhiCommandBuffer *) override;

private:
    std::shared_ptr<GpuRenderState> m_state;
    EchoGpuSurface::Snapshot &m_snapshot;
};
} // namespace EchoMusic
