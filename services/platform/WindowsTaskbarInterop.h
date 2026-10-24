#pragma once
// Shared Win32 plumbing for the taskbar adapters: the ITaskbarList3
// acquisition dance and the GDI conversions the thumbbar and the iconic
// preview need. Callers only exist inside Q_OS_WIN blocks, so the whole
// header is guarded and the cpp compiles empty elsewhere.
// <QtGlobal> must come first: Q_OS_WIN is defined by Qt's headers, not by the
// compiler command line, so a guard before any Qt include would see it unset.
#include <QtGlobal>
#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shobjidl.h>
#include <QImage>
namespace EchoMusic
{
/// The members stay `void *` so the adapter headers compile without windows.h;
/// this is the single cast back.
ITaskbarList3 *taskbarListFrom(void *value);
/// CoCreateInstance with the apartment fallback and one HrInit. Returns an
/// initialized, caller-owned instance or null.
ITaskbarList3 *acquireTaskbarList3();
/// 32-bit top-down BGRA DIB section sharing QImage's premultiplied layout.
HBITMAP createTaskbarDib(const QImage &image);
/// The thumbbar takes an HICON; a monochrome mask is required even for a
/// 32-bit icon. The result is caller-owned (DestroyIcon).
HICON createTaskbarIcon(const QImage &image);
} // namespace EchoMusic
#endif
