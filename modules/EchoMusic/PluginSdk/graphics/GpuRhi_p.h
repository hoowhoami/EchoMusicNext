#pragma once
#include "EchoGpuSurface.h"
#include <rhi/qrhi.h>
#include <QElapsedTimer>
#include <QQuickWindow>
#include <QMatrix4x4>
#include <QMutex>
#include <map>
#include <vector>
namespace EchoMusic
{
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
struct NativeGpuResource
{
    std::unique_ptr<QRhiBuffer> buffer;
    std::unique_ptr<QRhiTexture> texture;
    std::unique_ptr<QRhiRenderBuffer> depth;
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
    QRhiReadbackResult result;
    QPointer<EchoPluginOperation> operation;
    bool complete = false;
};
struct GpuRenderState
{
    explicit GpuRenderState(std::shared_ptr<GpuBridge> sink) : bridge(std::move(sink)) {}
    ~GpuRenderState();
    void release();
    bool updateResources(QRhiResourceUpdateBatch *updates,
                         const QHash<QString, GpuResourceDescription> &descriptions);
    bool prepareOutput(QRhiRenderTarget *output);
    void composite(QRhiCommandBuffer *cb, QRhiRenderTarget *output);
    bool buildPasses(const QVariantMap &frame, QRhiRenderTarget *output);
    void readback(QRhiCommandBuffer *cb, QRhiTexture *surface,
                  const QList<QPair<QString, QPointer<EchoPluginOperation>>> &requests);
    EchoGpuSurface::Snapshot snapshot;
    std::shared_ptr<GpuBridge> bridge;
    std::shared_ptr<GpuMemoryBudget> budget;
    qint64 reservedBytes = 0;
    QRhi *rhi = nullptr;
    QPointer<QQuickWindow> window;
    std::atomic<int> objects = 0;
    std::atomic<bool> enabled = false;
    std::map<QString, NativeGpuResource> resources;
    std::vector<NativeGpuPass> passes;
    std::vector<std::shared_ptr<PendingGpuReadback>> reads;
    std::unique_ptr<QRhiBuffer> frameUniform;
    std::unique_ptr<QRhiTexture> workingTexture;
    std::unique_ptr<QRhiRenderBuffer> workingDepth;
    std::unique_ptr<QRhiRenderBuffer> workingMultisample;
    std::unique_ptr<QRhiTextureRenderTarget> workingTarget;
    std::unique_ptr<QRhiRenderPassDescriptor> workingDescriptor;
    std::unique_ptr<QRhiSampler> outputSampler;
    std::unique_ptr<QRhiShaderResourceBindings> outputBindings;
    std::unique_ptr<QRhiGraphicsPipeline> outputPipeline;
    std::unique_ptr<QRhiRenderPassDescriptor> outputDescriptor;
    bool reportHdr = false, workingNeedsClear = false;
    double reportHeadroom = 0;
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
    bool m_hdr = false;
};
} // namespace EchoMusic
