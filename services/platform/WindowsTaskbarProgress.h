#pragma once
#include <QtGlobal>
#include "TaskbarProgressState.h"
namespace EchoMusic
{
/// Windows taskbar button progress (ITaskbarList3). Off Windows every call is a
/// no-op, so the shell controller carries no platform branches.
///
/// The COM object is created lazily: building it at startup would fail on
/// systems where Explorer is not ready, and the feature must be recoverable
/// without a restart.
class WindowsTaskbarProgress
{
public:
    WindowsTaskbarProgress() = default;
    ~WindowsTaskbarProgress();
    WindowsTaskbarProgress(const WindowsTaskbarProgress &) = delete;
    WindowsTaskbarProgress &operator=(const WindowsTaskbarProgress &) = delete;
    /// False when the shell COM object cannot be created; callers hide the
    /// feature instead of reporting an error the user cannot act on.
    bool available();
    void apply(quintptr hwnd, const TaskbarProgressValue &value);
    /// Removes the bar without touching the rest of the button state.
    void clear(quintptr hwnd);

private:
    /// ITaskbarList3*, owned here. Opaque so no Windows header reaches the
    /// shell module.
    void *m_taskbar = nullptr;
    bool m_attempted = false;
};
} // namespace EchoMusic
