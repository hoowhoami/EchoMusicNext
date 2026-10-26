#include "PointerHoverRefresh.h"
#include <QCursor>
#include <QWindow>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif
namespace EchoMusic
{
bool refreshPointerHover(QWindow *window)
{
#ifdef Q_OS_WIN
    if (!window || !window->handle() || !window->isVisible())
        return false;
    const auto handle = reinterpret_cast<HWND>(window->winId());
    POINT local{};
    RECT client{};
    // WM_MOUSEMOVE uses native client pixels, not QWindow/QCursor logical
    // coordinates. User32 also handles mixed-DPI and negative-screen origins.
    if (!GetCursorPos(&local) || !ScreenToClient(handle, &local) ||
        !GetClientRect(handle, &client) || !PtInRect(&client, local))
        return false;
    // The backend translates this like any other pointer move, so the window
    // learns the position it is actually pointing at while the physical cursor
    // stays where it is. A move from the real pointer is not required: once no
    // stale grab holds the pointer, a delivered move updates hover.
    return PostMessageW(handle, WM_MOUSEMOVE, 0, MAKELPARAM(local.x, local.y));
#else
    Q_UNUSED(window);
    return false;
#endif
}
} // namespace EchoMusic
