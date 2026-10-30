#include "GpuRhi_p.h"
#include <QSGDynamicTexture>

namespace EchoMusic
{
void GpuRenderState::watchTextureSources(EchoGpuSurface &surface)
{
    const auto previous = std::exchange(textureProviders, {});
    // GUI object access is restricted to synchronize, while the GUI is blocked.
    for (auto it = snapshot.resources.cbegin(); it != snapshot.resources.cend(); ++it) {
        const auto &source = it->textureSource;
        if (!source || !source->active || !source->item ||
            source->item->window() != surface.window())
            continue;
        bool cycle = !gpuTextureSourceSafe(source->item);
        for (auto *parent = static_cast<QQuickItem *>(&surface); parent;
             parent = parent->parentItem())
            cycle |= parent == source->item;
        auto *provider = cycle ? nullptr : source->item->textureProvider();
        textureProviders.insert(it.key(), provider);
        if (previous.value(it.key()) == provider)
            continue;
        for (const auto &connection : textureSourceConnections.take(it.key()))
            QObject::disconnect(connection);
        if (!provider)
            continue;
        const auto wake = [sink = bridge, source] {
            if (source->active && sink->surface)
                sink->surface->requestFrame();
        };
        textureSourceConnections[it.key()] = {
            QObject::connect(provider, &QSGTextureProvider::textureChanged, bridge.get(), wake,
                             Qt::QueuedConnection),
            QObject::connect(provider, &QObject::destroyed, bridge.get(), wake,
                             Qt::QueuedConnection)};
    }
    for (const auto &id : textureSourceConnections.keys())
        if (!textureProviders.contains(id))
            for (const auto &connection : textureSourceConnections.take(id))
                QObject::disconnect(connection);
}
bool GpuRenderState::updateTextureSource(NativeGpuResource &native,
                                         const GpuResourceDescription &description,
                                         QSGTextureProvider *provider,
                                         QRhiResourceUpdateBatch *updates, bool &topology)
{
    const auto &source = description.textureSource;
    const auto reject = [&](const QString &reason) {
        source->ready = false;
        native.ready = false;
        error = reason;
        return false;
    };
    if (!source->active || !provider || !provider->texture())
        return reject("Texture source is not ready; use an explicit layer/texture provider");
    auto *texture = provider->texture();
    if (texture->isAtlasTexture())
        return reject("Atlas texture sources require an explicit full-item layer");
    // Qt's layer renders on this same frame/command buffer, before our sampling
    // pass. This is GPU-to-GPU; no grabToImage/readback or CPU upload is involved.
    if (auto *dynamic = qobject_cast<QSGDynamicTexture *>(texture))
        dynamic->updateTexture();
    texture->commitTextureOperations(rhi, updates);
    auto *handle = texture->rhiTexture();
    if (!handle || handle->rhi() != rhi)
        return reject("Texture source is unavailable on this RHI device");
    if (handle->pixelSize().isEmpty() || handle->pixelSize().width() > 4096 ||
        handle->pixelSize().height() > 4096 ||
        qint64(handle->pixelSize().width()) * handle->pixelSize().height() > 8ll * 1024 * 1024 ||
        handle->depth() > 1 || handle->arraySize() > 0 ||
        handle->flags().testFlag(QRhiTexture::CubeMap))
        return reject("Texture source exceeds the supported 2D sampling extent");
    if (!native.textureSource) {
        native.textureSource = source;
        ++source->readers;
    }
    if (!native.ready || native.texture.get() != handle ||
        source->handle != handle->globalResourceId() || source->extent != handle->pixelSize()) {
        topology = true;
        native.texture = std::shared_ptr<QRhiTexture>(handle, [](QRhiTexture *) {});
        if (source->handle != handle->globalResourceId() || source->extent != handle->pixelSize()) {
            source->handle = handle->globalResourceId();
            source->extent = handle->pixelSize();
            ++source->generation;
        }
    }
    source->ready = true;
    native.ready = true;
    native.kind = "texture";
    native.definition = description.definition;
    native.definition["width"] = handle->pixelSize().width();
    native.definition["height"] = handle->pixelSize().height();
    native.identity = gpuDeviceId(rhi) + ":source:" + QString::number(handle->globalResourceId());
    native.revision = description.revision;
    return true;
}
} // namespace EchoMusic
