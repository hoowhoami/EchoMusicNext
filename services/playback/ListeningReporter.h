#pragma once
#include "ApiClient.h"
#include <QElapsedTimer>
#include <QQueue>
#include <QTimer>
namespace EchoMusic
{
/// Non-persistent listening ledger. Audio telemetry gates monotonic wall time;
/// one ordered CSCC queue and an independent minute grade ledger own all writes.
class ListeningReporter final : public QObject
{
    Q_OBJECT
public:
    explicit ListeningReporter(ApiClient &api, QObject *parent = nullptr);
    ~ListeningReporter() override;
    void tick(const QString &track, quint64 songId, bool eligible, bool advancing,
              qint64 positionMs, double speed);
    void resetPosition();
    void flush(bool completed = false);
    void shutdown();
signals:
    void drained();

private:
    friend class ListeningReporterTestAccess;
    struct Event
    {
        ApiClient::ListeningEvent kind;
        quint64 song;
        qint64 duration;
        bool completed;
    };
    void tickAt(const QString &track, quint64 songId, bool eligible, bool advancing,
                qint64 positionMs, double speed, qint64 now);
    void sendNext();
    void succeed(quint64 id, const QByteArray &bytes);
    void fail(quint64 id, const QByteArray &bytes);
    void reset();
    void finishDrain();
    ApiClient &m_api;
    QElapsedTimer m_clock;
    QTimer m_spacing, m_deadline;
    QQueue<Event> m_events;
    QString m_track;
    quint64 m_song = 0, m_revision = 0, m_eventRequest = 0, m_gradeRequest = 0;
    qint64 m_duration = 0, m_pendingMs = 0, m_sampleTime = -1, m_position = 0;
    qint64 m_lastEvent = -3000, m_lastGrade = 0;
    quint64 m_ledger = 0;
    bool m_gradeWriting = false, m_gradeRetried = false, m_closing = false, m_drained = false;
};
} // namespace EchoMusic
