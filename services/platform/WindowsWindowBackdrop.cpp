#include "WindowsWindowBackdrop.h"
#include <QCoreApplication>
#include <QOperatingSystemVersion>
#include <QWindow>
#include <QJsonDocument>
#include <QLoggingCategory>
#include <QColor>
#include <dwmapi.h>
#include <commctrl.h>
#include <algorithm>
#include <cmath>
namespace EchoMusic
{
namespace
{
Q_LOGGING_CATEGORY(windowLog, "echo.platform.windows.window")
// DWMWA_SYSTEMBACKDROP_TYPE, DWMWA_BORDER_COLOR and their values are missing
// from older SDK headers; pin the numbers this adapter relies on.
constexpr int kAttrSystemBackdrop = 38;
constexpr int kAttrBorderColor = 34;
constexpr UINT kBorderDefault = 0xFFFFFFFF; // DWMWA_COLOR_DEFAULT
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
SetComposition compositionReadApi()
{
    const auto module = GetModuleHandleW(L"user32.dll");
    return module ? reinterpret_cast<SetComposition>(
                        GetProcAddress(module, "GetWindowCompositionAttribute"))
                  : nullptr;
}
bool ensureBlurBehind(HWND handle)
{
    // Re-arm Qt's own opt-in (qwindowswindow applyBlurBehindWindow): the
    // -1,-1,0,0 region keeps DWM's legacy blur surface empty, while the enable
    // bit still gates whole-window alpha compositing for the swapchain.
    DWM_BLURBEHIND options{};
    options.dwFlags = DWM_BB_ENABLE;
    options.fEnable = TRUE;
    options.hRgnBlur = CreateRectRgn(-1, -1, 0, 0);
    const bool ok = SUCCEEDED(DwmEnableBlurBehindWindow(handle, &options));
    if (options.hRgnBlur)
        DeleteObject(options.hRgnBlur);
    return ok;
}
bool disableBlurBehind(HWND handle)
{
    // DWM_BB's blur region renders a single static snapshot on Windows 11
    // 22H2+ that never updates; the system backdrop and the DirectComposition
    // swapchain alpha replace it. Qt re-enables the region on window creation,
    // flag and composition changes - the repair queue re-disables it after
    // each of those.
    DWM_BLURBEHIND options{};
    options.dwFlags = DWM_BB_ENABLE;
    options.fEnable = FALSE;
    return SUCCEEDED(DwmEnableBlurBehindWindow(handle, &options));
}
bool frameBorder(HWND handle, UINT color)
{
    return SUCCEEDED(DwmSetWindowAttribute(handle, kAttrBorderColor, &color, sizeof(color)));
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
// The DWM window transitions (minimize, maximize, restore) are gated on
// WS_CAPTION at the transition start. Translucent windows live caption-less
// - DWM paints its native caption buttons underneath the client surface and
// they bleed through every translucent pixel - so their transitions never
// animate. The caption is added for the duration of a transition instead: it
// registers the animation, and the queued repair (which strips the caption
// for the translucent presentation anyway) removes it once the state has
// settled. Removing it mid-transition does not abort the animation. Since
// the frameless WM_NCCALCSIZE keeps the client equal to the whole window,
// the transient caption changes no layout.
void ensureCaption(HWND window)
{
    const LONG_PTR style = GetWindowLongPtrW(window, GWL_STYLE);
    if (style & WS_CAPTION)
        return;
    SetWindowLongPtrW(window, GWL_STYLE, style | WS_CAPTION);
}
// Mirror of ensureCaption: a maximize must start without the caption so the
// maximize engine lands on the bare work area instead of the invisible
// resize-border ring (see maximizeInfoToWork). The repair restores the
// policy's caption after the state settles.
void removeCaption(HWND window)
{
    const LONG_PTR style = GetWindowLongPtrW(window, GWL_STYLE);
    if (!(style & WS_CAPTION))
        return;
    SetWindowLongPtrW(window, GWL_STYLE, style & ~WS_CAPTION);
}
// Shrink a zoomed window onto the bare work area of its monitor. The ring a
// captioned maximize lands is continuously re-enforced on any resize, so the
// caller must have uncaptioned the window first - uncaptioned maximized
// windows are freely resizable and the shrink sticks exactly on the work
// area (measured 2026-11-05).
void shrinkMaximizedToWork(HWND window)
{
    MONITORINFO monitor{sizeof(MONITORINFO)};
    if (!GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor))
        return;
    const RECT &work = monitor.rcWork;
    SetWindowPos(window, nullptr, work.left, work.top, work.right - work.left,
                 work.bottom - work.top, SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
}
// Windows maximizes a captioned window over the work area plus the invisible
// resize-border ring, and enforces that rect below message level - for any
// resize of the captioned maximized window (a SetWindowPos onto the work area
// is re-forced to the ring). Uncaptioned maximizes take the WM_GETMINMAXINFO
// ptMaxSize answer verbatim, and once the caption is gone an uncaptioned
// maximized window is freely resizable. The pipeline uses this: maximizes
// start captioned (the DWM animation plays, the ring lands), and the queued
// repair settles by uncaptioning and shrinking the window onto the bare work
// area - no ring overlap with the taskbar for topmost windows, and the client
// keeps equal to the window rect so Qt's margin cache stays zero. This
// rewrite is the belt for the caption-less maximize paths that skip the
// transient-caption hook (aero snap, exotic entries): their engine takes this
// ptMaxSize directly, landing the bare work area in one step.
void maximizeInfoToWork(HWND window, MINMAXINFO *info)
{
    MONITORINFO monitor{sizeof(MONITORINFO)};
    if (!GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor))
        return;
    const RECT &work = monitor.rcWork;
    // ptMaxPosition is in workspace coordinates: relative to the monitor the
    // window maximizes onto.
    info->ptMaxPosition =
        POINT{work.left - monitor.rcMonitor.left, work.top - monitor.rcMonitor.top};
    info->ptMaxSize = POINT{work.right - work.left, work.bottom - work.top};
}
} // namespace
WindowsWindowBackdrop::WindowsWindowBackdrop()
{
    m_build = QOperatingSystemVersion::current().microVersion();
    m_transitionCaption.setSingleShot(true);
    m_transitionCaption.setInterval(350);
    connect(&m_transitionCaption, &QTimer::timeout, this, [this] { queueRepair(); });
    if (qApp)
        qApp->installNativeEventFilter(this);
}
WindowsWindowBackdrop::~WindowsWindowBackdrop()
{
    if (qApp)
        qApp->removeNativeEventFilter(this);
    clear();
    releaseSurface();
}
Qt::WindowFlags WindowsWindowBackdrop::windowFlags(int build)
{
    if (build >= 22621) {
        // Frameless from creation, matching the Win10 branch and the runtime
        // translucent presentation. The native frame reserved the DWM resize
        // borders around the client, so the restored geometry - and every
        // opaque-to-translucent switch - grew the window by that ring, and the
        // persisted geometry ratcheted one ring larger per restart. Alpha +
        // FramelessWindowHint still makes Qt apply WS_EX_LAYERED, which
        // applyWindowPolicy strips; the legacy Accent acrylic does not need
        // the layered bit gone. CustomizeWindowHint leaves caption painting
        // to QML.
        return Qt::Window | Qt::FramelessWindowHint | Qt::ExpandedClientAreaHint |
               Qt::NoTitleBarBackgroundHint | Qt::CustomizeWindowHint | Qt::WindowSystemMenuHint;
    }
    return Qt::Window | Qt::FramelessWindowHint;
}
void WindowsWindowBackdrop::attach(QWindow *window)
{
    const auto handle = window ? reinterpret_cast<HWND>(window->winId()) : nullptr;
    if (window && m_window == window && m_handle == handle && m_subclassInstalled)
        return;
    // The fullscreen dance may have left the frameless hint behind when the
    // previous session ended mid-fullscreen; translucent themes keep the hint
    // (sticky, see applyWindowPolicy) and the repair re-adds it if needed, so
    // only drop it when the dance owns it.
    if (m_framelessForFullscreen && m_window && m_window->flags().testFlag(Qt::FramelessWindowHint))
        m_window->setFlags(m_window->flags() & ~Qt::FramelessWindowHint);
    m_framelessForFullscreen = false;
    m_restoreFullscreen = false;
    m_geometryQueued = false;
    m_maximizeSettlePending = false;
    clear();
    releaseSurface();
    disconnect(m_visibilityConnection);
    disconnect(m_stateConnection);
    disconnect(m_screenConnection);
    m_window = window;
    m_handle = handle;
    m_error.clear();
    if (window) {
        m_visibilityConnection = connect(window, &QWindow::visibilityChanged, this, [this] {
            if (!m_window)
                return;
            if (m_window->visibility() == QWindow::Hidden ||
                m_window->visibility() == QWindow::Minimized) {
                // Qt emulates fullscreen with style/geometry only. Hiding or
                // minimizing the window drops it, so remember the intent and
                // re-apply it once the window is shown again.
                if (m_window->windowStates().testFlag(Qt::WindowFullScreen))
                    m_restoreFullscreen = true;
            }
            queueRepair();
        });
        // QWindow publishes WindowFullScreen only after the QPA transition, which
        // is later than the WM_STYLECHANGED that starts it. Repair on the state
        // signal so the fullscreen geometry is reconciled once Qt agrees.
        m_stateConnection =
            connect(window, &QWindow::windowStateChanged, this, [this] { queueRepair(); });
        // Windows reports no DPI/display change when a fullscreen window follows
        // the screen, so monitor geometry is re-reconciled here.
        m_screenConnection =
            connect(window, &QWindow::screenChanged, this, [this] { queueRepair(); });
    }
    if (m_handle) {
        m_subclassInstalled =
            SetWindowSubclass(m_handle, windowProcedure, reinterpret_cast<UINT_PTR>(this),
                              reinterpret_cast<DWORD_PTR>(this));
        if (!m_subclassInstalled)
            qCWarning(windowLog) << "Window input/composition subclass failed" << GetLastError();
    }
    applyWindowPolicy();
    logState("attach");
}
void WindowsWindowBackdrop::releaseSurface()
{
    if (m_handle && IsWindow(m_handle) && m_subclassInstalled)
        RemoveWindowSubclass(m_handle, windowProcedure, reinterpret_cast<UINT_PTR>(this));
    m_subclassInstalled = false;
}
bool WindowsWindowBackdrop::fullscreen() const
{
    if (!m_window || !m_handle)
        return false;
    // The native frame disappears before QWindow publishes the entering state.
    return m_window->windowStates().testFlag(Qt::WindowFullScreen) ||
           (m_window->flags().testFlag(Qt::ExpandedClientAreaHint) &&
            !(GetWindowLongPtrW(m_handle, GWL_STYLE) & WS_THICKFRAME));
}
void WindowsWindowBackdrop::applyWindowPolicy()
{
    if (!m_handle || !IsWindow(m_handle))
        return;
    // The window keeps its GDI redirection surface by design: the
    // WS_EX_NOREDIRECTIONBITMAP runtime attempt is a post-creation style set
    // that user32 rejects (error 87 - the bit only takes effect when Qt's
    // WindowCreationData applies it, and the process-level QPA variable was
    // dropped because raster popups lose their content without a redirection
    // bitmap). Every presentation resize therefore reallocates that surface,
    // and queueRepair/applyFullscreenGeometryNow zero it (PatBlt BLACKNESS)
    // since the pure-D3D window holds no GDI content.
    const bool translucent = m_enabled || m_transparency > 0;
    // While the theme is translucent the window goes frameless: Qt then omits
    // the frame margins (shouldOmitFrameAdjustment) and the swapchain covers
    // the whole window - with the margins kept, Qt's geometry_sys shrinks the
    // scene by them and the uncovered strip shows as a border ring on the
    // right/bottom edges. WS_THICKFRAME is re-added below so the DWM shadow,
    // native resize and taskbar snapping stay; Qt's own frameless
    // WM_NCCALCSIZE branch keeps the client equal to the whole window.
    // Frameless + alpha makes Qt apply WS_EX_LAYERED. The legacy Accent
    // acrylic composes independently of that bit, but every validated state
    // ran without it and Qt re-applies it on the next initialize - stripped
    // right after, same as DWM_BB. The fullscreen dance owns the same hint
    // while a fullscreen session is in flight (fullscreen() also covers the
    // transition, where the window states have not been published yet).
    const bool fullscreenActive = fullscreen();
    const bool wantFrameless = translucent || fullscreenActive;
    // The hint comes from windowFlags() at creation on every build; this block
    // only re-adds it when the fullscreen dance dropped it. It is never
    // removed afterwards: Qt's shouldOmitFrameAdjustment checks the flag
    // dynamically, and removing it later would churn the geometry during
    // state transitions (a maximize restore lost its state that way). An
    // opaque theme works fine with the hint - its QML covers the whole window.
    if (wantFrameless && m_window && !m_window->flags().testFlag(Qt::FramelessWindowHint) &&
        !m_window->windowStates().testFlag(Qt::WindowFullScreen)) {
        auto flags = m_window->flags();
        flags.setFlag(Qt::FramelessWindowHint, true);
        m_window->setFlags(flags);
    }
    if (m_window && m_window->flags().testFlag(Qt::ExpandedClientAreaHint) &&
        m_window->flags().testFlag(Qt::CustomizeWindowHint) &&
        !m_window->windowStates().testFlag(Qt::WindowFullScreen)) {
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
        // Both skin states present frameless (POPUP + THICKFRAME, no
        // CAPTION): THICKFRAME keeps the DWM shadow, native resize and
        // snapping; the caption must stay off - DWM paints its native caption
        // buttons underneath the client surface and they bleed through every
        // translucent pixel of the custom caption on the DirectComposition
        // presentation, and a caption present at a maximize start makes
        // user32 land the resize-border ring rect whose transparent rows
        // cover the taskbar for topmost windows while writing the ring
        // margins into Qt's frozen margin cache (2026-11-05). The transient
        // transition caption (minimize / windowed-restore animations) is the
        // one exception: it stays until the DWM animation has played out
        // (m_transitionCaption), whose timeout re-queues the repair that
        // strips it. Maximize transitions run caption-less by design.
        LONG_PTR style = GetWindowLongPtrW(m_handle, GWL_STYLE);
        LONG_PTR withFrame = (style | WS_POPUP | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX) &
                             ~LONG_PTR(WS_CAPTION);
        // Keep the transient transition caption alive while its timer runs -
        // except on a maximized window: the maximize settle has already
        // uncaptioned it to free the ring enforcement, and re-adding would
        // bring the ring (and the native buttons bleeding through translucent
        // pixels) back for the remainder of the timer. Minimize and windowed
        // restores are never zoomed at this point, so their animations keep
        // their caption.
        if (m_transitionCaption.isActive() && !IsZoomed(m_handle))
            withFrame |= WS_CAPTION;
        if (style != withFrame) {
            SetWindowLongPtrW(m_handle, GWL_STYLE, withFrame);
            // The changed frame bits reach the window manager only after a
            // recalculation; without it the client area keeps the old metrics.
            SetWindowPos(m_handle, nullptr, 0, 0, 0, 0,
                         SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                             SWP_NOACTIVATE);
        }
        // Frameless + alpha makes Qt apply WS_EX_LAYERED (with an opaque
        // LWA_ALPHA), and layered windows cannot host the system backdrop -
        // the frosted material would silently disappear. Strip it; Qt re-applies
        // it on the next initialize and the repair re-strips again.
        if (wantFrameless) {
            const LONG_PTR exStyle = GetWindowLongPtrW(m_handle, GWL_EXSTYLE);
            if (exStyle & WS_EX_LAYERED) {
                SetWindowLongPtrW(m_handle, GWL_EXSTYLE, exStyle & ~LONG_PTR(WS_EX_LAYERED));
                SetWindowPos(m_handle, nullptr, 0, 0, 0, 0,
                             SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                                 SWP_NOACTIVATE);
            }
        }
    }
    applyFullscreenGeometry();
    // Frameless Qt windows do not qualify for DWM's automatic heuristics.
    // This documented opt-in is independent of the user's Acrylic preference;
    // DWM still owns square maximized/snapped/full-screen corners. A fullscreen
    // surface has to reach the monitor corners, so rounding is turned off there
    // and restored together with the windowed frame.
    const DWM_WINDOW_CORNER_PREFERENCE preference = fullscreen() ? DWMWCP_DONOTROUND : DWMWCP_ROUND;
    const auto result = DwmSetWindowAttribute(m_handle, DWMWA_WINDOW_CORNER_PREFERENCE, &preference,
                                              sizeof(preference));
    if (FAILED(result))
        qWarning("DWM rounded-corner request failed: HRESULT=0x%08lx", DWORD(result));
}
void WindowsWindowBackdrop::applyFullscreenGeometry()
{
    // The queued repair can run inside Qt's synchronous state transition, before
    // Qt finishes its own geometry bookkeeping. Reconciling the native frame at
    // that point leaves QQuickWindow with the intermediate size, so defer it by
    // one event-loop turn.
    if (m_geometryQueued)
        return;
    m_geometryQueued = true;
    // The repair may outlive the adapter (window teardown), so keep a guard
    // instead of dereferencing a destroyed receiver.
    QPointer<WindowsWindowBackdrop> guard(this);
    QMetaObject::invokeMethod(
        this,
        [guard] {
            if (!guard)
                return;
            guard->m_geometryQueued = false;
            guard->applyFullscreenGeometryNow();
        },
        Qt::QueuedConnection);
}
void WindowsWindowBackdrop::applyFullscreenGeometryNow()
{
    if (!m_window || !m_handle || IsIconic(m_handle) || !IsWindowVisible(m_handle))
        return;
    if (!m_window->windowStates().testFlag(Qt::WindowFullScreen) ||
        m_window->windowStates().testFlag(Qt::WindowMinimized)) {
        // Leaving fullscreen restores the framed window, so the temporary
        // frameless hint has to go with it.
        if (m_framelessForFullscreen) {
            m_framelessForFullscreen = false;
            m_window->setFlags(m_window->flags() & ~Qt::FramelessWindowHint);
        }
        return;
    }
    if (!m_window->flags().testFlag(Qt::ExpandedClientAreaHint))
        return;
    // Qt derives the fullscreen QWindow geometry from the frame margins of the
    // expanded client area, and caches those margins for the lifetime of the
    // window. Fractional scaling rounds them down, so the surface stays a few
    // device-independent pixels smaller than the monitor (transparent strips on
    // the right/bottom edge). A frameless window has no margins to subtract and
    // covers the monitor exactly. Qt pairs the hint with WS_EX_LAYERED because
    // the window has an alpha buffer; the fullscreen surface is opaque and uses
    // no system backdrop, and the frame (and the regular surface policy) comes
    // back together when the hint is dropped.
    if (!m_window->flags().testFlag(Qt::FramelessWindowHint)) {
        m_framelessForFullscreen = true;
        m_window->setFlags(m_window->flags() | Qt::FramelessWindowHint);
        return;
    }
    MONITORINFO monitor{sizeof(MONITORINFO)};
    RECT bounds{};
    if (!GetMonitorInfoW(MonitorFromWindow(m_handle, MONITOR_DEFAULTTONEAREST), &monitor) ||
        !GetWindowRect(m_handle, &bounds))
        return;
    // Qt may still publish FullScreen while it has already restored the saved
    // native frame on the way out; never undo that exit.
    const LONG_PTR style = GetWindowLongPtrW(m_handle, GWL_STYLE);
    if (style & WS_THICKFRAME) {
        qCDebug(windowLog) << "fullscreen-settle skip: thickframe present";
        return;
    }
    // A repair that ran before the frameless hint was applied can leave frame
    // bits behind. Strip them without touching Qt's saved normal geometry,
    // activation or minimize/restore ownership.
    const LONG_PTR fullscreenStyle = style & ~LONG_PTR(WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX |
                                                       WS_MAXIMIZEBOX | WS_MAXIMIZE);
    const bool changed = style != fullscreenStyle;
    if (changed)
        SetWindowLongPtrW(m_handle, GWL_STYLE, fullscreenStyle);
    // The window and its client area have to match the monitor in native pixels,
    // including after a monitor or DPI change that Qt does not report.
    RECT client{};
    POINT origin{};
    const bool clientCovered =
        GetClientRect(m_handle, &client) && ClientToScreen(m_handle, &origin) &&
        origin.x == bounds.left && origin.y == bounds.top &&
        client.right == bounds.right - bounds.left && client.bottom == bounds.bottom - bounds.top;
    const auto &screen = monitor.rcMonitor;
    if (changed || !EqualRect(&bounds, &screen) || !clientCovered) {
        if (!SetWindowPos(m_handle, nullptr, screen.left, screen.top, screen.right - screen.left,
                          screen.bottom - screen.top,
                          SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED))
            qWarning("Fullscreen monitor geometry failed: error=%lu", GetLastError());
    }
    // Qt does not always deliver the final resize to the scene, so the
    // QQuickWindow root item can keep its pre-fullscreen size (a transparent
    // strip at the right/bottom edge even though the window covers the monitor).
    // Nudge the window by one pixel and back to make the scene geometry follow.
    const QSize size = m_window->size();
    m_window->resize(size - QSize(1, 1));
    m_window->resize(size);
    // 2026-11-05 fullscreen white strip: the window keeps a GDI redirection
    // surface (no WS_EX_NOREDIRECTIONBITMAP on this presentation path), and
    // DWM composites it between the DirectComposition swapchain and the
    // Accent backdrop. The fullscreen transition reallocates that surface;
    // its grown rows come up uninitialized (white) and are never drawn by
    // either GDI or the scene, so the translucent skin shows them as a
    // full-width white strip along the old work-area bottom line. No DWM
    // knob refreshes it (accent off/on, blur-behind toggle, extend frame,
    // border color, cloak pulse all verified ineffective). The surface has
    // no GDI content on this pure-D3D window, so zero it whole - the rows
    // then composite as transparent and the backdrop shows through.
    if (const HDC hdc = GetDC(m_handle)) {
        RECT client{};
        GetClientRect(m_handle, &client);
        PatBlt(hdc, 0, 0, client.right, client.bottom, BLACKNESS);
        ReleaseDC(m_handle, hdc);
    }
}
bool WindowsWindowBackdrop::supported() const
{
    BOOL compositing = FALSE;
    return m_window && m_handle && IsWindow(m_handle) &&
           m_window->requestedFormat().alphaBufferSize() > 0 &&
           SUCCEEDED(DwmIsCompositionEnabled(&compositing)) && compositing &&
           compositionApi() != nullptr;
}
bool WindowsWindowBackdrop::accent(int state)
{
    const auto api = compositionApi();
    if (!api)
        return state == 0;
    // Match EchoMusic's ordinary legacy Acrylic mode. Its stronger theme tint
    // belongs to the separate keep-on-blur feature, not the default backend.
    const DWORD tint = 0x01000000;
    AccentPolicy policy{state, 0, state == 4 ? tint : DWORD(0), 0};
    CompositionAttribute data{19, &policy, sizeof(policy)};
    return api(m_handle, &data);
}
void WindowsWindowBackdrop::clear()
{
    if (m_handle && IsWindow(m_handle) && m_applied != Applied::None) {
        accent(0);
        frameBorder(m_handle, kBorderDefault);
        disableBlurBehind(m_handle);
        // Reset the frame extension unconditionally. The system backdrop it
        // once served is never written by this adapter, and a glass fill left
        // extended washes over the desktop that shows through the swapchain
        // whenever the material is off - the frost-less transparency mode
        // (applyMaterialState keeps the accent at 0 there and never re-touches
        // the extension). The legacy Accent acrylic is independent of it; the
        // extension used to stay at -1 forever after the first opaque
        // fullscreen suspension.
        extendFrame(m_handle, false);
        refreshFrame(m_handle);
    }
    m_applied = Applied::None;
    m_enabled = m_initialized = false;
}
QString WindowsWindowBackdrop::apply(bool enabled, bool dark, int transparency)
{
    transparency = qBound(0, transparency, 100);
    // Remember the request before any readiness gate: the first apply can race
    // ahead of the surface report, and the queued repair then re-runs the state
    // instead of waiting for the next theme change.
    m_requestedEnabled = enabled ? 1 : 0;
    m_requestedDark = dark ? 1 : 0;
    m_requestedTransparency = transparency;
    if (!m_window || !m_handle || !IsWindow(m_handle) || !m_subclassInstalled ||
        (enabled && !supported())) {
        m_error = tr("系统窗口合成接口不可用，窗口效果未能应用");
        emit completed(m_error);
        logState("setup-failed");
        return m_error;
    }
    if (m_initialized && enabled == m_enabled && dark == m_dark && transparency == m_transparency)
        return m_error;
    if (!applyMaterialState(enabled, dark, transparency)) {
        m_error = tr("系统背景接口不可用，窗口毛玻璃未能应用");
        emit completed(m_error);
        logState("apply-failed");
        return m_error;
    }
    emit completed({});
    logState("apply");
    return {};
}
void WindowsWindowBackdrop::prepareWindowTransition(bool maximizing)
{
    if (!m_handle || !IsWindow(m_handle) || fullscreen())
        return;
    // The app-side maximize/minimize/restore entries bypass the system menu,
    // so the transient transition caption has to be added here; see
    // ensureCaption for why, and m_transitionCaption for how long it stays.
    // Maximizes also carry the caption: the DWM animation is caption-gated,
    // and the ring rect the captioned engine lands is shrunk onto the bare
    // work area by the settle once the animation has played out. An already
    // maximized window (shown back from hide/mini without ever leaving the
    // zoomed state) has no geometry change to animate - Qt's show_sys runs
    // SW_SHOWMAXIMIZED for it, and with a caption that re-maximize lands the
    // ring and the settle shrink becomes a visible jump. Skip both; the
    // uncaptioned engine takes the bare work area verbatim.
    if (maximizing) {
        if (IsZoomed(m_handle))
            return;
        m_maximizeSettlePending = true;
        m_transitionCaption.start();
        ensureCaption(m_handle);
        return;
    }
    m_transitionCaption.start();
    ensureCaption(m_handle);
}
bool WindowsWindowBackdrop::applyMaterialState(bool enabled, bool dark, int transparency)
{
    // The state lands first: the window policy (frameless hint, caption bits,
    // LAYERED suppression) derives from it and must precede the material - the
    // frameless toggle makes Qt re-run initialize, which re-enables DWM_BB and
    // WS_EX_LAYERED behind the adapter's back.
    m_enabled = enabled;
    m_transparency = transparency;
    m_dark = dark;
    applyWindowPolicy();
    // A fullscreen surface is only opaque when the request is opaque - a
    // translucent skin keeps showing the desktop through the monitor-sized
    // window. Suspending the material for every fullscreen state made it
    // accent(0) + DWM_BB off: the frost vanished for the whole session, and
    // because Qt re-enables DWM_BB only on creation, flag and composition
    // changes (none of which the fullscreen dance runs), the opt-in stayed
    // disabled after the exit repair re-applied the accent - the window
    // rendered opaque until restart. The legacy Accent acrylic is independent
    // of the layered bit the dance re-pairs (applyWindowPolicy strips it
    // again), so translucent requests keep their material in fullscreen and
    // only an opaque request suspends.
    if (fullscreen() && !enabled && transparency <= 0) {
        clear();
        m_initialized = true;
        m_error.clear();
        m_applied = Applied::None;
        return true;
    }
    if ((enabled || transparency > 0) && !supported()) {
        clear();
        return false;
    }
    // DWM's built-in tint follows the resolved light/dark variant.
    const BOOL darkMode = dark;
    DwmSetWindowAttribute(m_handle, 20, &darkMode, sizeof(darkMode));
    // The enable bit is the alpha-compositing opt-in for the whole window on
    // this build - disabling it made the entire transparency mode render
    // opaque (the backdrop experiment, 2026-11-04). clear() turns it off on
    // attach, teardown and opaque-fullscreen suspension, and Qt only turns it
    // back on on creation, flag and composition changes - none of which run
    // while the material re-applies. Re-arm it whenever the skin is
    // translucent, or the premultiplied alpha of the swapchain stops being
    // composited after a fullscreen round trip.
    if (enabled || transparency > 0)
        ensureBlurBehind(m_handle);
    // Frosted: the legacy Accent acrylic (SetWindowCompositionAttribute,
    // ACCENT_ENABLE_ACRYLICBLURBEHIND) - more transparent than the system
    // backdrop material (DWMSBT_TRANSIENTWINDOW washes the backdrop toward a
    // flat desaturated tint) and consistent across builds. The accent policy
    // is window-wide, so the strip Qt's frozen frame margins leave uncovered
    // (QTBUG-117704) shows the same material as the body - no ring, and no
    // extended frame is needed: any glass fill would cover the desktop shown
    // through the swapchain when the material is off. The frame border hides
    // while the theme is translucent because applyWindowPolicy strips the
    // native caption.
    const bool ok = accent(enabled ? 4 : 0);
    if (!ok) {
        clear();
        return false;
    }
    m_applied = enabled ? Applied::LegacyAccent : Applied::None;
    m_initialized = true;
    m_error.clear();
    return true;
}
void WindowsWindowBackdrop::queueRepair()
{
    if (m_repairQueued || !m_handle || !m_window)
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
            if (!m_window || !m_handle || !IsWindow(m_handle))
                return;
            // Maximize settle: the captioned maximize landed the resize-border
            // ring and the DWM animation is playing. Uncaption and shrink onto
            // the bare work area now, while the morph still covers the screen -
            // the ring is only enforced while the window carries a caption
            // (measured 2026-11-05: SetWindowPos onto the work area is re-forced
            // to the ring for a captioned maximized window but sticks for an
            // uncaptioned one), and the client keeps equalling the whole window
            // rect so QTBUG-117704's guard never writes ring margins into the
            // frozen cache. The morph plays on regardless (mid-transition
            // caption removal does not abort it), so the composited geometry is
            // the work area from early on and no jump is visible.
            if (m_maximizeSettlePending) {
                m_maximizeSettlePending = false;
                if (IsZoomed(m_handle) && !fullscreen()) {
                    removeCaption(m_handle);
                    shrinkMaximizedToWork(m_handle);
                }
            }
            // A hidden fullscreen window loses the emulated state; restore it
            // once the show/minimize transition has completely settled.
            if (m_restoreFullscreen && m_window->isVisible()) {
                m_restoreFullscreen = false;
                if (!m_window->windowStates().testFlag(Qt::WindowFullScreen))
                    m_window->showFullScreen();
            }
            // Run after Qt's handler on both Win10 and Win11. Qt re-enables its
            // blur behind (creation, flag and composition changes) and resets
            // frame margins there; the repair reclaims the whole material state
            // from the last request, which also retries an apply that raced
            // ahead of the surface report. The window policy follows the state,
            // so the native caption bits track the appearance change too.
            if (m_requestedEnabled >= 0) {
                if (applyMaterialState(m_requestedEnabled == 1, m_requestedDark == 1,
                                       m_requestedTransparency)) {
                    emit completed({});
                } else {
                    m_error = tr("系统背景接口不可用，窗口毛玻璃未能恢复");
                    emit completed(m_error);
                }
            } else {
                applyWindowPolicy();
            }
            // Fallback net for ring rects the flag settle does not cover: a
            // maximize that ran while a transition caption from an earlier
            // minimize/restore was still alive (aero snap inside its 350ms
            // window - no SC_MAXIMIZE, no flag) landed the ring, and the
            // policy above has since stripped that caption. An uncaptioned
            // maximized window is freely resizable, so pull it onto the bare
            // work area; maximized windows that are already there are a
            // no-op EqualRect.
            if (IsZoomed(m_handle) && !fullscreen()) {
                MONITORINFO monitor{sizeof(MONITORINFO)};
                RECT bounds{};
                if (GetMonitorInfoW(MonitorFromWindow(m_handle, MONITOR_DEFAULTTONEAREST),
                                    &monitor) &&
                    GetWindowRect(m_handle, &bounds) && !EqualRect(&bounds, &monitor.rcWork)) {
                    removeCaption(m_handle);
                    shrinkMaximizedToWork(m_handle);
                }
            }
            // The GDI redirection surface is reallocated by every presentation
            // resize (maximize, restore, fullscreen) and its grown rows come up
            // uninitialized white; composited between the swapchain and the
            // Accent backdrop they show through the translucent skin as a
            // strip along the old geometry's bottom line (2026-11-05). The
            // surface holds no GDI content on this pure-D3D window - zero it
            // whole so the backdrop shows through the translucent rows.
            if (m_handle && IsWindow(m_handle)) {
                if (const HDC hdc = GetDC(m_handle)) {
                    RECT client{};
                    GetClientRect(m_handle, &client);
                    PatBlt(hdc, 0, 0, client.right, client.bottom, BLACKNESS);
                    ReleaseDC(m_handle, hdc);
                }
            }
            // 2026-11-05 topmost strip: report window vs client geometry so a
            // maximized state that did not land on the work area is visible in
            // the log alongside the native client rect.
            RECT windowRect{}, clientRect{};
            GetWindowRect(m_handle, &windowRect);
            GetClientRect(m_handle, &clientRect);
            qCDebug(windowLog) << "repair-geometry window-rect" << windowRect.left << windowRect.top
                               << windowRect.right << windowRect.bottom << "client"
                               << clientRect.right << clientRect.bottom << "window"
                               << m_window->width() << m_window->height();
            logState("repair");
        },
        Qt::QueuedConnection);
}
bool WindowsWindowBackdrop::hitTest(const MSG &event, qintptr *result) const
{
    if (!result || !m_window)
        return false;
    if (fullscreen()) {
        *result = HTCLIENT;
        return true;
    }
    if (!m_window->flags().testFlag(Qt::ExpandedClientAreaHint) ||
        !m_window->flags().testFlag(Qt::CustomizeWindowHint))
        return false;
    // Qt 6.12's expanded/custom-caption hit test synthesizes mouse events from
    // GetAsyncKeyState across the entire client area, including releases with
    // the old button mask. That corrupts Quick's button/grab/hover state. Keep
    // ordinary input on WM_MOUSE* and leave caption gestures to the QML owner.
    *result = HTCLIENT;
    if (m_window->windowStates().testFlag(Qt::WindowMaximized) || IsZoomed(m_handle))
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
LRESULT CALLBACK WindowsWindowBackdrop::windowProcedure(HWND window, UINT message, WPARAM wParam,
                                                        LPARAM lParam, UINT_PTR id, DWORD_PTR data)
{
    auto *self = reinterpret_cast<WindowsWindowBackdrop *>(data);
    if (message == WM_NCDESTROY) {
        RemoveWindowSubclass(window, windowProcedure, id);
        if (self->m_handle == window) {
            self->m_handle = nullptr;
            self->m_enabled = self->m_initialized = false;
            self->m_subclassInstalled = false;
        }
        return DefSubclassProc(window, message, wParam, lParam);
    }
    // Transient transition caption, see ensureCaption. Opaque windows carry
    // it anyway (no-op there); fullscreen belongs to the dance below. Every
    // maximize - including a minimized window restoring into the maximized
    // state (WPF_RESTORETOMAXIMIZED runs the same maximize engine) - carries
    // the caption so the DWM animation plays; the captioned engine lands the
    // resize-border ring rect and m_maximizeSettlePending marks the queued
    // settle that uncaptions and shrinks the window onto the bare work area
    // (see the repair callback).
    if (message == WM_SYSCOMMAND && !self->fullscreen()) {
        const auto command = wParam & 0xfff0;
        if (command == SC_MAXIMIZE) {
            self->m_maximizeSettlePending = true;
            self->m_transitionCaption.start();
            ensureCaption(window);
        } else if (command == SC_MINIMIZE || command == SC_RESTORE) {
            WINDOWPLACEMENT placement{sizeof(WINDOWPLACEMENT)};
            if (GetWindowPlacement(window, &placement) &&
                !(placement.flags & WPF_RESTORETOMAXIMIZED)) {
                self->m_transitionCaption.start();
                ensureCaption(window);
            } else {
                self->m_maximizeSettlePending = true;
                self->m_transitionCaption.start();
                ensureCaption(window);
            }
        }
    }
    // No WM_NCCALCSIZE override: Qt's frameless branch keeps the client equal
    // to the whole window rect for every state. While a captioned maximize
    // momentarily lands the resize-border ring the client covers it too (the
    // shell paints the ring rows until the settle shrinks the window onto the
    // bare work area), and because client size == window size throughout,
    // Qt's QTBUG-117704 guard never writes the ring into the frozen margin
    // cache - the cache stays at its windowed value of zero forever.
    if (self->fullscreen()) {
        // Qt excludes synchronously sent WM_SYSCOMMAND / WM_SIZING / WM_MOVING
        // from global native filters. Guard them on the real HWND subclass
        // chain, including commands generated by QWindow::startSystemMove/Resize.
        const auto command = wParam & 0xfff0;
        if (message == WM_SYSCOMMAND && (command == SC_MOVE || command == SC_SIZE))
            return 0;
        if (message == WM_NCHITTEST)
            return HTCLIENT;
        if ((message == WM_NCLBUTTONDOWN || message == WM_NCLBUTTONDBLCLK) &&
            (wParam == HTCAPTION || (wParam >= HTLEFT && wParam <= HTBOTTOMRIGHT) ||
             wParam == HTBORDER))
            return 0;
        if ((message == WM_MOVING || message == WM_SIZING) && lParam) {
            // Also stop a modal gesture that was already running on entry.
            RECT bounds{};
            if (GetWindowRect(window, &bounds))
                *reinterpret_cast<RECT *>(lParam) = bounds;
            return TRUE;
        }
    }
    // A fractional DPR renders the scene at an integer-logical size while the
    // native client rect moves in single device pixels, so the per-size
    // logical-to-client mapping carries a fractional phase, and during a live
    // drag every edge in the window re-rasterizes at a shifting sub-pixel
    // position - visible as border jitter on high-contrast surfaces such as
    // the FM disc. Snapping the proposed client size down to the smallest
    // device-pixel unit whose logical size is whole (the DPR fraction's
    // numerator: 5 px at 125%, 3 px at 150%) pins that phase to the nominal
    // DPR for the whole drag; the cost is that the layout grows in whole-DIP
    // steps instead of per device pixel. Unlike the fullscreen override above,
    // this only rewrites the proposal and falls through: the modal sizing loop
    // applies the modified RECT itself.
    if (message == WM_SIZING && lParam && !self->fullscreen() && self->m_optimizeResizeSnap) {
        auto &proposal = *reinterpret_cast<RECT *>(lParam);
        const qreal dpr = self->m_window ? self->m_window->devicePixelRatio() : 1.0;
        // Windows' fractional scale presets all reduce to small denominators
        // (125% -> 5/4, 150% -> 3/2, 175% -> 7/4); 16 is headroom beyond every
        // preset, and a DPR outside it keeps unit at 1, disabling snapping.
        qreal unit = 1.0;
        for (int denominator = 1; denominator <= 16; ++denominator) {
            const qreal scaled = dpr * denominator;
            if (qAbs(scaled - std::round(scaled)) < 1e-9) {
                unit = std::round(scaled);
                break;
            }
        }
        RECT windowRect{};
        RECT clientRect{};
        if (unit > 1.0 && GetWindowRect(window, &windowRect) && GetClientRect(window, &clientRect)) {
            const LONG frameWidth =
                (windowRect.right - windowRect.left) - (clientRect.right - clientRect.left);
            const LONG frameHeight =
                (windowRect.bottom - windowRect.top) - (clientRect.bottom - clientRect.top);
            const LONG proposedWidth = proposal.right - proposal.left - frameWidth;
            const LONG proposedHeight = proposal.bottom - proposal.top - frameHeight;
            const auto snapped = [unit](LONG proposed) {
                return static_cast<LONG>(std::max(unit, std::floor(proposed / unit) * unit));
            };
            if (wParam != WMSZ_LEFT && wParam != WMSZ_TOPLEFT && wParam != WMSZ_BOTTOMLEFT)
                proposal.right = proposal.left + frameWidth + snapped(proposedWidth);
            else
                proposal.left = proposal.right - frameWidth - snapped(proposedWidth);
            if (wParam != WMSZ_TOP && wParam != WMSZ_TOPLEFT && wParam != WMSZ_TOPRIGHT)
                proposal.bottom = proposal.top + frameHeight + snapped(proposedHeight);
            else
                proposal.top = proposal.bottom - frameHeight - snapped(proposedHeight);
        }
    }
    // The Acrylic stays on while the window is dragged: suspending it for the
    // duration of a modal move/size loop (the legacy drag-lag mitigation)
    // made the material visibly disappear for the whole drag, which reads
    // worse than the lag it avoided.
    const QPointer<WindowsWindowBackdrop> guard(self);
    const auto result = DefSubclassProc(window, message, wParam, lParam);
    if (!guard || self->m_handle != window)
        return result;
    // The maximize engine reads the MINMAXINFO only after the whole window
    // procedure chain returns, so rewriting it here overrides whatever Qt's
    // handler proposed - see maximizeInfoToWork. A captioned maximize ignores
    // this struct entirely (user32 places those over the ring regardless,
    // proven with rewritten proposals); it matters for every caption-less
    // maximized layout, including the relayout that follows the transient
    // transition caption being stripped again. ptMaxSize is consumed by a
    // maximize alone, so windowed moves and resizes are unaffected.
    if (message == WM_GETMINMAXINFO && lParam && !self->fullscreen())
        maximizeInfoToWork(window, reinterpret_cast<MINMAXINFO *>(lParam));
    if (message == WM_EXITSIZEMOVE || message == WM_CANCELMODE) {
        self->queueRepair();
    } else if (message == WM_DWMCOMPOSITIONCHANGED || message == WM_STYLECHANGED ||
               message == WM_DPICHANGED || message == WM_DISPLAYCHANGE ||
               message == WM_SHOWWINDOW || message == WM_THEMECHANGED ||
               message == WM_SETTINGCHANGE) {
        self->queueRepair();
    }
    return result;
}
QVariantMap WindowsWindowBackdrop::diagnostics() const
{
    QVariantMap state{{"build", m_build},
                      {"frostEnabled", m_enabled},
                      {"darkMode", m_dark},
                      {"initialized", m_initialized},
                      {"error", m_error}};
    if (!m_window || !m_handle || !IsWindow(m_handle)) {
        state.insert("attached", false);
        return state;
    }
    const auto style = GetWindowLongPtrW(m_handle, GWL_STYLE);
    const auto extended = GetWindowLongPtrW(m_handle, GWL_EXSTYLE);
    DWORD_PTR data = 0;
    state.insert("attached", true);
    state.insert(
        "subclassInstalled",
        GetWindowSubclass(m_handle, windowProcedure, reinterpret_cast<UINT_PTR>(this), &data) != 0);
    state.insert("style", QString::number(quintptr(style), 16));
    state.insert("extendedStyle", QString::number(quintptr(extended), 16));
    state.insert("layered", (extended & WS_EX_LAYERED) != 0);
    state.insert("surfaceType", int(m_window->surfaceType()));
    state.insert("requestedAlphaBufferSize", m_window->requestedFormat().alphaBufferSize());
    state.insert("alphaBufferSize", m_window->format().alphaBufferSize());
    state.insert("windowState", m_window->windowStates().toInt());
    state.insert("fullscreenInputLocked", fullscreen());
    state.insert("windowColor", m_window->property("color").value<QColor>().name(QColor::HexArgb));
    state.insert("devicePixelRatio", m_window->devicePixelRatio());
    RECT bounds{};
    MONITORINFO monitor{sizeof(MONITORINFO)};
    if (GetWindowRect(m_handle, &bounds) &&
        GetMonitorInfoW(MonitorFromWindow(m_handle, MONITOR_DEFAULTTONEAREST), &monitor)) {
        state.insert("nativeBounds", QVariantList{int(bounds.left), int(bounds.top),
                                                  int(bounds.right - bounds.left),
                                                  int(bounds.bottom - bounds.top)});
        state.insert("coversMonitor", EqualRect(&bounds, &monitor.rcMonitor) != FALSE);
    }
    state.insert("remoteSession", GetSystemMetrics(SM_REMOTESESSION) != 0);
    BOOL composing = FALSE;
    state.insert("compositionEnabled", SUCCEEDED(DwmIsCompositionEnabled(&composing))
                                           ? QVariant(bool(composing))
                                           : QVariant());
    int materialType = 0;
    state.insert("systemBackdrop",
                 SUCCEEDED(DwmGetWindowAttribute(m_handle, kAttrSystemBackdrop, &materialType,
                                                 sizeof(materialType)))
                     ? QVariant(materialType)
                     : QVariant());
    UINT borderColor = 0;
    state.insert("frameBorder", SUCCEEDED(DwmGetWindowAttribute(m_handle, kAttrBorderColor,
                                                                &borderColor, sizeof(borderColor)))
                                    ? QVariant(uint(borderColor))
                                    : QVariant());
    state.insert("transparency", m_transparency);
    QVariant accentState, accentColor;
    if (const auto read = compositionReadApi()) {
        AccentPolicy policy{};
        CompositionAttribute attribute{19, &policy, sizeof(policy)};
        if (read(m_handle, &attribute)) {
            accentState = policy.state;
            accentColor = QString::number(policy.color, 16);
        }
    }
    state.insert("accentState", accentState);
    state.insert("accentTint", accentColor);
    return state;
}
void WindowsWindowBackdrop::logState(const char *reason)
{
    const auto state = diagnostics();
    if (state == m_lastDiagnostics)
        return;
    m_lastDiagnostics = state;
    qCInfo(windowLog).noquote() << reason
                                << QJsonDocument::fromVariant(state).toJson(QJsonDocument::Compact);
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
        m_enabled = false;
    }
    // All other messages retain Qt's activation, state and ownership handling.
    return false;
}
} // namespace EchoMusic
