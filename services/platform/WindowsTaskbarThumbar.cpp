#include "WindowsTaskbarThumbar.h"
#include "WindowsTaskbarInterop.h"
#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <objbase.h>
#include <shobjidl.h>
#endif
namespace EchoMusic
{
#ifdef Q_OS_WIN
namespace
{
constexpr UINT_PTR ThumbarSubclassId = 0x45434842;
/// Menu command ids are allocated by the toolkit from 0x8000 upward; a private
/// base keeps the thumbbar from colliding with a real command.
constexpr int ThumbarIdBase = 0xec00;
LRESULT CALLBACK thumbarSubclass(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam, UINT_PTR,
                                 DWORD_PTR ref)
{
    auto *self = reinterpret_cast<WindowsTaskbarThumbar *>(ref);
    if (message == WM_NCDESTROY) {
        if (self)
            self->detach();
        RemoveWindowSubclass(hwnd, thumbarSubclass, ThumbarSubclassId);
    } else if (self && message == self->taskbarCreatedMessage()) {
        // Explorer recreated the button (the window was hidden to the tray, or
        // the shell restarted); the old toolbar went with it.
        self->reregister();
        return 0;
    } else if (message == WM_COMMAND && lparam == 0) {
        const int id = int(LOWORD(wparam));
        if (id >= ThumbarIdBase && id < ThumbarIdBase + 32) {
            if (self)
                self->notifyClicked(id);
            return 0;
        }
    }
    return DefSubclassProc(hwnd, message, wparam, lparam);
}
} // namespace
WindowsTaskbarThumbar::WindowsTaskbarThumbar(QObject *parent) : QObject(parent)
{
    // Registered before the window exists: Explorer sends this whenever it
    // creates (or recreates) the taskbar button for a window.
    m_taskbarCreatedMessage = RegisterWindowMessageW(L"TaskbarButtonCreated");
}
WindowsTaskbarThumbar::~WindowsTaskbarThumbar()
{
    detach();
    if (auto *list = taskbarListFrom(m_taskbar))
        list->Release();
    m_taskbar = nullptr;
}
bool WindowsTaskbarThumbar::available()
{
    if (m_taskbar)
        return true;
    if (m_attempted)
        return false;
    m_attempted = true;
    m_taskbar = acquireTaskbarList3();
    return m_taskbar != nullptr;
}
void WindowsTaskbarThumbar::releaseIcons()
{
    for (const auto icon : m_icons)
        DestroyIcon(reinterpret_cast<HICON>(icon));
    m_icons.clear();
}
void WindowsTaskbarThumbar::detach()
{
    releaseIcons();
    const auto handle = reinterpret_cast<HWND>(m_hwnd);
    if (m_subclassed && handle && IsWindow(handle))
        RemoveWindowSubclass(handle, thumbarSubclass, ThumbarSubclassId);
    m_subclassed = false;
    m_hwnd = 0;
    m_buttonCount = 0;
}
void WindowsTaskbarThumbar::apply(quintptr hwnd, const QList<TaskbarThumbarButton> &buttons)
{
    if (hwnd == 0 || buttons.isEmpty() || !available()) {
        detach();
        return;
    }
    const auto handle = reinterpret_cast<HWND>(hwnd);
    if (!IsWindow(handle))
        return;
    if (m_hwnd != hwnd) {
        if (m_subclassed)
            detach();
        if (!SetWindowSubclass(handle, thumbarSubclass, ThumbarSubclassId,
                               reinterpret_cast<DWORD_PTR>(this)))
            return;
        m_subclassed = true;
        m_hwnd = hwnd;
    }
    m_buttons = buttons;
    push();
}
void WindowsTaskbarThumbar::push()
{
    if (m_hwnd == 0 || !m_taskbar)
        return;
    const auto handle = reinterpret_cast<HWND>(m_hwnd);
    if (!IsWindow(handle))
        return;
    releaseIcons();
    QList<THUMBBUTTON> native;
    native.reserve(m_buttons.size());
    for (const auto &button : m_buttons) {
        THUMBBUTTON entry{};
        entry.dwMask = THB_ICON | THB_TOOLTIP | THB_FLAGS;
        entry.iId = UINT(button.id);
        entry.dwFlags = button.enabled ? THBF_ENABLED : THBF_DISABLED;
        const auto icon = quintptr(createTaskbarIcon(button.image));
        if (icon)
            m_icons.append(icon);
        entry.hIcon = reinterpret_cast<HICON>(icon);
        const auto tip = button.tooltip.toStdWString();
        wcsncpy_s(entry.szTip, tip.c_str(), _TRUNCATE);
        native.append(entry);
    }
    auto *list = taskbarListFrom(m_taskbar);
    // Explorer only accepts a new button set through AddButtons; an update in
    // place needs the same count.
    if (native.size() != m_buttonCount) {
        if (SUCCEEDED(list->ThumbBarAddButtons(handle, UINT(native.size()), native.data())))
            m_buttonCount = int(native.size());
    } else {
        list->ThumbBarUpdateButtons(handle, UINT(native.size()), native.data());
    }
}
void WindowsTaskbarThumbar::reregister()
{
    // The toolbar died with the previous button, so this is a first add again.
    m_buttonCount = 0;
    push();
}
void WindowsTaskbarThumbar::notifyClicked(int id)
{
    // Deferred: the click is delivered inside the message dispatch, and the
    // receivers move the playback queue.
    QMetaObject::invokeMethod(this, [this, id] { emit buttonClicked(id); }, Qt::QueuedConnection);
}
#else
WindowsTaskbarThumbar::WindowsTaskbarThumbar(QObject *parent) : QObject(parent) {}
WindowsTaskbarThumbar::~WindowsTaskbarThumbar() = default;
bool WindowsTaskbarThumbar::available()
{
    return false;
}
void WindowsTaskbarThumbar::apply(quintptr hwnd, const QList<TaskbarThumbarButton> &buttons)
{
    Q_UNUSED(hwnd)
    Q_UNUSED(buttons)
}
void WindowsTaskbarThumbar::detach() {}
void WindowsTaskbarThumbar::releaseIcons() {}
void WindowsTaskbarThumbar::push() {}
void WindowsTaskbarThumbar::reregister() {}
void WindowsTaskbarThumbar::notifyClicked(int id)
{
    Q_UNUSED(id)
}
#endif
} // namespace EchoMusic
