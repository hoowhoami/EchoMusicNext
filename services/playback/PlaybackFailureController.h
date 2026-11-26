#pragma once
#include <QObject>
#include <QTimer>

namespace EchoMusic
{
class PlaybackService;
/// Host-owned delayed error skipping. The timer is tied to track, account and
/// request epochs; successful playback or explicit user intent ends the chain.
class PlaybackFailureController final : public QObject
{
    Q_OBJECT
public:
    enum class Kind
    {
        Source,
        Output
    };
    explicit PlaybackFailureController(PlaybackService &playback);
    void failed(Kind kind = Kind::Source);
    /// A device failure can retry the AO of the same loaded source. Source or
    /// stale-session failures still follow the normal URL/decoder reload path.
    bool resumeOutput();
    bool awaitingOutputRetry() const
    {
        return m_kind == Kind::Output;
    }
    void sourceOperation(const QString &title);
    void sourceAdopted();
    void cancel(bool resetAttempts = false);
    void started(bool preserveFailureChain);
    void succeeded();

private:
    PlaybackService &m_playback;
    QTimer m_timer;
    quint64 m_sequence = 0, m_revision = 0;
    QString m_trackKey;
    int m_attempts = 0;
    QString m_noticeKey, m_operationTitle;
    quint64 m_noticeSequence = 0, m_noticeSession = 0, m_noticeGeneration = 0;
    quint64 m_failureSourceRevision = 0;
    Kind m_kind = Kind::Source;
};
} // namespace EchoMusic
