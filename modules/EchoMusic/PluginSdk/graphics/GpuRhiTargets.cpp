#include "GpuRhi_p.h"

namespace EchoMusic
{
QRhiRenderTarget *GpuRenderState::buildRenderTarget(NativeGpuPass &pass,
                                                    const QVariantMap &pipeline,
                                                    QRhiRenderTarget *output,
                                                    const QSet<QString> &readable,
                                                    const QSet<QString> &writable,
                                                    QSet<QString> &plannedClears, bool resolve)
{
    const auto &p = pass.definition;
    const auto views = gpuColorTargets(p);
    const bool explicitDepth = p.contains("depthTarget");
    const bool offscreen = !views.isEmpty() || explicitDepth;
    const bool load = p.value("loadOp", "clear") == "load";
    const int samples = p.value("samples", 1).toInt();
    if (!offscreen && !load)
        return output;
    if (offscreen && !rhi->supportedSampleCounts().contains(samples)) {
        error = "Requested offscreen sample count is unsupported";
        return nullptr;
    }
    QVector<QRhiColorAttachment> colors;
    QSet<NativeGpuResource *> attachments;
    QRhiTexture *depth = offscreen ? nullptr : workingDepth.get();
    QRhiTexture *depthResolve = nullptr;
    QSize extent;
    bool newDepth = false;
    const auto acquire = [&](const QString &id,
                             bool isDepth) -> std::shared_ptr<NativeGpuResource> {
        const auto found = resources.find(id);
        if (found == resources.end() || !found->second->texture ||
            readable.contains(found->second->identity) ||
            writable.contains(found->second->identity) ||
            attachments.contains(found->second.get()) ||
            (found->second->texture->format() == QRhiTexture::D32F) != isDepth) {
            error = "Attachment is missing, duplicated, has the wrong format or aliases a shader "
                    "binding";
            return {};
        }
        attachments.insert(found->second.get());
        return found->second;
    };
    const auto multisample = [&](const std::shared_ptr<NativeGpuResource> &resource,
                                 const QVariantMap &view, const QSize &size) -> QRhiTexture * {
        const int key = gpuDepthKey(view) * 16 + samples;
        const QString identity = resource->identity + '/' + QString::number(key);
        auto &texture = resource->multisamples[key];
        if (load && !resource->initializedSamples.contains(key) &&
            !plannedClears.contains(identity)) {
            error = "MSAA load requires a previously cleared multisample attachment";
            return nullptr;
        }
        if (!texture) {
            const auto format = resource->texture->format();
            const int bytes = format == QRhiTexture::RGBA32F   ? 16
                              : format == QRhiTexture::RGBA16F ? 8
                                                               : 4;
            if (!resource->reserve(qint64(size.width()) * size.height() * samples * bytes)) {
                error = "Multisample attachment exceeds native GPU budget";
                return nullptr;
            }
            texture.reset(rhi->newTexture(format, size, samples, QRhiTexture::RenderTarget));
            if (!texture->create()) {
                error = "Cannot allocate multisample attachment";
                return nullptr;
            }
        }
        if (!load)
            plannedClears.insert(identity);
        pass.initializedSamples.append({resource, key});
        return texture.get();
    };
    if (!offscreen) {
        QRhiColorAttachment color(workingMultisample ? workingMultisample.get()
                                                     : workingTexture.get());
        if (workingMultisample)
            color.setResolveTexture(workingTexture.get());
        colors.append(color);
    } else {
        for (const auto &value : views) {
            const auto view = value.toMap();
            const auto resource = acquire(view.value("texture").toString(), false);
            if (!resource)
                return nullptr;
            auto *texture = resource->texture.get();
            const int level = view.value("level", 0).toInt();
            if ((texture->flags().testFlag(QRhiTexture::ThreeDimensional) &&
                 !rhi->isFeatureSupported(QRhi::RenderTo3DTextureSlice)) ||
                (level && !rhi->isFeatureSupported(QRhi::RenderToNonBaseMipLevel))) {
                error = "Requested color mip or volume slice is unsupported";
                return nullptr;
            }
            const QSize size(qMax(1, texture->pixelSize().width() >> level),
                             qMax(1, texture->pixelSize().height() >> level));
            if (!extent.isEmpty() && size != extent) {
                error = "Color target sizes differ";
                return nullptr;
            }
            extent = size;
            QRhiColorAttachment color(texture);
            color.setLevel(level);
            color.setLayer(view.value("layer", 0).toInt());
            if (samples > 1) {
                auto *msaa = multisample(resource, view, size);
                if (!msaa)
                    return nullptr;
                color = QRhiColorAttachment(msaa);
                if (resolve) {
                    color.setResolveTexture(texture);
                    color.setResolveLevel(level);
                    color.setResolveLayer(view.value("layer", 0).toInt());
                    if (!load) {
                        const int key = gpuDepthKey(view) * 16 + samples;
                        pass.discardedSamples.append({resource, key});
                        plannedClears.remove(resource->identity + '/' + QString::number(key));
                    }
                }
            }
            colors.append(color);
        }
        if (explicitDepth) {
            const auto resource =
                acquire(p.value("depthTarget").toMap().value("texture").toString(), true);
            if (!resource)
                return nullptr;
            auto *texture = resource->texture.get();
            if (!extent.isEmpty() && extent != texture->pixelSize()) {
                error = "Depth and color target sizes differ";
                return nullptr;
            }
            extent = texture->pixelSize();
            depth = texture;
            if (samples > 1) {
                if (!rhi->isFeatureSupported(QRhi::ResolveDepthStencil)) {
                    error = "Depth multisample resolve is unsupported by this device";
                    return nullptr;
                }
                depth = multisample(resource, {}, extent);
                if (!depth)
                    return nullptr;
                if (resolve) {
                    depthResolve = texture;
                    pass.discardedSamples.append({resource, samples});
                    plannedClears.remove(resource->identity + '/' + QString::number(samples));
                }
            }
        } else if (!views.isEmpty() && (pipeline.value("depthTest").toBool() ||
                                        pipeline.value("depthWrite").toBool())) {
            const auto first = views.first().toMap();
            auto resource = resources.at(first.value("texture").toString());
            const int key = gpuDepthKey(first) * 16 + samples;
            auto &attachment = resource->depths[key];
            if (!attachment) {
                if (!resource->reserve(qint64(extent.width()) * extent.height() * 4 * samples)) {
                    error = "Shared depth allocation exceeds GPU budget";
                    return nullptr;
                }
                attachment.reset(
                    rhi->newTexture(QRhiTexture::D32F, extent, samples, QRhiTexture::RenderTarget));
                if (!attachment->create()) {
                    error = "Cannot create offscreen depth attachment";
                    return nullptr;
                }
                newDepth = true;
            }
            depth = attachment.get();
        }
    }
    QRhiTextureRenderTargetDescription description;
    description.setColorAttachments(colors.cbegin(), colors.cend());
    description.setDepthTexture(depth);
    description.setDepthResolveTexture(depthResolve);
    QRhiTextureRenderTarget::Flags flags;
    if (load) {
        flags |= QRhiTextureRenderTarget::PreserveColorContents;
        if (depth && !newDepth)
            flags |= QRhiTextureRenderTarget::PreserveDepthStencilContents;
    }
    pass.target.reset(rhi->newTextureRenderTarget(description, flags));
    pass.descriptor.reset(pass.target->newCompatibleRenderPassDescriptor());
    pass.target->setRenderPassDescriptor(pass.descriptor.get());
    if (!pass.target->create()) {
        error = "Cannot create offscreen render target";
        return nullptr;
    }
    return pass.target.get();
}
bool GpuRenderState::initializeTextures(QRhiCommandBuffer *cb)
{
    for (auto &[id, resource] : resources) {
        Q_UNUSED(id)
        if (resource->texture && resource->needsColorInitialization) {
            QRhiTextureRenderTargetDescription color{QRhiColorAttachment(resource->texture.get())};
            resource->initialColorTarget.reset(rhi->newTextureRenderTarget(color));
            resource->initialColorDescriptor.reset(
                resource->initialColorTarget->newCompatibleRenderPassDescriptor());
            resource->initialColorTarget->setRenderPassDescriptor(
                resource->initialColorDescriptor.get());
            if (!resource->initialColorTarget->create()) {
                error = "Cannot initialize color texture";
                return false;
            }
            cb->beginPass(resource->initialColorTarget.get(), Qt::transparent, {1., 0});
            cb->endPass();
            if (resource->texture->flags().testFlag(QRhiTexture::MipMapped)) {
                auto *updates = rhi->nextResourceUpdateBatch();
                updates->generateMips(resource->texture.get());
                cb->resourceUpdate(updates);
            }
            resource->needsColorInitialization = false;
        }
        if (!resource->texture || resource->texture->format() != QRhiTexture::D32F ||
            resource->depthInitialized)
            continue;
        QRhiTextureRenderTargetDescription description;
        description.setDepthTexture(resource->texture.get());
        resource->initialDepthTarget.reset(rhi->newTextureRenderTarget(description));
        resource->initialDepthDescriptor.reset(
            resource->initialDepthTarget->newCompatibleRenderPassDescriptor());
        resource->initialDepthTarget->setRenderPassDescriptor(
            resource->initialDepthDescriptor.get());
        if (!resource->initialDepthTarget->create()) {
            error = "Cannot initialize depth texture";
            return false;
        }
        cb->beginPass(resource->initialDepthTarget.get(), Qt::transparent, {1., 0});
        cb->endPass();
        resource->depthInitialized = true;
    }
    return true;
}
} // namespace EchoMusic
