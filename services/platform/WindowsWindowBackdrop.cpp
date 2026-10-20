#include "WindowsWindowBackdrop.h"
#include <QCoreApplication>
#include <QOperatingSystemVersion>
#include <QWindow>
#include <dwmapi.h>
namespace EchoMusic
{
namespace
{
struct AccentPolicy
{
    int state;
    DWORD flags;
    DWORD color;
    DWORD animation;
};
struct CompositionAttribute
{
    int attribute;
    void *data;
    SIZE_T size;
};
using SetComposition = BOOL(WINAPI *)(HWND, CompositionAttribute *);
SetComposition compositionApi()
{
    // Windows 10 Acrylic is undocumented. Resolve it; do not assume availability.
    const auto module = GetModuleHandleW(L"user32.dll");
    return module ? reinterpret_cast<SetComposition>(
                        GetProcAddress(module, "SetWindowCompositionAttribute"))
                  : nullptr;
}
bool alphaBehind(HWND handle, bool enabled)
{
    DWM_BLURBEHIND options{};
    options.dwFlags = enabled ? DWM_BB_ENABLE | DWM_BB_BLURREGION : DWM_BB_ENABLE;
    options.fEnable = enabled;
    options.hRgnBlur = enabled ? CreateRectRgn(0, 0, -1, -1) : nullptr;
    if (enabled && !options.hRgnBlur)
        return false;
    const bool ok = SUCCEEDED(DwmEnableBlurBehindWindow(handle, &options));
    if (options.hRgnBlur)
        DeleteObject(options.hRgnBlur);
    return ok;
}
bool systemMaterial(HWND handle, int material)
{
    return SUCCEEDED(DwmSetWindowAttribute(handle, 38, &material, sizeof(material)));
}
bool extendFrame(HWND handle, bool enabled)
{
    // DWM only paints the system backdrop where the frame accepts it. A
    // frameless window has no non-client area, so the whole client area must
    // opt in while the material is active.
    const MARGINS margins = enabled ? MARGINS{-1, -1, -1, -1} : MARGINS{0, 0, 0, 0};
    return SUCCEEDED(DwmExtendFrameIntoClientArea(handle, &margins));
}
void refreshFrame(HWND handle)
{
    // DWM can defer a newly requested backdrop on an already visible frameless
    // window until the non-client area is recalculated.
    SetWindowPos(handle, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
}
} // namespace
WindowsWindowBackdrop::WindowsWindowBackdrop()
{
    m_build = QOperatingSystemVersion::current().microVersion();
    if (qApp)
        qApp->installNativeEventFilter(this);
}
WindowsWindowBackdrop::~WindowsWindowBackdrop()
{
    if (qApp)
        qApp->removeNativeEventFilter(this);
    clear();
}
void WindowsWindowBackdrop::attach(QWindow *window)
{
    const auto handle = window ? reinterpret_cast<HWND>(window->winId()) : nullptr;
    if (window && m_window == window && m_handle == handle)
        return;
    clear();
    disconnect(m_visibilityConnection);
    m_window = window;
    m_handle = handle;
    if (window)
        m_visibilityConnection =
            connect(window, &QWindow::visibilityChanged, this, [this] { queueRepair(); });
    applyCorners();
}
void WindowsWindowBackdrop::applyCorners()
{
    if (m_build < 22000 || !m_handle || !IsWindow(m_handle))
        return;
    // Frameless Qt windows do not qualify for DWM's automatic heuristics.
    // This documented opt-in is independent of the user's Acrylic preference;
    // DWM still owns square maximized/snapped/full-screen corners.
    const DWM_WINDOW_CORNER_PREFERENCE preference = DWMWCP_ROUND;
    const auto result = DwmSetWindowAttribute(m_handle, DWMWA_WINDOW_CORNER_PREFERENCE, &preference,
                                              sizeof(preference));
    if (FAILED(result))
        qWarning("DWM rounded-corner request failed: HRESULT=0x%08lx", DWORD(result));
}
bool WindowsWindowBackdrop::supported() const
{
    BOOL compositing = FALSE;
    return m_window && m_handle && IsWindow(m_handle) &&
           SUCCEEDED(DwmIsCompositionEnabled(&compositing)) && compositing &&
           (m_build >= 22621 || compositionApi());
}
bool WindowsWindowBackdrop::accent(int state)
{
    const auto api = compositionApi();
    if (!api)
        return state == 0;
    // Use the default non-persistent Accent Acrylic tint while preserving Qt alpha composition.
    AccentPolicy policy{state, 0, state == 4 ? DWORD(0x01000000) : DWORD(0), 0};
    CompositionAttribute data{19, &policy, sizeof(policy)};
    return api(m_handle, &data);
}
void WindowsWindowBackdrop::clear()
{
    if (m_handle && IsWindow(m_handle) && m_enabled) {
        if (m_build >= 22621)
            material(false);
        else
            accent(0);
    }
    m_enabled = m_suspended = false;
}
QString WindowsWindowBackdrop::apply(bool enabled, bool dark)
{
    if (!supported())
        return enabled ? QObject::tr("系统背景接口不可用，窗口毛玻璃未能应用") : QString();
    if (enabled == m_enabled && dark == m_dark)
        return m_error;
    const BOOL darkMode = dark;
    // DWM's built-in tint follows the resolved light/dark variant.
    DwmSetWindowAttribute(m_handle, 20, &darkMode, sizeof(darkMode));
    const bool ok = m_build >= 22621 ? material(enabled) : accent(enabled && !m_suspended ? 4 : 0);
    m_dark = dark;
    if (!ok) {
        if (m_build >= 22621)
            material(false);
        else
            accent(0);
        clear();
        m_error = tr("系统背景接口不可用，窗口毛玻璃未能应用");
        emit completed(m_error);
        return m_error;
    }
    m_enabled = enabled;
    m_error.clear();
    emit completed({});
    return {};
}
bool WindowsWindowBackdrop::material(bool enabled)
{
    // Qt6.12 qwindowswindow.cpp enables DWM_BB for alpha-capable accelerated
    // windows. That alpha description masks the system backdrop. D3D11 keeps
    // its premultiplied DirectComposition swapchain, so suspend only DWM_BB
    // while system Acrylic owns the backdrop and restore Qt's description on exit.
    // A frameless window has no non-client frame for DWM to paint the material
    // into, so the client area must extend the frame while the material is active.
    if (enabled) {
        if (!extendFrame(m_handle, true))
            return false;
        if (!alphaBehind(m_handle, false) || !systemMaterial(m_handle, 3)) {
            extendFrame(m_handle, false);
            return false;
        }
        refreshFrame(m_handle);
        return true;
    }
    const bool cleared = systemMaterial(m_handle, 1);
    const bool framed = extendFrame(m_handle, false);
    const bool restored = alphaBehind(m_handle, true);
    refreshFrame(m_handle);
    return cleared && framed && restored;
}
void WindowsWindowBackdrop::queueRepair()
{
    if (m_repairQueued || m_build < 22000)
        return;
    m_repairQueued = true;
    // Reassert DWM corner/material policy after Qt's native handler. Qt resets
    // the frame margins and DWM_BB during window setup, including after
    // composition changes, so the repair must restore both. The QObject context
    // cancels this callback on teardown.
    QMetaObject::invokeMethod(
        this,
        [this] {
            m_repairQueued = false;
            applyCorners();
            if (m_enabled && m_build >= 22621 && m_window && m_handle && IsWindow(m_handle)) {
                if (material(true)) {
                    m_error.clear();
                    emit completed({});
                } else {
                    material(false);
                    m_error = tr("系统背景接口不可用，窗口毛玻璃未能恢复");
                    emit completed(m_error);
                }
            }
        },
        Qt::QueuedConnection);
}
bool WindowsWindowBackdrop::nativeEventFilter(const QByteArray &, void *message, qintptr *)
{
    const auto *event = static_cast<MSG *>(message);
    if (!event || event->hwnd != m_handle)
        return false;
    if (event->message == WM_NCDESTROY) {
        m_handle = nullptr;
        m_enabled = m_suspended = false;
    } else if (m_build >= 22000 &&
               (event->message == WM_DWMCOMPOSITIONCHANGED || event->message == WM_STYLECHANGED)) {
        queueRepair();
    }
    if (m_enabled && m_build < 22621) {
        // Suspend expensive Accent Acrylic during the OS modal move/size loop,
        // including stationary mouse pauses; ordinary geometry signals cannot do this.
        if (event->message == WM_ENTERSIZEMOVE) {
            m_suspended = true;
            if (!accent(0))
                qWarning("Failed to suspend window Acrylic");
        } else if (event->message == WM_EXITSIZEMOVE ||
                   (event->message == WM_DWMCOMPOSITIONCHANGED && !m_suspended)) {
            m_suspended = false;
            if (!accent(4))
                qWarning("Failed to restore window Acrylic");
        }
    }
    // Observe only: Qt continues processing activation, hit testing and ownership.
    return false;
}
} // namespace EchoMusic
