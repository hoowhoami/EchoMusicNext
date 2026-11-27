#pragma once
#include <QObject>
#include <QElapsedTimer>
#include <QTimer>
#include <QJsonObject>
namespace EchoMusic
{
class PlaybackService;
/// Resume/stall recovery belongs to the owning playback session. Cancellation
/// epochs invalidate deadlines on pause, source change, account change and quit.
class PlaybackRecoveryController final : public QObject
{
    Q_OBJECT
public:
    explicit PlaybackRecoveryController(PlaybackService &playback);
    void cancel();
    void resumeRequested();
    void liveInputEnded();
    bool handleEvent(const QJsonObject &event);
    bool acceptsPosition(qint64 position);

private:
    void recover(bool stall);
    PlaybackService &m_playback;
    QTimer m_resumeTimer, m_recoveryTimer;
    quint64 m_sequence = 0, m_revision = 0;
    QString m_attemptTrack;
    int m_attempts = 0;
    qint64 m_target = -1;
    bool m_resumeWaitingForOutput = false;
};
} // namespace EchoMusic
