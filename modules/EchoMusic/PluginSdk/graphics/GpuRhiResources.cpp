#include "GpuRhi_p.h"
#include "GpuTextureLayout_p.h"
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
            if (values.isEmpty()) {
                if (graphics)
                    emit graphics->changed();
            } else if (surface)
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
    for (const auto &connections : textureSourceConnections)
        for (const auto &connection : connections)
            QObject::disconnect(connection);
    textureSourceConnections.clear();
    textureProviders.clear();
    if (!reads.empty() && rhi)
        rhi->finish();
    for (auto &read : reads) {
        if (!read->complete)
            bridge->complete(read->operation, {}, "GPU readback canceled by resource retirement");
        // Device-loss retention below owns only callback records, never QRhi objects
        // whose destructor could otherwise run after the RHI or on the GUI thread.
        if (read->gpuSnapshot && read->fenceReady && !read->bufferReadScheduled)
            read->complete = true;
        read->copyPass = {};
        read->gpuSnapshot.reset();
    }
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
    if (snapshot.submission)
        bridge->complete(snapshot.submission->operation, {},
                         "GPU submission retired before completion");
    snapshot = {};
    passes.clear();
    uploads.clear();
    resources.clear();
    copyPipeline.reset();
    copyLayout.reset();
    copySource.reset();
    copyDestination.reset();
    copyUniform.reset();
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
    glyphAtlases.clear();
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
    uploads.clear();
    qint64 wanted = 0;
    for (const auto &d : descriptions) {
        if (d.shared)
            continue;
        wanted += d.allocationBytes;
        if (d.kind == "buffer" && !d.definition.value("usage").toStringList().contains("uniform") &&
            rhi->isFeatureSupported(QRhi::Compute))
            wanted += d.allocationBytes;
    }
    for (const auto &value : snapshot.frame.value("passes").toList())
        wanted += value.toMap().value("data").toByteArray().size();
    QMap<QString, QSet<int>> depthTargets;
    for (const auto &p : snapshot.frame.value("passes").toList()) {
        const auto pass = p.toMap();
        const auto views = gpuColorTargets(pass);
        const auto pipeline = descriptions.value(pass.value("pipeline").toString()).definition;
        if (!views.isEmpty() &&
            (pipeline.value("depthTest").toBool() || pipeline.value("depthWrite").toBool())) {
            const auto view = views.first().toMap();
            depthTargets[view.value("texture").toString()].insert(gpuDepthKey(view));
        }
    }
    // Depth belongs to the first color texture/layer/mip, charged until disposal.
    for (const auto &[id, resource] : resources)
        if (descriptions.contains(id) && !descriptions.value(id).shared)
            for (const auto &[level, depth] : resource->depths)
                if (depth)
                    depthTargets[id].insert(level);
    for (auto it = depthTargets.cbegin(); it != depthTargets.cend(); ++it) {
        if (descriptions.value(it.key()).shared)
            continue;
        const auto d = descriptions.value(it.key()).definition;
        for (const int key : it.value()) {
            const int level = key % 16;
            wanted += qint64(qMax(1, d.value("width").toInt() >> level)) *
                      qMax(1, d.value("height").toInt() >> level) * 4;
        }
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
        if (d.shared && (!d.shared->active || (d.lease && !*d.lease))) {
            error = "Shared GPU resource or import lease has retired";
            return false;
        }
        if (inserted) {
            found->second = d.shared ? sharedAllocation(d) : std::make_shared<NativeGpuResource>();
            if (!found->second) {
                resources.erase(found);
                return false;
            }
        }
        auto &native = *found->second;
        if (d.textureSource) {
            if (!updateTextureSource(native, d, textureProviders.value(it.key()), updates,
                                     topology))
                return false;
            continue;
        }
        if (inserted && native.ready)
            topology = true;
        if (inserted && !native.ready) {
            objects = int(resources.size()) + int(passes.size()) + bool(frameUniform);
            topology = true;
            native.identity =
                d.shared ? gpuDeviceId(rhi) + ":" + QString::number(quintptr(d.shared.get()))
                         : it.key();
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
                if (list.contains("indirect")) {
                    if (!rhi->isFeatureSupported(QRhi::DrawIndirect)) {
                        error = "Indirect draws are unsupported by this device";
                        return false;
                    }
                    usage |= QRhiBuffer::IndirectBuffer;
                }
                if (list.contains("storage")) {
                    if (!rhi->isFeatureSupported(QRhi::Compute)) {
                        error = "Storage buffers are unsupported by this device";
                        return false;
                    }
                    usage |= QRhiBuffer::StorageBuffer;
                }
                // Non-uniform CPU writes use ordered GPU staging. Metal SSBOs
                // are not frame-slotted; writing their shared CPU memory would
                // race already-encoded dispatches, including later frames.
                if (!list.contains("uniform") && rhi->isFeatureSupported(QRhi::Compute))
                    usage |= QRhiBuffer::StorageBuffer;
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
                const bool array = d.definition.value("dimension") == "2d-array";
                const bool cube = d.definition.value("dimension") == "cube";
                const bool volume = d.definition.value("dimension") == "3d";
                if (volume && (!rhi->isFeatureSupported(QRhi::ThreeDimensionalTextures) ||
                               (d.definition.value("mipmaps").toBool() &&
                                !rhi->isFeatureSupported(QRhi::ThreeDimensionalTextureMipmaps)))) {
                    error = "3D textures or mipmaps are unsupported by this device";
                    return false;
                }
                if (array && (!rhi->isFeatureSupported(QRhi::TextureArrays) ||
                              d.definition.value("layers").toInt() >
                                  rhi->resourceLimit(QRhi::TextureArraySizeMax))) {
                    error = "Texture array size is unsupported by this device";
                    return false;
                }
                QRhiTexture::Flags flags = QRhiTexture::UsedAsTransferSource;
                if (array)
                    flags |= QRhiTexture::TextureArray;
                if (cube)
                    flags |= QRhiTexture::CubeMap;
                if (volume)
                    flags |= QRhiTexture::ThreeDimensional;
                if (d.definition.value("mipmaps").toBool())
                    flags |= QRhiTexture::MipMapped | QRhiTexture::UsedWithGenerateMips;
                const auto usage = d.definition.value("usage").toStringList();
                if (usage.contains("render-target"))
                    flags |= QRhiTexture::RenderTarget;
                if (usage.contains("storage"))
                    flags |= QRhiTexture::UsedWithLoadStore;
                if (!rhi->isTextureFormatSupported(f, flags)) {
                    error = "Texture format/usage is unsupported by this device";
                    return false;
                }
                const QSize extent(d.definition.value("width").toInt(),
                                   d.definition.value("height").toInt());
                if (volume)
                    native.texture.reset(rhi->newTexture(f, extent.width(), extent.height(),
                                                         d.definition.value("depth").toInt(), 1,
                                                         flags));
                else if (array)
                    native.texture.reset(rhi->newTextureArray(
                        f, d.definition.value("layers").toInt(), extent, 1, flags));
                else
                    native.texture.reset(rhi->newTexture(f, extent, 1, flags));
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
                                    (d.definition.value("mipFilter", "none") == "none"
                                         ? QRhiSampler::None
                                         : filter(d.definition.value("mipFilter"))),
                                    address(d.definition.value("u", "clamp")),
                                    address(d.definition.value("v", "clamp")),
                                    address(d.definition.value("w", "clamp"))));
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
        if (d.revision != native.revision && (!d.readOnly || d.revision > native.revision)) {
            if (native.buffer) {
                if (native.buffer->type() == QRhiBuffer::Dynamic)
                    updates->updateDynamicBuffer(native.buffer.get(), 0, d.data);
                else if (rhi->isFeatureSupported(QRhi::Compute)) {
                    NativeGpuPass upload;
                    upload.definition = {{"type", "upload-buffer"},
                                         {"destination", it.key()},
                                         {"offset", 0},
                                         {"data", d.data}};
                    if (!buildTransfer(upload))
                        return false;
                    uploads.push_back(std::move(upload));
                } else
                    updates->uploadStaticBuffer(native.buffer.get(), d.data);
            } else if (native.texture && !d.data.isEmpty()) {
                const int layers = gpuTextureSlices(d.definition);
                const int bytes = int(d.data.size()) / layers;
                QVector<QRhiTextureUploadEntry> entries;
                for (int layer = 0; layer < layers; ++layer)
                    entries.append(QRhiTextureUploadEntry(
                        layer, 0,
                        QRhiTextureSubresourceUploadDescription(d.data.mid(layer * bytes, bytes))));
                QRhiTextureUploadDescription upload;
                upload.setEntries(entries.cbegin(), entries.cend());
                updates->uploadTexture(native.texture.get(), upload);
            }
            native.revision = d.revision;
        }
    }
    if (topology) {
        passes.clear();
        frameRevision = 0;
    }
    objects = int(resources.size()) + int(passes.size()) + bool(frameUniform);
    return true;
}
void GpuRenderState::readback(
    QRhiCommandBuffer *cb, QRhiTexture *surface,
    const QList<QPair<QVariantMap, QPointer<EchoPluginOperation>>> &requests)
{
    std::erase_if(reads, [](const auto &read) { return read->complete; });
    if (requests.isEmpty())
        return;
    auto *updates = rhi->nextResourceUpdateBatch();
    for (const auto &[options, op] : requests) {
        const auto id = options.value("resource").toString();
        auto texture = surface;
        QRhiBuffer *buffer = nullptr;
        QVariantMap textureDefinition;
        if (!id.isEmpty()) {
            auto it = resources.find(id);
            if (it == resources.end()) {
                bridge->complete(op, {}, "GPU resource retired before readback");
                continue;
            }
            texture = it->second->texture.get();
            textureDefinition = it->second->definition;
            buffer = it->second->buffer.get();
        }
        if (buffer && (!rhi->isFeatureSupported(QRhi::ReadBackNonUniformBuffer) ||
                       buffer->type() == QRhiBuffer::Dynamic)) {
            bridge->complete(op, {}, "Buffer readback is unavailable for this resource/device");
            continue;
        }
        const int offset = options.value("offset", 0).toInt();
        const int level = options.value("level", 0).toInt();
        const int layer = options.value("layer", 0).toInt();
        if (texture && (layer < 0 || layer >= gpuTextureSlices(textureDefinition, level))) {
            bridge->complete(op, {}, "Texture readback face, layer or slice exceeds selected mip");
            continue;
        }
        if (texture && (level >= QRhi::mipLevelsForSize(texture->pixelSize()) ||
                        (level && (!texture->flags().testFlag(QRhiTexture::MipMapped) ||
                                   !rhi->isFeatureSupported(QRhi::ReadBackNonBaseMipLevel))))) {
            bridge->complete(op, {}, "Texture mip readback is unavailable");
            continue;
        }
        const auto extent = texture ? QRhi::sizeForMipLevel(level, texture->pixelSize()) : QSize();
        const QRect rect(options.value("x", 0).toInt(), options.value("y", 0).toInt(),
                         options.value("width", extent.width()).toInt(),
                         options.value("height", extent.height()).toInt());
        const qint64 size = buffer ? options.value("size", buffer->size() - offset).toLongLong()
                                   : qint64(rect.width()) * rect.height() *
                                         (texture->format() == QRhiTexture::RGBA32F   ? 16
                                          : texture->format() == QRhiTexture::RGBA16F ? 8
                                                                                      : 4);
        if ((buffer && (offset % 4 || size % 4 || qint64(offset) + size > buffer->size())) ||
            (texture && (!QRect(QPoint(), extent).contains(rect) || rect.isEmpty()))) {
            bridge->complete(op, {}, "GPU readback range exceeds resource");
            continue;
        }
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
                                  format = texture ? int(texture->format()) : 0, level, layer,
                                  offset, yUp = rhi->isYUpInFramebuffer()] {
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
                                             {"format", isBuffer ? "buffer" : f},
                                             {"level", level},
                                             {"layer", layer},
                                             {"offset", offset},
                                             {"yUp", yUp}});
            read->result.data.clear();
        };
        reads.push_back(read);
        if (buffer && rhi->isFeatureSupported(QRhi::Compute)) {
            // Readback uses an immutable GPU snapshot. Qt Metal's unslotted SSBO
            // readback can report a frame-slot completion before this dispatch;
            // start its CPU read only after a real texture fence has completed.
            read->gpuSnapshot.reset(
                rhi->newBuffer(QRhiBuffer::Static, QRhiBuffer::StorageBuffer, int(size)));
            read->copyPass.definition = {
                {"sourceOffset", offset}, {"destinationOffset", 0}, {"size", size}};
            if (!read->gpuSnapshot->create() ||
                !buildBufferCopy(read->copyPass, buffer, read->gpuSnapshot.get())) {
                read->complete = true;
                bridge->complete(op, {},
                                 error.isEmpty() ? "Cannot allocate GPU readback snapshot" : error);
                continue;
            }
            cb->resourceUpdate(updates);
            copyBuffer(cb, read->copyPass, rhi->nextResourceUpdateBatch());
            updates = rhi->nextResourceUpdateBatch();
            read->fence.completed = [weak] {
                if (auto read = weak.lock()) {
                    read->fenceReady = true;
                    read->fence.data.clear();
                }
            };
            QRhiReadbackDescription fence(surface);
            fence.setRect({0, 0, 1, 1});
            updates->readBackTexture(fence, &read->fence);
        } else if (buffer)
            updates->readBackBuffer(buffer, quint32(offset), quint32(size), &read->result);
        else {
            QRhiReadbackDescription description(texture);
            description.setLevel(level);
            description.setLayer(layer);
            description.setRect(rect);
            updates->readBackTexture(description, &read->result);
        }
    }
    cb->resourceUpdate(updates);
}
} // namespace EchoMusic
