#pragma once
#include "PlaybackSourceRequest.h"
#include "SettingsStore.h"
#include <QTimer>
#include <QSet>

namespace EchoMusic
{
class PlaybackService;
/// Qt owns the next-track decision and session identity. The native engine owns
/// readers, analysis and the audible transition boundary; a preparation result
/// never changes the queue's current entry before that boundary.
class TrackTransitionController final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString mode READ mode WRITE setMode NOTIFY changed)
    Q_PROPERTY(int fadeSeconds READ fadeSeconds WRITE setFadeSeconds NOTIFY changed)
    Q_PROPERTY(bool matchTempo READ matchTempo WRITE setMatchTempo NOTIFY changed)
public:
    bool containsSource(const QUrl &url) const;
    TrackTransitionController(PlaybackService &playback, ApiClient &api,
                              EchoMusic::SettingsStore &settings);
    QString mode() const
    {
        return m_mode;
    }
    int fadeSeconds() const
    {
        return m_fadeSeconds;
    }
    bool matchTempo() const
    {
        return m_matchTempo;
    }
    void setMode(const QString &mode);
    void setFadeSeconds(int seconds);
    void setMatchTempo(bool enabled);
    void initialize();
    void tick();
    void clear(bool allowRetry = true);
    void queueChanged();
    bool handleEvent(const QJsonObject &event);
    bool commit(bool automatic);
    int nextIndex(bool automatic = true);
    void consumeNext(int index, bool automatic = true);
    int previousIndex();
    int failureIndex();
signals:
    void changed();
    /// Emitted once after a valid incoming track reaches its audible boundary.
    void notice(const QString &message);

private:
    friend class TrackTransitionControllerTestAccess;
    static QString formatNotice(const QJsonObject &event);
    void reconcileShuffle();
    std::optional<PlaybackTrack> nextTrack(bool automatic = true);
    QString contextKey() const;
    bool stillValid();
    void prepareSource();
    void failAttempt();
    bool adopt(const QJsonObject &event);
    PlaybackService &m_playback;
    ApiClient &m_api;
    EchoMusic::SettingsStore &m_settings;
    PlaybackSourceRequest m_source;
    QTimer m_timeout;
    PlaybackTrack m_target;
    QJsonObject m_privileges, m_resolved;
    QStringList m_urls;
    QList<qint64> m_urlAudioTracks;
    QString m_resolvedQuality;
    QString m_context, m_failedContext, m_targetKey, m_nextQuality = "standard",
                                                     m_mode = "none";
    QList<quint64> m_retired;
    quint64 m_revision = 0, m_requestId = 0, m_sequence = 0, m_probe = 0;
    int m_targetIndex = -1, m_fadeSeconds = 15;
    QString m_shuffleSource;
    QStringList m_shuffleTracks, m_shuffleRemaining, m_shuffleHistory;
    QSet<QString> m_shufflePlayed;
    bool m_prepared = false, m_committing = false, m_fallback = false, m_matchTempo = false;
    bool m_commitAutomatic = false;
    double m_preparedGain = 0;
};
} // namespace EchoMusic
