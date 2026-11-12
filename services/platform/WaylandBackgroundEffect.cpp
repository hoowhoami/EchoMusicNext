#include "WaylandBackgroundEffect.h"
#ifdef Q_OS_LINUX
#include "wayland-ext-background-effect-v1-client-protocol.h"
#include <QGuiApplication>
#include <QDebug>
#include <QLoggingCategory>
#include <QPlatformSurfaceEvent>
#include <QQuickWindow>
#include <QtGui/qguiapplication_platform.h>
#include <QtGui/qpa/qplatformwindow_p.h>
#include <wayland-client.h>

namespace EchoMusic
{
namespace
{
Q_LOGGING_CATEGORY(waylandBackdropLog, "echo.platform.wayland-backdrop")

void registryGlobal(void *data, wl_registry *registry, uint32_t name, const char *interface,
                    uint32_t version)
{
    static_cast<WaylandBackgroundEffect *>(data)->bindGlobal(registry, name, interface, version);
}

void registryRemove(void *, wl_registry *, uint32_t) {}

void capabilities(void *data, ext_background_effect_manager_v1 *, uint32_t flags)
{
    static_cast<WaylandBackgroundEffect *>(data)->setCapabilities(flags);
}

const ext_background_effect_manager_v1_listener managerListener{capabilities};
} // namespace

WaylandBackgroundEffect::WaylandBackgroundEffect(QObject *parent) : QObject(parent) {}

WaylandBackgroundEffect::~WaylandBackgroundEffect()
{
    detachSurface();
    if (m_manager)
        ext_background_effect_manager_v1_destroy(m_manager);
    if (m_registry)
        wl_registry_destroy(m_registry);
}

bool WaylandBackgroundEffect::available() const
{
    return m_available;
}

void WaylandBackgroundEffect::bindGlobal(wl_registry *registry, uint32_t name,
                                         const char *interface, uint32_t version)
{
    if (m_manager || qstrcmp(interface, ext_background_effect_manager_v1_interface.name) != 0)
        return;
    qCInfo(waylandBackdropLog) << "binding background effect manager, global=" << name
                               << "version=" << version;
    m_manager = static_cast<ext_background_effect_manager_v1 *>(wl_registry_bind(
        registry, name, &ext_background_effect_manager_v1_interface, qMin(version, 1u)));
    qCInfo(waylandBackdropLog) << "background effect manager=" << m_manager;
    if (m_manager)
        ext_background_effect_manager_v1_add_listener(m_manager, &managerListener, this);
}

void WaylandBackgroundEffect::setCapabilities(uint32_t flags)
{
    const bool available = flags & EXT_BACKGROUND_EFFECT_MANAGER_V1_CAPABILITY_BLUR;
    qCInfo(waylandBackdropLog) << "background effect capabilities=0x" << Qt::hex << flags
                               << "blur=" << available;
    if (m_available == available)
        return;
    m_available = available;
    if (m_effect)
        updateRegion();
    else
        attachSurface();
    emit availableChanged();
}

bool WaylandBackgroundEffect::bindProtocol()
{
    if (m_manager) {
        qCInfo(waylandBackdropLog) << "protocol already bound, available=" << m_available;
        return m_available;
    }
    auto *application =
        qGuiApp ? qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>() : nullptr;
    qCInfo(waylandBackdropLog) << "binding protocol, platform="
                               << (qGuiApp ? QGuiApplication::platformName() : QString())
                               << "applicationInterface=" << application;
    if (!application) {
        qCWarning(waylandBackdropLog) << "QWaylandApplication interface is null";
        return false;
    }
    m_display = application->display();
    m_compositor = application->compositor();
    qCInfo(waylandBackdropLog) << "display=" << m_display << "compositor=" << m_compositor;
    if (!m_display || !m_compositor) {
        qCWarning(waylandBackdropLog) << "Wayland display or compositor is null";
        return false;
    }
    m_registry = wl_display_get_registry(m_display);
    qCInfo(waylandBackdropLog) << "registry=" << m_registry;
    if (!m_registry) {
        qCWarning(waylandBackdropLog) << "wl_display_get_registry failed";
        return false;
    }
    static const wl_registry_listener listener{registryGlobal, registryRemove};
    wl_registry_add_listener(m_registry, &listener, this);
    if (wl_display_roundtrip(m_display) < 0) {
        qCWarning(waylandBackdropLog)
            << "registry roundtrip failed, displayError=" << wl_display_get_error(m_display);
        wl_registry_destroy(m_registry);
        m_registry = nullptr;
        return false;
    }
    if (!m_manager) {
        qCWarning(waylandBackdropLog)
            << "ext_background_effect_manager_v1 was not advertised by the compositor";
        wl_registry_destroy(m_registry);
        m_registry = nullptr;
        return false;
    }
    if (wl_display_roundtrip(m_display) < 0) {
        qCWarning(waylandBackdropLog)
            << "capability roundtrip failed, displayError=" << wl_display_get_error(m_display);
        return false;
    }
    qCInfo(waylandBackdropLog) << "protocol available=" << m_available;
    return m_available;
}

bool WaylandBackgroundEffect::attach(QWindow *window)
{
    qCInfo(waylandBackdropLog) << "attach window=" << window << "previous=" << m_window.data();
    if (m_window == window) {
        if (!window)
            return false;
        const auto result = bindProtocol() && m_available;
        attachSurface();
        qCInfo(waylandBackdropLog) << "attach same window result=" << result;
        return result;
    }
    if (m_window) {
        m_window->removeEventFilter(this);
        disconnect(m_window, nullptr, this, nullptr);
    }
    detachSurface();
    m_window = window;
    if (!window)
        return false;
    window->installEventFilter(this);
    connect(window, &QWindow::widthChanged, this, &WaylandBackgroundEffect::updateRegion,
            Qt::UniqueConnection);
    connect(window, &QWindow::heightChanged, this, &WaylandBackgroundEffect::updateRegion,
            Qt::UniqueConnection);
    bindProtocol();
    attachSurface();
    qCInfo(waylandBackdropLog) << "attach result available=" << m_available
                               << "surface=" << m_surface << "effect=" << m_effect;
    return m_available;
}

void WaylandBackgroundEffect::setEnabled(bool enabled)
{
    if (m_enabled == enabled)
        return;
    m_enabled = enabled;
    updateRegion();
}

bool WaylandBackgroundEffect::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_window && event->type() == QEvent::PlatformSurface) {
        const auto *surfaceEvent = static_cast<QPlatformSurfaceEvent *>(event);
        if (surfaceEvent->surfaceEventType() == QPlatformSurfaceEvent::SurfaceAboutToBeDestroyed)
            detachSurface();
        else if (surfaceEvent->surfaceEventType() == QPlatformSurfaceEvent::SurfaceCreated)
            attachSurface();
    }
    return false;
}

void WaylandBackgroundEffect::attachSurface()
{
    if (!m_window || m_effect) {
        qCInfo(waylandBackdropLog) << "attachSurface skipped window=" << m_window.data()
                                   << "available=" << m_available << "effect=" << m_effect;
        return;
    }
    if (!m_surface) {
        auto *native = m_window->nativeInterface<QNativeInterface::Private::QWaylandWindow>();
        qCInfo(waylandBackdropLog) << "window native interface=" << native;
        m_surface = native ? native->surface() : nullptr;
    }
    qCInfo(waylandBackdropLog) << "window surface=" << m_surface;
    if (!m_surface) {
        qCWarning(waylandBackdropLog) << "Wayland window surface is null";
        return;
    }
    if (!m_available || !m_manager)
        return;
    m_effect = ext_background_effect_manager_v1_get_background_effect(m_manager, m_surface);
    qCInfo(waylandBackdropLog) << "background effect surface=" << m_effect;
    updateRegion();
}

void WaylandBackgroundEffect::detachSurface()
{
    if (m_effect)
        ext_background_effect_surface_v1_destroy(m_effect);
    m_effect = nullptr;
    m_surface = nullptr;
}

void WaylandBackgroundEffect::updateRegion()
{
    if (!m_effect || !m_surface)
        return;
    wl_region *region = nullptr;
    if (m_enabled && m_available && m_window && m_window->width() > 0 && m_window->height() > 0) {
        region = wl_compositor_create_region(m_compositor);
        if (region)
            wl_region_add(region, 0, 0, m_window->width(), m_window->height());
    }
    ext_background_effect_surface_v1_set_blur_region(m_effect, region);
    if (region)
        wl_region_destroy(region);
    if (auto *quickWindow = qobject_cast<QQuickWindow *>(m_window.data()))
        quickWindow->requestUpdate();
}
} // namespace EchoMusic
#else
namespace EchoMusic
{
WaylandBackgroundEffect::WaylandBackgroundEffect(QObject *parent) : QObject(parent) {}
WaylandBackgroundEffect::~WaylandBackgroundEffect() = default;
bool WaylandBackgroundEffect::available() const
{
    return false;
}
bool WaylandBackgroundEffect::attach(QWindow *)
{
    return false;
}
void WaylandBackgroundEffect::setEnabled(bool) {}
void WaylandBackgroundEffect::bindGlobal(wl_registry *, uint32_t, const char *, uint32_t) {}
void WaylandBackgroundEffect::setCapabilities(uint32_t) {}
bool WaylandBackgroundEffect::eventFilter(QObject *, QEvent *)
{
    return false;
}
bool WaylandBackgroundEffect::bindProtocol()
{
    return false;
}
void WaylandBackgroundEffect::attachSurface() {}
void WaylandBackgroundEffect::detachSurface() {}
void WaylandBackgroundEffect::updateRegion() {}
} // namespace EchoMusic
#endif
