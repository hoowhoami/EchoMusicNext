#include "GpuRhi_p.h"
#include <QThread>
#include <QUuid>
namespace EchoMusic
{
QString gpuDeviceId(QRhi *rhi)
{
    struct Registry
    {
        QMutex mutex;
        QHash<QRhi *, QString> ids;
    };
    static const auto registry = std::make_shared<Registry>();
    QMutexLocker lock(&registry->mutex);
    if (!registry->ids.contains(rhi)) {
        registry->ids.insert(rhi, QUuid::createUuid().toString(QUuid::WithoutBraces));
        rhi->addCleanupCallback([retained = registry](QRhi *device) {
            QMutexLocker cleanup(&retained->mutex);
            retained->ids.remove(device);
        });
    }
    return registry->ids.value(rhi);
}
NativeGpuResource::~NativeGpuResource()
{
    if (textureSource && --textureSource->readers == 0) {
        textureSource->ready = false;
        textureSource->handle = 0;
    }
    depths.clear();
    texture.reset();
    buffer.reset();
    sampler.reset();
    if (sharedBudget && charged)
        sharedBudget->native.release(charged);
}
bool NativeGpuResource::reserve(qint64 bytes)
{
    if (!sharedBudget)
        return true; // Private allocations are charged by their render state.
    if (!sharedBudget->native.reserve(bytes))
        return false;
    charged += bytes;
    return true;
}
std::shared_ptr<NativeGpuResource> GpuRenderState::sharedAllocation(const GpuResourceDescription &d)
{
    auto &state = *d.shared;
    QMutexLocker lock(&state.mutex);
    // Never lock a foreign thread's weak allocation: becoming its last strong
    // reference would run QRhi destructors on the wrong render thread.
    if (!state.native.expired() &&
        (state.device != rhi || state.thread != QThread::currentThread())) {
        error = "Shared GPU resource belongs to a different RHI device or render thread";
        return {};
    }
    if (auto allocation = state.native.lock())
        return allocation;
    auto allocation = std::make_shared<NativeGpuResource>();
    allocation->sharedBudget = budget;
    allocation->shared = d.shared;
    const bool staged = d.kind == "buffer" && rhi->isFeatureSupported(QRhi::Compute);
    if (!allocation->reserve(d.allocationBytes * (staged ? 2 : 1))) {
        error = "Shared native GPU allocation budget exceeded";
        return {};
    }
    state.device = rhi;
    state.thread = QThread::currentThread();
    state.native = allocation;
    ++state.generation;
    return allocation;
}
bool GpuRenderState::resourcesValid() const
{
    for (const auto &d : snapshot.resources)
        if ((d.shared && !d.shared->active) || (d.lease && !*d.lease) ||
            (d.textureSource &&
             (!d.textureSource->active || (d.textureSource->glyphAtlas &&
                                           (!d.textureSource->glyphAtlas->active ||
                                            !*d.textureSource->glyphAtlas->preparation->valid)))))
            return false;
    return true;
}
} // namespace EchoMusic
