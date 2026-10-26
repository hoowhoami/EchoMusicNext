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
Qt::WindowFlags WindowsWindowBackdrop::windowFlags(int build)
{
    if (build >= 22621) {
        // Alpha + FramelessWindowHint makes Qt use WS_EX_LAYERED, which cannot
        // receive the system backdrop. Keep the native frame and let the client
        // occupy it; CustomizeWindowHint leaves caption painting to QML.
        return Qt::Window | Qt::ExpandedClientAreaHint | Qt::NoTitleBarBackgroundHint |
               Qt::CustomizeWindowHint | Qt::WindowSystemMenuHint;
    }
    return Qt::Window | Qt::FramelessWindowHint;
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
    applyWindowPolicy();
}
void WindowsWindowBackdrop::applyWindowPolicy()
{
    if (m_build < 22000 || !m_handle || !IsWindow(m_handle))
        return;
    if (m_window && m_window->flags().testFlag(Qt::ExpandedClientAreaHint) &&
        m_window->flags().testFlag(Qt::CustomizeWindowHint) &&
        !m_window->windowStates().testFlag(Qt::WindowFullScreen) &&
        (GetWindowLongPtrW(m_handle, GWL_STYLE) & WS_THICKFRAME)) {
        // QWindow publishes its new state only after the QPA transition. A
        // queued repair can run during that transition, while QWindow still
        // says Normal/Maximized but Qt has already removed the native frame.
        // Never put the caption back onto that fullscreen-style HWND.
        // Qt disables SC_CLOSE when CustomizeWindowHint omits its native close
        // button. The QML caption still needs the system menu and Alt+F4.
        if (const auto menu = GetSystemMenu(m_handle, FALSE))
            EnableMenuItem(menu, SC_CLOSE, MF_BYCOMMAND | MF_ENABLED);
        // Qt's flags leave the minimize/maximize buttons out (the QML caption
        // draws its own), but the shell is stricter: it only toggles the window
        // from its taskbar button when the native style carries WS_MINIMIZEBOX.
        // Setting the bits on the native window only - without ever handing them
        // to Qt - keeps the QML caption the sole painter, so no native buttons
        // appear, while the taskbar toggle and the system menu entries work.
        //
        // WS_CAPTION is the same kind of native-only bit: DWM only animates
        // minimize/restore for windows that carry a caption, and CustomizeWindowHint
        // leaves it off, so without it the window disappears in a single frame
        // instead of shrinking to the taskbar. The client still covers the frame
        // (ExpandedClientAreaHint) and QML paints the whole caption, so the bit
        // restores the animation without bringing back native chrome.
        const LONG_PTR style = GetWindowLongPtrW(m_handle, GWL_STYLE);
        const LONG_PTR withFrame = style | WS_CAPTION | WS_MINIMIZEBOX | WS_MAXIMIZEBOX;
        if (style != withFrame) {
            SetWindowLongPtrW(m_handle, GWL_STYLE, withFrame);
            // The changed frame bits reach the window manager only after a
            // recalculation; without it the client area keeps the old metrics.
            SetWindowPos(m_handle, nullptr, 0, 0, 0, 0,
                         SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                             SWP_NOACTIVATE);
        }
    }
    applyFullscreenGeometry();
    // Frameless Qt windows do not qualify for DWM's automatic heuristics.
    // This documented opt-in is independent of the user's Acrylic preference;
    // DWM still owns square maximized/snapped/full-screen corners.
    const DWM_WINDOW_CORNER_PREFERENCE preference = DWMWCP_ROUND;
    const auto result = DwmSetWindowAttribute(m_handle, DWMWA_WINDOW_CORNER_PREFERENCE, &preference,
                                              sizeof(preference));
    if (FAILED(result))
        qWarning("DWM rounded-corner request failed: HRESULT=0x%08lx", DWORD(result));
}
void WindowsWindowBackdrop::applyFullscreenGeometry()
{
    if (!m_window || !m_window->flags().testFlag(Qt::ExpandedClientAreaHint) ||
        !m_window->windowStates().testFlag(Qt::WindowFullScreen) ||
        m_window->windowStates().testFlag(Qt::WindowMinimized) || IsIconic(m_handle) ||
        !IsWindowVisible(m_handle))
        return;
    MONITORINFO monitor{sizeof(MONITORINFO)};
    RECT bounds{};
    if (!GetMonitorInfoW(MonitorFromWindow(m_handle, MONITOR_DEFAULTTONEAREST), &monitor) ||
        !GetWindowRect(m_handle, &bounds))
        return;
    // A caption/frame repaired during Qt's synchronous state transition can
    // constrain the window to rcWork. Reconcile the completed fullscreen
    // request with rcMonitor in native pixels, without replacing Qt's saved
    // normal geometry, flags, activation or minimize/restore ownership.
    const LONG_PTR style = GetWindowLongPtrW(m_handle, GWL_STYLE);
    // The inverse transition has the same ordering: Qt may still publish
    // FullScreen after restoring its saved native frame. Do not undo that exit.
    if (style & WS_THICKFRAME)
        return;
    const LONG_PTR fullscreenStyle = style & ~LONG_PTR(WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX |
                                                       WS_MAXIMIZEBOX | WS_MAXIMIZE);
    const bool changed = style != fullscreenStyle;
    if (changed)
        SetWindowLongPtrW(m_handle, GWL_STYLE, fullscreenStyle);
    const auto &screen = monitor.rcMonitor;
    if (changed || !EqualRect(&bounds, &screen)) {
        if (!SetWindowPos(m_handle, nullptr, screen.left, screen.top, screen.right - screen.left,
                          screen.bottom - screen.top,
                          SWP_NOZORDER | SWP_NOACTIVATE | (changed ? SWP_FRAMECHANGED : 0)))
            qWarning("Fullscreen monitor geometry failed: error=%lu", GetLastError());
    }
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
    // Accent's ABGR tint needs real alpha on the legacy layered path. An almost
    // transparent tint can leave only desktop transparency instead of Acrylic.
    const DWORD tint = m_dark ? 0x8c545454 : 0x8cd3d3d3;
    AccentPolicy policy{state, 0, state == 4 ? tint : DWORD(0), 0};
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
    m_dark = dark;
    // DWM's built-in tint follows the resolved light/dark variant.
    DwmSetWindowAttribute(m_handle, 20, &darkMode, sizeof(darkMode));
    const bool ok = m_build >= 22621 ? material(enabled) : accent(enabled && !m_suspended ? 4 : 0);
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
    // The main window is non-layered from creation on this OS path. Keep its
    // alpha-capable swapchain, but suspend Qt's DWM_BB while Acrylic owns the
    // extended frame. Restore the window's own frame policy when clearing it.
    const bool expanded = m_window && m_window->flags().testFlag(Qt::ExpandedClientAreaHint);
    if (enabled) {
        if (GetWindowLongPtrW(m_handle, GWL_EXSTYLE) & WS_EX_LAYERED)
            return false;
        if (!extendFrame(m_handle, true))
            return false;
        if (!alphaBehind(m_handle, false) || !systemMaterial(m_handle, 3)) {
            extendFrame(m_handle, expanded);
            return false;
        }
        refreshFrame(m_handle);
        return true;
    }
    const bool cleared = systemMaterial(m_handle, 1);
    const bool framed = extendFrame(m_handle, expanded);
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
            applyWindowPolicy();
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
bool WindowsWindowBackdrop::hitTest(const MSG &event, qintptr *result) const
{
    if (!result || !m_window || !m_window->flags().testFlag(Qt::ExpandedClientAreaHint) ||
        !m_window->flags().testFlag(Qt::CustomizeWindowHint))
        return false;
    // Qt 6.12's expanded/custom-caption hit test synthesizes mouse events from
    // GetAsyncKeyState across the entire client area, including releases with
    // the old button mask. That corrupts Quick's button/grab/hover state. Keep
    // ordinary input on WM_MOUSE* and leave caption gestures to the QML owner.
    *result = HTCLIENT;
    if (m_window->windowStates().testAnyFlags(Qt::WindowFullScreen | Qt::WindowMaximized) ||
        IsZoomed(m_handle))
        return true;
    RECT bounds{};
    if (!GetWindowRect(m_handle, &bounds))
        return true;
    // LPARAM is signed screen coordinates in physical pixels, also on monitors
    // left/above the primary screen. Never mix these with QML's scaled geometry.
    const int x = static_cast<short>(LOWORD(event.lParam));
    const int y = static_cast<short>(HIWORD(event.lParam));
    const UINT dpi = GetDpiForWindow(m_handle);
    const int padding = GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
    const int borderX = GetSystemMetricsForDpi(SM_CXSIZEFRAME, dpi) + padding;
    const int borderY = GetSystemMetricsForDpi(SM_CYSIZEFRAME, dpi) + padding;
    const bool resizeX = m_window->minimumWidth() < m_window->maximumWidth();
    const bool resizeY = m_window->minimumHeight() < m_window->maximumHeight();
    const bool left = resizeX && x >= bounds.left && x < bounds.left + borderX;
    const bool right = resizeX && x < bounds.right && x >= bounds.right - borderX;
    const bool top = resizeY && y >= bounds.top && y < bounds.top + borderY;
    const bool bottom = resizeY && y < bounds.bottom && y >= bounds.bottom - borderY;
    if (left)
        *result = top ? HTTOPLEFT : bottom ? HTBOTTOMLEFT : HTLEFT;
    else if (right)
        *result = top ? HTTOPRIGHT : bottom ? HTBOTTOMRIGHT : HTRIGHT;
    else if (top)
        *result = HTTOP;
    else if (bottom)
        *result = HTBOTTOM;
    return true;
}
bool WindowsWindowBackdrop::nativeEventFilter(const QByteArray &, void *message, qintptr *result)
{
    const auto *event = static_cast<MSG *>(message);
    if (!event || event->hwnd != m_handle)
        return false;
    if (event->message == WM_NCHITTEST && hitTest(*event, result))
        return true;
    if (event->message == WM_NCDESTROY) {
        m_handle = nullptr;
        m_enabled = m_suspended = false;
    } else if (m_build >= 22000 &&
               (event->message == WM_DWMCOMPOSITIONCHANGED || event->message == WM_STYLECHANGED ||
                event->message == WM_DPICHANGED || event->message == WM_DISPLAYCHANGE)) {
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
    // All other messages retain Qt's activation, state and ownership handling.
    return false;
}
} // namespace EchoMusic
