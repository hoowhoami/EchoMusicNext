#include "WindowBackdrop.h"
#include "WaylandBackgroundEffect.h"
#include "HyprlandWindowBackdrop.h"
#ifdef Q_OS_WIN
#include "WindowsWindowBackdrop.h"
#include <QOperatingSystemVersion>
#endif
#include <QGuiApplication>
#include <QLoggingCategory>
#include <QWindow>
namespace EchoMusic
{
Q_LOGGING_CATEGORY(windowBackdropLog, "echo.platform.window-backdrop")

#ifdef Q_OS_MACOS
QString applyMacBackdrop(QWindow *window, void *&effect, bool enabled, bool dark, int transparency);
void releaseMacBackdrop(void *&effect);
#endif
WindowBackdrop::WindowBackdrop(bool native) : m_native(native)
{
#ifdef Q_OS_WIN
    if (native) {
        m_windows = std::make_unique<WindowsWindowBackdrop>();
        connect(m_windows.get(), &WindowsWindowBackdrop::completed, this,
                &WindowBackdrop::reportError);
    }
#endif
#ifdef Q_OS_LINUX
    if (native) {
        m_wayland = std::make_unique<WaylandBackgroundEffect>();
        connect(m_wayland.get(), &WaylandBackgroundEffect::availableChanged, this,
                &WindowBackdrop::supportedChanged);
        if (HyprlandWindowBackdrop::available()) {
            m_hyprland = std::make_unique<HyprlandWindowBackdrop>();
            connect(m_hyprland.get(), &HyprlandWindowBackdrop::completed, this,
                    [this](const QString &error) {
                        if (m_hyprlandActive)
                            reportError(error);
                    });
        }
    }
#endif
}
void WindowBackdrop::reportError(const QString &error)
{
    m_error = error;
    emit completed(error);
}
WindowBackdrop::~WindowBackdrop()
{
#ifdef Q_OS_MACOS
    releaseMacBackdrop(m_effect);
#endif
}
Qt::WindowFlags WindowBackdrop::mainWindowFlags()
{
#ifdef Q_OS_WIN
    return WindowsWindowBackdrop::windowFlags(QOperatingSystemVersion::current().microVersion());
#elif defined(Q_OS_MACOS)
    return Qt::Window | Qt::ExpandedClientAreaHint | Qt::NoTitleBarBackgroundHint;
#else
    return Qt::Window | Qt::FramelessWindowHint;
#endif
}
void WindowBackdrop::attach(QWindow *window)
{
    if (window && m_window == window)
        return;
#ifdef Q_OS_MACOS
    // The retained effect belongs to the old native view. Detach it before
    // changing windows, including an explicit detach with nullptr.
    releaseMacBackdrop(m_effect);
#endif
    m_error.clear();
    m_window = window;
    m_hyprlandActive = false;
    if (m_hyprland)
        m_hyprland->attach(window);
    if (m_wayland && (!window || QGuiApplication::platformName().startsWith("wayland"))) {
        const auto attached = m_wayland->attach(window);
        qCInfo(windowBackdropLog) << "Wayland backdrop attach window=" << window
                                  << "result=" << attached;
    }
#ifdef Q_OS_WIN
    if (m_windows)
        m_windows->attach(window);
#endif
}
bool WindowBackdrop::supported() const
{
#ifdef Q_OS_MACOS
    return m_native && m_window && QGuiApplication::platformName() == "cocoa";
#elif defined(Q_OS_WIN)
    return m_native && m_windows && m_windows->supported();
#elif defined(Q_OS_LINUX)
    const auto platform = QGuiApplication::platformName();
    const auto result = m_native && m_window &&
                        ((platform.startsWith("wayland") &&
                          ((m_wayland && m_wayland->available()) || m_hyprland)) ||
                         (platform == "xcb" && m_hyprland));
    qCInfo(windowBackdropLog) << "Wayland backdrop supported=" << result << "native=" << m_native
                              << "window=" << m_window.data() << "adapter=" << m_wayland.get()
                              << "platform=" << QGuiApplication::platformName()
                              << "available=" << (m_wayland && m_wayland->available());
    return result;
#else
    return false;
#endif
}
QString WindowBackdrop::apply(bool enabled, bool dark, int transparency)
{
#ifdef Q_OS_LINUX
    const bool protocol = m_window && m_wayland &&
                          QGuiApplication::platformName().startsWith("wayland") &&
                          m_wayland->available();
    if (m_wayland)
        m_wayland->setEnabled(enabled && protocol);
#endif
#ifdef Q_OS_WIN
    if (m_windows) {
        m_windows->attach(m_window);
        // Clear/transparency remains usable without the optional Accent API.
        // The Windows adapter reports its own setup/material failures.
        return m_windows->apply(enabled, dark, transparency);
    }
#endif
    if (!supported()) {
#ifdef Q_OS_MACOS
        releaseMacBackdrop(m_effect);
#endif
        return enabled ? QObject::tr("当前平台的窗口毛玻璃尚未接入") : QString();
    }
#ifdef Q_OS_MACOS
    return applyMacBackdrop(m_window, m_effect, enabled, dark, transparency);
#elif defined(Q_OS_WIN)
    Q_UNUSED(transparency)
    return {};
#else
    Q_UNUSED(dark)
    Q_UNUSED(transparency)
#ifdef Q_OS_LINUX
    if (protocol) {
        m_error.clear();
        if (m_hyprlandActive) {
            m_hyprland->apply(true);
            m_hyprlandActive = false;
        }
        m_wayland->attach(m_window);
        return {};
    }
    if (m_hyprland) {
        m_hyprlandActive = true;
        m_hyprland->apply(enabled);
        return m_error;
    }
#endif
    return {};
#endif
}
void WindowBackdrop::prepareWindowTransition(bool maximizing)
{
#ifdef Q_OS_WIN
    if (m_windows)
        m_windows->prepareWindowTransition(maximizing);
#endif
}
} // namespace EchoMusic
