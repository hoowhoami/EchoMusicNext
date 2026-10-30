#include "EchoGpuSurface.h"
#include "EchoPluginContext.h"
#include <QQmlContext>
#include <QQuickWindow>

namespace EchoMusic
{
bool gpuTextureSourceSafe(QQuickItem *source)
{
    QList<QQuickItem *> pending{source};
    int visited = 0;
    while (!pending.isEmpty()) {
        auto *item = pending.takeLast();
        if (++visited > 4096 || qobject_cast<EchoGpuSurface *>(item))
            return false;
        pending.append(item->childItems());
    }
    return true;
}
namespace
{
EchoPluginContext *sourceScope(QQuickItem *item)
{
    for (auto *parent = item; parent; parent = parent->parentItem()) {
        if (auto *scope =
                qobject_cast<EchoPluginContext *>(parent->property("plugin").value<QObject *>()))
            return scope;
    }
    if (auto *context = qmlContext(item))
        return qobject_cast<EchoPluginContext *>(
            context->contextProperty("plugin").value<QObject *>());
    return nullptr;
}
} // namespace
EchoGpuResource *EchoGpuSurface::importTextureSource(QQuickItem *source)
{
    if (!m_graphics)
        return nullptr;
    const auto reject = [this](const QString &reason) -> EchoGpuResource * {
        m_graphics->fail(reason);
        return nullptr;
    };
    auto *owner = source ? sourceScope(source) : nullptr;
    if (!m_graphics->active() || m_retired || !owner || !owner->active() ||
        owner->activationContext() != m_graphics->scope().activationContext() || !window() ||
        source->window() != window())
        return reject("Texture source must belong to the same active plugin and window");
    for (QQuickItem *parent = this; parent; parent = parent->parentItem())
        if (parent == source)
            return reject("A GPU surface cannot capture itself or its ancestor");
    if (!gpuTextureSourceSafe(source))
        return reject("Texture sources must have at most 4096 items and contain no GPU surfaces");
    for (const auto &existing : m_textureSources)
        if (existing && existing->active() && existing->m_description.textureSource &&
            existing->m_description.textureSource->item == source) {
            requestFrame();
            return existing;
        }
    QPointer<EchoGpuSurface> alive(this);
    QPointer<QQuickItem> sourceGuard(source);
    QPointer<EchoPluginContext> ownerGuard(owner);
    auto state = std::make_shared<GpuResourceDescription::TextureSource>();
    state->item = source;
    auto *resource = m_graphics->create("texture",
                                        {{"dimension", "2d"},
                                         {"width", 1},
                                         {"height", 1},
                                         {"format", "rgba8"},
                                         {"usage", QStringList{"sampled"}}},
                                        {}, nullptr, true, state);
    if (!resource)
        return nullptr;
    if (!alive || !sourceGuard || !ownerGuard || !ownerGuard->active() || !resource->active()) {
        resource->dispose();
        return nullptr;
    }
    m_textureSources.append(resource);
    connect(source, &QObject::destroyed, resource, &EchoGpuResource::dispose);
    connect(source, &QQuickItem::windowChanged, resource, &EchoGpuResource::dispose);
    connect(owner, &EchoPluginContext::sessionInvalidated, resource, &EchoGpuResource::dispose);
    connect(owner, &QObject::destroyed, resource, &EchoGpuResource::dispose);
    requestFrame();
    return resource;
}
void EchoGpuSurface::retireTextureSources()
{
    const auto sources = std::exchange(m_textureSources, {});
    for (auto resource : sources)
        if (resource)
            resource->dispose();
}
} // namespace EchoMusic
