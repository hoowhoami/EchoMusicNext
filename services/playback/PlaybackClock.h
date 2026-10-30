#pragma once
#include <QElapsedTimer>
#include <QVariantMap>
namespace EchoMusic
{
/// Shared, timer-free timeline anchor. Observations belong to PlaybackService;
/// consumers never turn a read into a fresh measurement or advance a stalled source.
class PlaybackClock
{
public:
    bool observe(QVariantMap state, bool positionSample, const QString &reason = {});
    QVariantMap snapshot() const
    {
        return m_state;
    }
    double position() const;

private:
    QVariantMap m_state;
    QElapsedTimer m_elapsed;
    quint64 m_generation = 0;
};
} // namespace EchoMusic
