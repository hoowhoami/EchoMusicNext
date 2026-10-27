#include "WindowBackdrop.h"
#include "HyprlandWindowBackdrop.h"
#ifdef Q_OS_WIN
#include "WindowsWindowBackdrop.h"
#include <QOperatingSystemVersion>
#endif
#include <QGuiApplication>
#include <QWindow>
namespace EchoMusic
{
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
    if (native && HyprlandWindowBackdrop::available()) {
        m_hyprland = std::make_unique<HyprlandWindowBackdrop>();
        connect(m_hyprland.get(), &HyprlandWindowBackdrop::completed, this,
                &WindowBackdrop::reportError);
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
    if (m_hyprland)
        m_hyprland->attach(window);
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
    return m_native && m_window && m_hyprland &&
           (QGuiApplication::platformName().startsWith("wayland") ||
            QGuiApplication::platformName() == "xcb");
#else
    return false;
#endif
}
QString WindowBackdrop::apply(bool enabled, bool dark, int transparency)
{
#ifdef Q_OS_WIN
    if (m_windows) {
        m_windows->attach(m_window);
        // Clear/transparency remains usable without the optional Accent API.
        // The Windows adapter reports its own setup/material failures.
        return m_windows->apply(enabled, dark);
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
    if (m_hyprland)
        m_hyprland->apply(enabled);
    return m_error;
#endif
}
} // namespace EchoMusic
