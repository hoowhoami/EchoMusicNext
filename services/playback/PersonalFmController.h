#pragma once
#include "ApiClient.h"
#include "PlaybackTrack.h"
#include <QElapsedTimer>
#include <QHash>
#include <QPointer>
#include <QSettings>
#include <QTimer>
#include <optional>
namespace EchoMusic
{
class PlaybackService;
class PlaybackQueueModel;
/// Playback-owned dynamic provider. Only adopted audio enters the FM history;
/// the preview buffer never becomes an independently writable player.
class PersonalFmController final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool signedIn READ signedIn NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool previewLoading READ previewLoading NOTIFY changed)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY changed)
    Q_PROPERTY(int mode READ mode NOTIFY changed)
    Q_PROPERTY(int songPool READ songPool NOTIFY changed)
    Q_PROPERTY(QString title READ title NOTIFY changed)
    Q_PROPERTY(QString subtitle READ subtitle NOTIFY changed)
    Q_PROPERTY(QString poolLabel READ poolLabel NOTIFY changed)
    Q_PROPERTY(QVariantMap current READ current NOTIFY changed)
    Q_PROPERTY(QVariantList sideTracks READ sideTracks NOTIFY changed)
    Q_PROPERTY(bool currentPlaying READ currentPlaying NOTIFY changed)
public:
    PersonalFmController(PlaybackService &, ApiClient &, QSettings &);
    ~PersonalFmController() override;
    static QString queueKey()
    {
        return QStringLiteral("fm/personal");
    }
    bool active() const;
    bool signedIn() const;
    bool busy() const;
    bool previewLoading() const
    {
        return m_resetRequest != 0;
    }
    QString errorMessage() const
    {
        return m_error;
    }
    int mode() const
    {
        return m_mode;
    }
    int songPool() const
    {
        return m_pool;
    }
    QString title() const;
    QString subtitle() const;
    QString poolLabel() const;
    QVariantMap current() const;
    QVariantList sideTracks() const;
    bool currentPlaying() const;
    void setPageActive(bool active, bool networkEnabled);
    Q_INVOKABLE void setMode(int mode);
    Q_INVOKABLE void setSongPool(int pool);
    Q_INVOKABLE void play(const QString &id = {});
    Q_INVOKABLE void dislike();
    Q_INVOKABLE void retry();
    void advance(bool natural = false, bool disliked = false, bool preserveFailureChain = false);
    std::optional<PlaybackTrack> nextTrack() const;
    QString transitionKey() const;
    void adoptTransition(const PlaybackTrack &track);
    void favoriteFeedback(const PlaybackTrack &track, bool favorite);
    void shutdown();
    void abandonQueue(bool preserveReports = true);
signals:
    void changed();

private:
    friend class PersonalFmControllerTestAccess;
    friend class PlaybackService;
    struct Pending
    {
        quint64 epoch = 0, session = 0, playbackSequence = 0;
        QString source;
        QPointer<QTimer> deadline;
        bool reset = false, refill = false, preserveQueue = false, resume = false,
             pageBound = false;
        int previousMode = 0, previousPool = 0;
    };
    struct Decision
    {
        PlaybackTrack track;
        QString source, title;
        int index = -1;
    };
    std::optional<Decision> decision() const;
    PlaybackQueueModel *model() const;
    const PlaybackTrack *displayTrack() const;
    static QVariantMap presentation(const PlaybackTrack &track);
    void resetPreview(int mode, int pool, ApiClient::FmAction action, bool preserveQueue);
    quint64 request(ApiClient::FmRequest request, Pending pending);
    ApiClient::FmRequest parameters(ApiClient::FmAction action,
                                    const PlaybackTrack *track = nullptr) const;
    void succeeded(quint64 id, const QByteArray &bytes);
    void failed(quint64 id, const QString &message);
    void cancel(quint64 id);
    void resetSession();
    void refill();
    void reportAdvance(bool disliked, bool natural);
    void startCandidate(const PlaybackTrack &track, bool preserveFailureChain = false);
    void commit(const PlaybackTrack &track);
    void playbackChanged();
    void merge(const PlaybackTracks &tracks);
    void watchMetadata();
    void savePreferences();
    PlaybackService &m_playback;
    ApiClient &m_api;
    QSettings &m_settings;
    QHash<quint64, Pending> m_requests;
    PlaybackTracks m_buffer;
    std::optional<PlaybackTrack> m_pendingTrack;
    quint64 m_epoch = 0, m_resetRequest = 0, m_refillRequest = 0, m_loadSequence = 0;
    quint64 m_waitSequence = 0;
    QString m_waitSource, m_error;
    QElapsedTimer m_refillAfter;
    int m_mode = 0, m_pool = 0;
    bool m_pageActive = false, m_networkEnabled = false, m_closed = false;
    bool m_waiting = false, m_playAfterReset = false, m_starting = false, m_committing = false;
    bool m_committed = false, m_reported = false, m_resetPending = true;
};
} // namespace EchoMusic
