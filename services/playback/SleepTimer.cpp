#include "SleepTimer.h"
#include <limits>
namespace EchoMusic
{
bool SleepTimer::start(qint64 minutes, const QString &action)
{
    if (m_executing || minutes < 1 || minutes > 59999 ||
        !QStringList{"pause", "quit", "shutdown"}.contains(action))
        return false;
    const auto now = m_clock();
    const auto duration = minutes * qint64(60000);
    if (now < 0 || now > std::numeric_limits<qint64>::max() - duration)
        return false;
    m_action = action;
    m_error.clear();
    m_minutes = int(minutes);
    m_remaining = int(minutes * 60);
    m_waiting.clear();
    m_deadline = now + duration;
    emit changed();
    return true;
}
void SleepTimer::cancel()
{
    m_deadline = -1;
    m_remaining = 0;
    m_waiting.clear();
    emit changed();
}
void SleepTimer::setAction(const QString &action)
{
    if (!m_executing && QStringList{"pause", "quit", "shutdown"}.contains(action)) {
        m_action = action;
        emit changed();
    }
}
void SleepTimer::setFinishTrack(bool finish)
{
    if (!m_executing) {
        m_finishTrack = finish;
        emit changed();
    }
}
bool SleepTimer::complete()
{
    cancel();
    m_executing = true;
    emit changed();
    emit actionRequested(m_action);
    return true;
}
bool SleepTimer::tick(const QString &track, bool playing)
{
    if (!active())
        return false;
    const auto now = m_clock();
    const int remaining = int(qMax<qint64>(0, (m_deadline - now + 999) / 1000));
    if (remaining != m_remaining) {
        m_remaining = remaining;
        emit changed();
    }
    if (now < m_deadline)
        return false;
    if (!m_finishTrack || !playing || track.isEmpty() || (waiting() && m_waiting != track))
        return complete();
    if (m_waiting != track) {
        m_waiting = track;
        emit changed();
    }
    return false;
}
bool SleepTimer::trackEnded()
{
    return active() && m_clock() >= m_deadline ? complete() : false;
}
void SleepTimer::executionFinished(const QString &error)
{
    m_executing = false;
    m_error = error;
    emit changed();
    if (!error.isEmpty())
        emit executionFailed(error);
}
} // namespace EchoMusic
