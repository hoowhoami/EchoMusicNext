#include "WindowsTaskbarProgress.h"
#include "WindowsTaskbarInterop.h"
#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>
#include <shobjidl.h>
#endif
namespace EchoMusic
{
#ifdef Q_OS_WIN
namespace
{
TBPFLAG flagFor(TaskbarProgressMode mode)
{
    switch (mode) {
    case TaskbarProgressMode::Normal:
        return TBPF_NORMAL;
    case TaskbarProgressMode::Paused:
        return TBPF_PAUSED;
    case TaskbarProgressMode::Indeterminate:
        return TBPF_INDETERMINATE;
    case TaskbarProgressMode::None:
        break;
    }
    return TBPF_NOPROGRESS;
}
/// The taskbar renders a fraction, not a time; a fixed denominator keeps the
/// value exact enough for a 1-pixel-wide bar without sending 64-bit counts.
constexpr ULONGLONG ProgressScale = 1000;
} // namespace
WindowsTaskbarProgress::~WindowsTaskbarProgress()
{
    if (auto *list = taskbarListFrom(m_taskbar))
        list->Release();
    m_taskbar = nullptr;
}
bool WindowsTaskbarProgress::available()
{
    if (m_taskbar)
        return true;
    if (m_attempted)
        return false;
    m_attempted = true;
    m_taskbar = acquireTaskbarList3();
    return m_taskbar != nullptr;
}
void WindowsTaskbarProgress::apply(quintptr hwnd, const TaskbarProgressValue &value)
{
    if (!available() || hwnd == 0)
        return;
    auto *list = taskbarListFrom(m_taskbar);
    const HWND handle = reinterpret_cast<HWND>(hwnd);
    // State first: a value written while the bar is still in its previous state
    // is dropped by Explorer.
    list->SetProgressState(handle, flagFor(value.mode));
    if (value.mode == TaskbarProgressMode::Normal || value.mode == TaskbarProgressMode::Paused) {
        const auto completed = static_cast<ULONGLONG>(value.ratio * double(ProgressScale) + 0.5);
        list->SetProgressValue(handle, completed, ProgressScale);
    }
}
void WindowsTaskbarProgress::clear(quintptr hwnd)
{
    if (!m_taskbar || hwnd == 0)
        return;
    taskbarListFrom(m_taskbar)->SetProgressState(reinterpret_cast<HWND>(hwnd), TBPF_NOPROGRESS);
}
#else
WindowsTaskbarProgress::~WindowsTaskbarProgress() = default;
bool WindowsTaskbarProgress::available()
{
    return false;
}
void WindowsTaskbarProgress::apply(quintptr hwnd, const TaskbarProgressValue &value)
{
    Q_UNUSED(hwnd)
    Q_UNUSED(value)
}
void WindowsTaskbarProgress::clear(quintptr hwnd)
{
    Q_UNUSED(hwnd)
}
#endif
} // namespace EchoMusic
