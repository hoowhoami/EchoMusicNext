#pragma once
#include <QtGlobal>
namespace EchoMusic
{
/// Taskbar button progress state. The values mirror EchoMusic
/// shared/taskbarProgress semantics: a track owns the bar, an unknown duration
/// is indeterminate, playback advances it and a pause keeps a visible slice.
enum class TaskbarProgressMode
{
    None,
    Normal,
    Paused,
    Indeterminate
};
struct TaskbarProgressValue
{
    TaskbarProgressMode mode = TaskbarProgressMode::None;
    /// Completed fraction in [0, 1]. Only meaningful for Normal and Paused.
    double ratio = 0;
};
/// Pure mapping from playback state to the taskbar progress mode. Tested without
/// a desktop; the platform layer only forwards the result.
///
/// Windows renders an all-zero paused bar as "no progress", so a paused value
/// keeps a visible minimum instead of the raw fraction (EchoMusic
/// PAUSED_MIN_VALUE).
TaskbarProgressValue resolveTaskbarProgress(bool hasTrack, bool playing, qint64 positionMs,
                                            qint64 durationMs);
/// Rate-limits the high-frequency position stream. A mode change or a visible
/// ratio change applies immediately; otherwise the caller is throttled, so a
/// stationary bar does not call the shell on every tick.
class TaskbarProgressGate
{
public:
    /// Returns true when the value must be pushed to the platform.
    bool accept(const TaskbarProgressValue &value, qint64 nowMs);
    void reset();
    TaskbarProgressValue last() const
    {
        return m_last;
    }

private:
    /// Position updates arrive far faster than the bar can render.
    static constexpr qint64 ThrottleMs = 200;
    /// A fraction change below this is invisible on a taskbar button.
    static constexpr double RatioEpsilon = 0.001;
    TaskbarProgressValue m_last;
    qint64 m_appliedMs = 0;
    bool m_applied = false;
};
} // namespace EchoMusic
