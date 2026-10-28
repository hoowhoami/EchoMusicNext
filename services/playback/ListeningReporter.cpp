#include "ListeningReporter.h"
#include <QJsonDocument>
#include <cmath>
#include <utility>
namespace EchoMusic
{
ListeningReporter::ListeningReporter(ApiClient &api, QObject *parent) : QObject(parent), m_api(api)
{
    m_clock.start();
    m_revision = api.sessionRevision();
    m_spacing.setSingleShot(true);
    m_deadline.setSingleShot(true);
    connect(&m_spacing, &QTimer::timeout, this, &ListeningReporter::sendNext);
    connect(&m_deadline, &QTimer::timeout, this, [this] {
        reset();
        finishDrain();
    });
    connect(&api, &ApiClient::succeeded, this, &ListeningReporter::succeed);
    connect(&api, &ApiClient::failed, this,
            [this](quint64 id, int, const QByteArray &bytes) { fail(id, bytes); });
    connect(&api, &ApiClient::sessionChanged, this, &ListeningReporter::reset);
    connect(&api, &ApiClient::closed, this, &ListeningReporter::reset);
}
ListeningReporter::~ListeningReporter()
{
    reset();
}
void ListeningReporter::reset()
{
    m_spacing.stop();
    for (auto *id : {&m_eventRequest, &m_gradeRequest})
        if (auto request = std::exchange(*id, 0))
            m_api.cancel(request);
    m_events.clear();
    m_track.clear();
    m_song = 0;
    m_duration = m_pendingMs = 0;
    m_ledger = 0;
    m_sampleTime = -1;
    m_gradeWriting = false;
    m_gradeRetried = false;
    m_revision = m_api.sessionRevision();
    m_lastGrade = m_clock.elapsed();
    if (m_closing)
        finishDrain();
}
void ListeningReporter::resetPosition()
{
    m_sampleTime = -1;
}
void ListeningReporter::tick(const QString &track, quint64 songId, bool eligible, bool advancing,
                             qint64 positionMs, double speed)
{
    tickAt(track, songId, eligible, advancing, positionMs, speed, m_clock.elapsed());
}
void ListeningReporter::tickAt(const QString &track, quint64 songId, bool eligible, bool advancing,
                               qint64 positionMs, double speed, qint64 now)
{
    if (m_closing)
        return;
    if (!eligible || !songId || !m_api.currentAccountUserId() || track.isEmpty()) {
        flush();
        return;
    }
    if (track != m_track || songId != m_song)
        flush();
    if (!advancing) {
        resetPosition();
        return;
    }
    if (m_track.isEmpty()) {
        m_track = track;
        m_song = songId;
        m_duration = 0;
        m_events.enqueue({ApiClient::ListeningEvent::Start, songId, 0, false});
        sendNext();
    }
    if (m_sampleTime >= 0) {
        const qint64 elapsed = now - m_sampleTime, progress = positionMs - m_position;
        const double rate = std::isfinite(speed) && speed > 0 ? speed : 1;
        if (elapsed > 0 && elapsed <= 5000 && progress > 0 && progress <= elapsed * rate + 1000) {
            const qint64 listened = qint64(std::min(double(elapsed), progress / rate));
            m_duration += listened;
            m_pendingMs += listened;
        }
    }
    m_sampleTime = now;
    m_position = positionMs;
    // Do not advance the window when fewer than 60 listened seconds are
    // available. Real engine ticks always leave the first sample short of a
    // nominal minute; advancing here delayed that minute to the next window.
    if (!m_gradeRequest && m_pendingMs >= 60000 && now - m_lastGrade >= 60000) {
        m_lastGrade = now;
        m_gradeWriting = false;
        m_gradeRetried = false;
        m_gradeRequest = m_api.userGradeInfo({}, false);
    }
}
void ListeningReporter::flush(bool completed)
{
    if (!m_track.isEmpty())
        m_events.enqueue({ApiClient::ListeningEvent::End, m_song, m_duration, completed});
    m_track.clear();
    m_song = 0;
    m_duration = 0;
    resetPosition();
    sendNext();
}
void ListeningReporter::sendNext()
{
    if (m_eventRequest || m_revision != m_api.sessionRevision())
        return;
    if (m_events.isEmpty()) {
        finishDrain();
        return;
    }
    const qint64 wait = 3000 - (m_clock.elapsed() - m_lastEvent);
    if (wait > 0) {
        m_spacing.start(int(wait));
        return;
    }
    const auto event = m_events.dequeue();
    m_lastEvent = m_clock.elapsed();
    m_eventRequest =
        m_api.reportListeningEvent(event.kind, event.song, event.duration, event.completed);
    // Submission failure is terminal for this event too. It is never persisted
    // or replayed on resume/relogin, even if the preceding start failed.
    if (!m_eventRequest) {
        QMetaObject::invokeMethod(this, &ListeningReporter::sendNext, Qt::QueuedConnection);
    }
}
void ListeningReporter::succeed(quint64 id, const QByteArray &bytes)
{
    if (m_revision != m_api.sessionRevision())
        return;
    if (id && id == m_eventRequest) {
        m_eventRequest = 0;
        sendNext();
        return;
    }
    if (!id || id != m_gradeRequest)
        return;
    m_gradeRequest = 0;
    if (!m_gradeWriting) {
        const auto value = QJsonDocument::fromJson(bytes).object().value("listening_seconds");
        if (value.isDouble() && value.toInteger(-1) >= 0) {
            m_ledger = std::max(m_ledger, quint64(value.toInteger()));
            m_gradeWriting = true;
            m_gradeRequest = m_api.reportListeningProgress(m_ledger, 60);
        }
    } else {
        // Server settlement can lag behind. Never lower the confirmed local
        // baseline to a stale GET response, and never count CSCC end a second time.
        m_ledger += 60;
        m_pendingMs = std::max<qint64>(0, m_pendingMs - 60000);
        m_gradeWriting = false;
    }
    finishDrain();
}
void ListeningReporter::fail(quint64 id, const QByteArray &bytes)
{
    if (id && id == m_eventRequest) {
        m_eventRequest = 0;
        qWarning("Listening event failed; no automatic replay");
        sendNext();
    } else if (id && id == m_gradeRequest) {
        m_gradeRequest = 0;
        const auto kind = QJsonDocument::fromJson(bytes).object().value("kind").toString();
        // This read-only baseline permits one bounded repeat after a transport
        // failure. The HTTP client owns connection recovery; a repeat does not
        // imply a fresh socket. Never replay a write after an uncertain result.
        if (!m_gradeWriting && !m_gradeRetried && kind == "transport") {
            m_gradeRetried = true;
            m_gradeRequest = m_api.userGradeInfo({}, false);
            qWarning("Listening grade baseline request failed; retrying once");
            return;
        }
        // Only a definite write rejection can retain this minute for the next
        // periodic window. Timeout/decode/HTTP may already have committed.
        if (m_gradeWriting && kind != "business" && kind != "authentication" && kind != "challenge")
            m_pendingMs = std::max<qint64>(0, m_pendingMs - 60000);
        const auto phase = m_gradeWriting ? "write" : "baseline";
        m_gradeWriting = false;
        qWarning("Listening grade request failed phase=%s", phase);
        finishDrain();
    }
}
void ListeningReporter::shutdown()
{
    if (m_closing)
        return;
    flush();
    m_closing = true;
    // Closing must stay bounded even when the upstream/default timeout is off.
    // An interrupted/uncertain request is discarded, never replayed next launch.
    m_deadline.start(10000);
    finishDrain();
}
void ListeningReporter::finishDrain()
{
    if (!m_closing || m_drained || m_eventRequest || m_gradeRequest || !m_events.isEmpty())
        return;
    m_deadline.stop();
    m_drained = true;
    emit drained();
}
} // namespace EchoMusic
