#pragma once
#include <QObject>
#include <QDateTime>
#include <functional>
namespace EchoMusic
{
/// Wall-clock deadline survives sleep/minimization. The playback service alone
/// supplies track identity and actual transport state; no QML countdown timer.
class SleepTimer final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool active READ active NOTIFY changed)
    Q_PROPERTY(bool waiting READ waiting NOTIFY changed)
    Q_PROPERTY(bool executing READ executing NOTIFY changed)
    Q_PROPERTY(int durationMinutes READ durationMinutes NOTIFY changed)
    Q_PROPERTY(int remainingSeconds READ remainingSeconds NOTIFY changed)
    Q_PROPERTY(QString action READ action WRITE setAction NOTIFY changed)
    Q_PROPERTY(bool finishTrack READ finishTrack WRITE setFinishTrack NOTIFY changed)
    Q_PROPERTY(QString endTime READ endTime NOTIFY changed)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY changed)
public:
    explicit SleepTimer(
        QObject *parent,
        std::function<qint64()> clock = [] { return QDateTime::currentMSecsSinceEpoch(); })
        : QObject(parent), m_clock(std::move(clock))
    {
    }
    bool active() const
    {
        return m_deadline >= 0;
    }
    bool waiting() const
    {
        return !m_waiting.isEmpty();
    }
    bool executing() const
    {
        return m_executing;
    }
    int durationMinutes() const
    {
        return m_minutes;
    }
    int remainingSeconds() const
    {
        return m_remaining;
    }
    QString action() const
    {
        return m_action;
    }
    bool finishTrack() const
    {
        return m_finishTrack;
    }
    QString endTime() const
    {
        return active() ? QDateTime::fromMSecsSinceEpoch(m_deadline).toString("HH:mm") : QString();
    }
    QString errorMessage() const
    {
        return m_error;
    }
    Q_INVOKABLE bool start(qint64 minutes, const QString &action);
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void setAction(const QString &action);
    void setFinishTrack(bool finish);
    bool tick(const QString &track, bool playing);
    bool trackEnded();
    void executionFinished(const QString &error = {});
signals:
    void changed();
    void executionFailed(const QString &message);
    void actionRequested(const QString &action);

private:
    bool complete();
    std::function<qint64()> m_clock;
    qint64 m_deadline = -1;
    int m_minutes = 30, m_remaining = 0;
    QString m_action = "pause", m_waiting, m_error;
    bool m_finishTrack = false, m_executing = false;
};
} // namespace EchoMusic
