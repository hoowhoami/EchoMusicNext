#pragma once
#include <QJsonObject>
#include <QString>
namespace EchoMusic
{
/// GUI-thread projection of native progress diagnostics. The service filters
/// track/source context before delivery; this value owns no engine or timer.
class PlaybackProgressState
{
public:
    enum Reason
    {
        Ready,
        Seeking,
        Buffering
    };
    void reset();
    void observe(const QJsonObject &event);
    Reason reason(bool available, bool playing) const;
    /// Native EOF reports draining before state-change/playback-end. The
    /// existing context-filtered snapshot bridges that automatic handoff.
    bool draining() const
    {
        return m_core == "draining";
    }

private:
    QString m_core;
    quint64 m_generation = 0, m_coreRevision = 0, m_aoRevision = 0, m_progressRevision = 0,
            m_seekRevision = 0;
    double m_position = -1, m_buffering = 100;
    bool m_aoPaused = false, m_seek = false;
};
} // namespace EchoMusic
