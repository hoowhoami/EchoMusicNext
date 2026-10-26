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
    const QPoint global = QCursor::pos();
    // A position outside the window would read as the pointer leaving it.
    if (!window->geometry().contains(global))
        return false;
    // The backend translates this like any other pointer move, so the window
    // learns the position it is actually pointing at while the physical cursor
    // stays where it is. A move from the real pointer is not required: once no
    // stale grab holds the pointer, a delivered move updates hover.
    const QPoint local = window->mapFromGlobal(global);
    return PostMessageW(reinterpret_cast<HWND>(window->winId()), WM_MOUSEMOVE, 0,
                        MAKELPARAM(local.x(), local.y()));
#else
    Q_UNUSED(window);
    return false;
#endif
}
} // namespace EchoMusic
