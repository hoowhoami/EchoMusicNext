#include "PlaybackClock.h"
#include <QDateTime>
#include <cmath>
namespace EchoMusic
{
double PlaybackClock::position() const
{
    double value = m_state.value("positionMs").toDouble();
    if (m_elapsed.isValid() && m_state.value("isAdvancing").toBool())
        value += qMin<qint64>(1000, m_elapsed.elapsed()) * m_state.value("playbackRate").toDouble();
    const auto duration = m_state.value("durationMs").toDouble();
    return qMax(0., duration > 0 ? qMin(value, duration) : value);
}
bool PlaybackClock::observe(QVariantMap state, bool sample, const QString &explicitReason)
{
    const bool identity = state.value("trackId") != m_state.value("trackId") ||
                          state.value("trackSeq") != m_state.value("trackSeq") ||
                          state.value("sourceRevision") != m_state.value("sourceRevision");
    QString reason = explicitReason;
    if (identity)
        reason = "load";
    bool changed = identity || !reason.isEmpty() || sample;
    for (const auto &key :
         {"isPlaying", "isAdvancing", "playbackRate", "durationMs", "displayState", "busyReason"})
        changed = changed || state.value(key) != m_state.value(key);
    if (!changed)
        return false;
    if (reason.isEmpty()) {
        if (state.value("displayState") != m_state.value("displayState"))
            reason = state.value("displayState").toString();
        else if (state.value("playbackRate") != m_state.value("playbackRate"))
            reason = "rate";
        else
            reason = "tick";
    }
    // State changes re-anchor the projected position, not the previous raw sample.
    if (!sample && !identity)
        state["positionMs"] = position();
    if (identity || explicitReason == "seek" || explicitReason == "reset")
        ++m_generation;
    state["seekTimestamp"] = explicitReason == "seek" ? QDateTime::currentMSecsSinceEpoch()
                             : identity ? 0
                                        : m_state.value("seekTimestamp", 0).toLongLong();
    state["generation"] = QVariant::fromValue(m_generation);
    state["sampledAt"] = QDateTime::currentMSecsSinceEpoch();
    state["reason"] = reason;
    m_state = std::move(state);
    m_elapsed.restart();
    return true;
}
} // namespace EchoMusic
