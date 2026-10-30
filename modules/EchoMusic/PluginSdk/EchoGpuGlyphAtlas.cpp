#include "EchoGpuGlyphAtlas.h"
#include "EchoGpuSurface.h"
#include "GlyphRun.h"
#include "GlyphAtlas_p.h"
#include "EchoPluginContext.h"
#include <QQmlContext>
#include <QUuid>
#include <algorithm>
namespace EchoMusic
{
EchoGpuGlyphAtlas::EchoGpuGlyphAtlas(EchoGpuSurface &surface, GlyphRun &source)
    : QObject(&surface), m_surface(&surface), m_source(&source)
{
    connect(&source, &GlyphRun::renderingChanged, this, &EchoGpuGlyphAtlas::refresh);
    connect(&source, &GlyphRun::atlasChanged, this, &EchoGpuGlyphAtlas::refresh);
    connect(&source, &GlyphRun::layoutChanged, this, &EchoGpuGlyphAtlas::refresh);
    connect(&source, &GlyphRun::preparationChanged, this, &EchoGpuGlyphAtlas::refresh);
    connect(&source, &GlyphRun::errorChanged, this, &EchoGpuGlyphAtlas::refresh);
    connect(&source, &QObject::destroyed, this, &EchoGpuGlyphAtlas::dispose);
    connect(&source, &QQuickItem::windowChanged, this, &EchoGpuGlyphAtlas::dispose);
    connect(&surface, &QQuickItem::windowChanged, this, &EchoGpuGlyphAtlas::dispose);
}
EchoGpuGlyphAtlas::~EchoGpuGlyphAtlas()
{
    blockSignals(true);
    dispose();
}
bool EchoGpuGlyphAtlas::ready() const
{
    return m_active && m_state && m_state->ready && m_state->generation == m_generation &&
           m_state->acknowledged == m_generation && m_state->preparation->valid->load() &&
           std::all_of(m_pages.cbegin(), m_pages.cend(),
                       [](const auto &page) { return page && page->active(); });
}
quint64 EchoGpuGlyphAtlas::revision() const
{
    return m_state ? m_state->preparation->revision : 0;
}
qreal EchoGpuGlyphAtlas::pixelRatio() const
{
    return m_state ? m_state->preparation->pixelRatio : 1;
}
QVariantList EchoGpuGlyphAtlas::metrics() const
{
    return m_state ? m_state->preparation->metrics : QVariantList{};
}
QVariantList EchoGpuGlyphAtlas::pages() const
{
    QVariantList result;
    for (const auto &page : m_pages)
        result.append(QVariant::fromValue(page.data()));
    return result;
}
void EchoGpuGlyphAtlas::clear()
{
    if (m_state) {
        m_state->active = false;
        m_state->ready = false;
    }
    if (m_state)
        m_generation = qMax(m_generation, m_state->generation.load());
    m_state.reset();
    const auto pages = std::exchange(m_pages, {});
    m_fragments.clear();

    for (auto page : pages)
        if (page)
            page->dispose();
}
void EchoGpuGlyphAtlas::activityChanged()
{
    if (!m_active || !m_source || !m_surface)
        return;
    QPointer<EchoGpuGlyphAtlas> self(this);
    m_source->setAtlasConsumerActive(this, m_surface->live());
    if (self)
        refresh();
}
void EchoGpuGlyphAtlas::refresh()
{
    if (m_refreshing || !m_active || !m_source || !m_surface)
        return;
    m_refreshing = true;
    QPointer<EchoGpuGlyphAtlas> self(this);
    const auto newError = m_source->errorString();
    const bool errorChanged = m_error != newError;
    m_error = newError;
    const auto prepared =
        m_surface->live() && m_source->isVisible() ? m_source->atlasPreparation() : nullptr;
    if ((!prepared && !m_state) || (prepared && m_state && m_state->preparation == prepared)) {
        m_refreshing = false;
        if (errorChanged)
            emit changed();
        return;
    }
    clear();
    if (!self)
        return;
    m_error = m_source->errorString();
    if (prepared) {
        m_state = std::make_shared<GpuGlyphAtlasState>();
        m_state->generation = m_generation;
        m_state->owner = this;
        m_state->surface = m_surface;
        m_state->preparation = prepared;
    }
    m_surface->invalidateGlyphFrame();
    m_refreshing = false;
    emit changed();
}
void EchoGpuGlyphAtlas::dispose()
{
    if (!m_active)
        return;
    m_active = false;
    QPointer<EchoGpuGlyphAtlas> self(this);
    clear();
    if (!self)
        return;
    if (m_source) {
        disconnect(m_source, nullptr, this, nullptr);
        m_source->setAtlasConsumerActive(this, false);
    }
    if (!self)
        return;
    if (m_surface)
        m_surface->invalidateGlyphFrame();
    emit changed();
    if (self && !signalsBlocked())
        deleteLater();
}
void EchoGpuGlyphAtlas::publish(const std::shared_ptr<GpuGlyphAtlasState> &state,
                                quint64 generation, const QVariantList &pages,
                                const QVariantList &fragments, quint64 uploads,
                                const QString &error)
{
    if (!m_active || m_state != state || !state->active || generation != state->generation ||
        !m_surface)
        return;
    m_error = error;
    if (!error.isEmpty()) {
        emit changed();
        return;
    }
    QPointer<EchoGpuGlyphAtlas> self(this);
    const auto old = std::exchange(m_pages, {});
    for (auto page : old)
        if (page)
            page->dispose();
    if (!self || !m_surface || !m_active || m_state != state)
        return;
    QPointer<EchoPluginGraphics> graphics = m_surface->graphics();
    if (!graphics || !graphics->active())
        return;
    for (int i = 0; i < pages.size(); ++i) {
        auto source = std::make_shared<GpuResourceDescription::TextureSource>();
        source->ready = true;
        source->generation = generation;
        source->glyphAtlas = state;
        source->glyphPage = i;
        auto *resource = graphics->create("texture", pages[i].toMap(), {}, nullptr, true, source);
        if (!self || !resource || !resource->active() || m_state != state || !m_active) {
            if (resource)
                resource->dispose();
            if (self) {
                m_error =
                    graphics ? graphics->errorMessage() : QStringLiteral("Graphics scope retired");
                dispose();
            }
            return;
        }
        m_pages.append(resource);
        connect(resource, &EchoGpuResource::changed, this,
                [this, page = QPointer<EchoGpuResource>(resource)] {
                    // The lease owns the set. Explicit retirement of one public page retires it
                    // all; internally replaced pages were already removed from m_pages before
                    // disposal.
                    if (page && !page->active() && m_pages.contains(page))
                        dispose();
                });
    }
    m_fragments = fragments;
    m_generation = generation;
    m_uploads += uploads;
    m_surface->invalidateGlyphFrame();
    state->acknowledged = generation;
    emit changed();
}
void EchoGpuGlyphAtlas::nativeLost(const std::shared_ptr<GpuGlyphAtlasState> &state,
                                   quint64 generation)
{
    if (m_active && m_state == state && m_generation == generation && !state->ready) {
        QPointer<EchoGpuGlyphAtlas> self(this);
        if (m_source && m_surface)
            m_source->setAtlasConsumerActive(this, m_surface->live());
        if (!self)
            return;
        refresh();
        if (self)
            emit changed();
    }
}
EchoGpuGlyphAtlas *EchoGpuSurface::importGlyphAtlas(GlyphRun *source)
{
    if (!m_graphics)
        return nullptr;
    EchoPluginContext *owner = nullptr;
    if (source) {
        for (auto *item = static_cast<QQuickItem *>(source); item; item = item->parentItem()) {
            owner = qobject_cast<EchoPluginContext *>(item->property("plugin").value<QObject *>());
            if (owner)
                break;
        }
        if (!owner)
            if (auto *context = qmlContext(source))
                owner = qobject_cast<EchoPluginContext *>(
                    context->contextProperty("plugin").value<QObject *>());
    }
    if (m_retired || !m_graphics->active() || !source || !owner || !owner->active() ||
        owner->activationContext() != m_graphics->scope().activationContext() || !window() ||
        source->window() != window()) {
        m_graphics->fail("Glyph atlas must belong to the same active plugin and window");
        return nullptr;
    }
    m_glyphAtlases.removeIf([](const auto &lease) { return !lease || !lease->active(); });
    for (auto lease : m_glyphAtlases)
        if (lease && lease->active() && lease->m_source == source)
            return lease;
    auto *lease = new EchoGpuGlyphAtlas(*this, *source);
    m_glyphAtlases.append(lease);
    source->setAtlasDomain(m_graphics->atlasDomain());
    connect(owner, &EchoPluginContext::sessionInvalidated, source,
            &GlyphRun::retireAtlasPreparation, Qt::UniqueConnection);
    connect(owner, &EchoPluginContext::sessionInvalidated, lease, &EchoGpuGlyphAtlas::dispose);
    connect(owner, &QObject::destroyed, lease, &EchoGpuGlyphAtlas::dispose);
    QPointer<EchoGpuGlyphAtlas> alive(lease);
    lease->activityChanged();
    return alive.data();
}
void EchoGpuSurface::invalidateGlyphFrame()
{
    m_frame.clear();
    ++m_revision;
    requestFrame();
}
void EchoGpuSurface::retireGlyphAtlases()
{
    const auto leases = std::exchange(m_glyphAtlases, {});
    for (auto lease : leases)
        if (lease)
            lease->dispose();
}
} // namespace EchoMusic
