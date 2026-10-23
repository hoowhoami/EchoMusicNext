#include "TaskbarProgressState.h"
#include <cmath>
namespace EchoMusic
{
namespace
{
/// A paused bar at exactly zero is indistinguishable from a removed one.
constexpr double PausedMinValue = 0.01;
double clampRatio(double value)
{
    return std::min(1.0, std::max(0.0, value));
}
} // namespace
TaskbarProgressValue resolveTaskbarProgress(bool hasTrack, bool playing, qint64 positionMs,
                                            qint64 durationMs)
{
    TaskbarProgressValue value;
    if (!hasTrack) {
        value.mode = TaskbarProgressMode::None;
        return value;
    }
    if (durationMs <= 0) {
        // A stream or a file whose duration is not known yet: show activity
        // instead of a bar that would sit at zero.
        value.mode = TaskbarProgressMode::Indeterminate;
        value.ratio = 1;
        return value;
    }
    const double ratio = clampRatio(static_cast<double>(positionMs) / static_cast<double>(durationMs));
    if (playing) {
        value.mode = TaskbarProgressMode::Normal;
        value.ratio = ratio;
        return value;
    }
    value.mode = TaskbarProgressMode::Paused;
    value.ratio = std::max(PausedMinValue, ratio);
    return value;
}
bool TaskbarProgressGate::accept(const TaskbarProgressValue &value, qint64 nowMs)
{
    if (!m_applied) {
        m_applied = true;
        m_last = value;
        m_appliedMs = nowMs;
        return true;
    }
    const bool modeChanged = value.mode != m_last.mode;
    const bool ratioChanged = std::abs(value.ratio - m_last.ratio) >= RatioEpsilon;
    if (!modeChanged && !ratioChanged && nowMs - m_appliedMs < ThrottleMs)
        return false;
    m_last = value;
    m_appliedMs = nowMs;
    return true;
}
void TaskbarProgressGate::reset()
{
    m_last = {};
    m_appliedMs = 0;
    m_applied = false;
}
} // namespace EchoMusic
