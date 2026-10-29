#include "GpuRhi_p.h"
#include <QMetaObject>
#include <rhi/qshader.h>
#include <cstring>
#include <QSet>
namespace EchoMusic
{
namespace
{
struct DeferredReadbacks final : QObject
{
    std::vector<std::shared_ptr<PendingGpuReadback>> reads;
};
} // namespace
void GpuBridge::report(QVariantMap values)
{
    QMetaObject::invokeMethod(
        this,
        [this, values = std::move(values)] {
            if (surface)
                surface->acceptReport(values);
            if (graphics)
                emit graphics->drainedChanged();
        },
        Qt::QueuedConnection);
}
void GpuBridge::complete(QPointer<EchoPluginOperation> operation, QVariantMap value, QString error)
{
    QMetaObject::invokeMethod(
        this,
        [operation, value = std::move(value), error = std::move(error)] {
            if (!operation || operation->finished())
                return;
            if (error.isEmpty())
                operation->succeed(value);
            else
                operation->fail("GraphicsError", error);
        },
        Qt::QueuedConnection);
}
GpuRenderState::~GpuRenderState()
{
    release();
}
void GpuRenderState::release()
{
    if (!reads.empty() && rhi)
        rhi->finish();
    for (auto &read : reads)
        if (!read->complete)
            bridge->complete(read->operation, {}, "GPU readback canceled by resource retirement");
    std::erase_if(reads, [](const auto &read) { return read->complete; });
    if (!reads.empty() && rhi) {
        // A failed device may not complete readback. QRhi still holds pointers
        // to result records; keep them until its backend has been destroyed.
        rhi->addCleanupCallback([pending = std::move(reads)](QRhi *) mutable {
            auto *retained = new DeferredReadbacks;
            retained->reads = std::move(pending);
            retained->deleteLater();
        });
    }
    reads.clear();
    for (const auto &request : snapshot.readbacks)
        bridge->complete(request.second, {}, "GPU frame retired before readback");
    snapshot = {};
    passes.clear();
    resources.clear();
    outputPipeline.reset();
    outputDescriptor.reset();
    outputBindings.reset();
    outputSampler.reset();
    workingTarget.reset();
    workingDescriptor.reset();
    workingDepth.reset();
    workingMultisample.reset();
    workingTexture.reset();
    frameUniform.reset();
    if (budget && reservedBytes)
        budget->native.release(reservedBytes);
    reservedBytes = 0;
    objects = 0;
    outputFormat.clear();
    frameRevision = 0;
    bridge->report({{"state", "suspended"},
                    {"nativeResources", 0},
                    {"frames", QVariant::fromValue(frames)},
                    {"generation", QVariant::fromValue(generation)}});
}
bool GpuRenderState::updateResources(QRhiResourceUpdateBatch *updates,
                                     const QHash<QString, GpuResourceDescription> &descriptions)
{
    qint64 wanted = 0;
    for (const auto &d : descriptions)
        wanted += d.allocationBytes;
    QSet<QString> depthTargets;
    for (const auto &p : snapshot.frame.value("passes").toList()) {
        const auto pass = p.toMap();
        const auto target = pass.value("target", "surface").toString();
        const auto pipeline = descriptions.value(pass.value("pipeline").toString()).definition;
        if (target != "surface" &&
            (pipeline.value("depthTest").toBool() || pipeline.value("depthWrite").toBool()))
            depthTargets.insert(target);
    }
    // Depth textures are cached with their render target across command-graph changes.
    // Keep those allocations charged until the owning texture is disposed.
    for (const auto &[id, resource] : resources)
        if (resource.depth && descriptions.contains(id))
            depthTargets.insert(id);
    for (const auto &target : depthTargets) {
        const auto d = descriptions.value(target).definition;
        wanted += qint64(d.value("width").toInt()) * d.value("height").toInt() * 4;
    }
    if (!budget || (wanted > reservedBytes && !budget->native.reserve(wanted - reservedBytes))) {
        error = "Plugin native GPU allocation budget exceeded";
        return false;
    }
    if (wanted < reservedBytes)
        budget->native.release(reservedBytes - wanted);
    reservedBytes = wanted;
    bool topology = resources.size() != size_t(descriptions.size());
    if (!topology)
        for (const auto &[id, unused] : resources)
            if (!descriptions.contains(id)) {
                topology = true;
                break;
            }
    if (topology) {
        passes.clear();
        frameRevision = 0;
    }
    for (auto it = resources.begin(); it != resources.end();) {
        if (!descriptions.contains(it->first)) {
            it = resources.erase(it);
            topology = true;
        } else
            ++it;
    }
    for (auto it = descriptions.begin(); it != descriptions.end(); ++it) {
        const auto &d = it.value();
        auto [found, inserted] = resources.try_emplace(it.key());
        auto &native = found->second;
        if (inserted) {
            objects = int(resources.size()) + int(passes.size()) + bool(frameUniform);
            topology = true;
            native.kind = d.kind;
            native.definition = d.definition;
            if (d.kind == "buffer") {
                QRhiBuffer::UsageFlags usage;
                const auto list = d.definition.value("usage").toStringList();
                if (list.contains("uniform"))
                    usage |= QRhiBuffer::UniformBuffer;
                if (list.contains("vertex"))
                    usage |= QRhiBuffer::VertexBuffer;
                if (list.contains("index"))
                    usage |= QRhiBuffer::IndexBuffer;
                if (list.contains("storage")) {
                    if (!rhi->isFeatureSupported(QRhi::Compute)) {
                        error = "Storage buffers are unsupported by this device";
                        return false;
                    }
                    usage |= QRhiBuffer::StorageBuffer;
                }
                native.buffer.reset(rhi->newBuffer(list.contains("uniform") ? QRhiBuffer::Dynamic
                                                                            : QRhiBuffer::Static,
                                                   usage, d.definition.value("size").toInt()));
                if (!native.buffer->create()) {
                    error = "Cannot allocate GPU buffer";
                    return false;
                }
            } else if (d.kind == "texture") {
                const auto format = d.definition.value("format").toString();
                QRhiTexture::Format f = format == "rgba16f"   ? QRhiTexture::RGBA16F
                                        : format == "rgba32f" ? QRhiTexture::RGBA32F
                                        : format == "r32f"    ? QRhiTexture::R32F
                                                              : QRhiTexture::RGBA8;
                QRhiTexture::Flags flags = QRhiTexture::UsedAsTransferSource;
                const auto usage = d.definition.value("usage").toStringList();
                if (usage.contains("render-target"))
                    flags |= QRhiTexture::RenderTarget;
                if (usage.contains("storage"))
                    flags |= QRhiTexture::UsedWithLoadStore;
                if (!rhi->isTextureFormatSupported(f, flags)) {
                    error = "Texture format/usage is unsupported by this device";
                    return false;
                }
                native.texture.reset(rhi->newTexture(
                    f, {d.definition.value("width").toInt(), d.definition.value("height").toInt()},
                    1, flags));
                if (!native.texture->create()) {
                    error = "Cannot allocate GPU texture";
                    return false;
                }
            } else if (d.kind == "sampler") {
                const auto filter = [](const QVariant &v) {
                    return v.toString() == "nearest" ? QRhiSampler::Nearest : QRhiSampler::Linear;
                };
                const auto address = [](const QVariant &v) {
                    return v.toString() == "repeat"   ? QRhiSampler::Repeat
                           : v.toString() == "mirror" ? QRhiSampler::Mirror
                                                      : QRhiSampler::ClampToEdge;
                };
                native.sampler.reset(
                    rhi->newSampler(filter(d.definition.value("magFilter", "linear")),
                                    filter(d.definition.value("minFilter", "linear")),
                                    QRhiSampler::None, address(d.definition.value("u", "clamp")),
                                    address(d.definition.value("v", "clamp"))));
                if (!native.sampler->create()) {
                    error = "Cannot create GPU sampler";
                    return false;
                }
            }
            native.ready = true;
        }
        if (!native.ready) {
            error = "GPU allocation failed; recreate the resource";
            return false;
        }
        if (d.revision != native.revision) {
            if (native.buffer) {
                if (native.buffer->type() == QRhiBuffer::Dynamic)
                    updates->updateDynamicBuffer(native.buffer.get(), 0, d.data);
                else
                    updates->uploadStaticBuffer(native.buffer.get(), d.data);
            } else if (native.texture && !d.data.isEmpty()) {
                QRhiTextureSubresourceUploadDescription upload(d.data);
                updates->uploadTexture(
                    native.texture.get(),
                    QRhiTextureUploadDescription({QRhiTextureUploadEntry(0, 0, upload)}));
            }
            native.revision = d.revision;
        }
    }
    if (topology)
        frameRevision = 0;
    objects = int(resources.size()) + int(passes.size()) + bool(frameUniform);
    return true;
}
void GpuRenderState::readback(QRhiCommandBuffer *cb, QRhiTexture *surface,
                              const QList<QPair<QString, QPointer<EchoPluginOperation>>> &requests)
{
    std::erase_if(reads, [](const auto &read) { return read->complete; });
    if (requests.isEmpty())
        return;
    auto *updates = rhi->nextResourceUpdateBatch();
    for (const auto &[id, op] : requests) {
        auto texture = surface;
        QRhiBuffer *buffer = nullptr;
        if (!id.isEmpty()) {
            auto it = resources.find(id);
            if (it == resources.end()) {
                bridge->complete(op, {}, "GPU resource retired before readback");
                continue;
            }
            texture = it->second.texture.get();
            buffer = it->second.buffer.get();
        }
        if (buffer && (!rhi->isFeatureSupported(QRhi::ReadBackNonUniformBuffer) ||
                       buffer->type() == QRhiBuffer::Dynamic)) {
            bridge->complete(op, {}, "Buffer readback is unavailable for this resource/device");
            continue;
        }
        const qint64 size = buffer    ? buffer->size()
                            : texture ? qint64(texture->pixelSize().width()) *
                                            texture->pixelSize().height() *
                                            (texture->format() == QRhiTexture::RGBA32F   ? 16
                                             : texture->format() == QRhiTexture::RGBA16F ? 8
                                                                                         : 4)
                                      : 0;
        if (size <= 0 || size > 16 * 1024 * 1024 || reads.size() >= 8) {
            bridge->complete(op, {}, "Readback exceeds its 16 MiB/eight-request budget");
            continue;
        }
        if (!budget || !budget->readbacks.reserve(size * 2)) {
            bridge->complete(op, {}, "Plugin GPU readback budget exceeded");
            continue;
        }
        auto read = std::make_shared<PendingGpuReadback>();
        read->budget = budget;
        read->bytes = size * 2;
        read->operation = op;
        const std::weak_ptr<PendingGpuReadback> weak = read;
        read->result.completed = [weak, sink = bridge, isBuffer = bool(buffer),
                                  format = texture ? int(texture->format()) : 0] {
            const auto read = weak.lock();
            if (!read)
                return;
            read->complete = true;
            const auto f = format == QRhiTexture::RGBA16F   ? "rgba16f"
                           : format == QRhiTexture::RGBA32F ? "rgba32f"
                           : format == QRhiTexture::R32F    ? "r32f"
                                                            : "rgba8";
            sink->complete(read->operation, {{"data", read->result.data},
                                             {"width", read->result.pixelSize.width()},
                                             {"height", read->result.pixelSize.height()},
                                             {"format", isBuffer ? "buffer" : f}});
            read->result.data.clear();
        };
        reads.push_back(read);
        if (buffer)
            updates->readBackBuffer(buffer, 0, buffer->size(), &read->result);
        else
            updates->readBackTexture(QRhiReadbackDescription(texture), &read->result);
    }
    cb->resourceUpdate(updates);
}
} // namespace EchoMusic
