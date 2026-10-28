#pragma once
#include <QCache>
#include <QTemporaryDir>
#include "ApiClient.h"
#include "PlaybackLibraryActions.h"
#include "SongCommands.h"
#include "AudioEngine.h"
#include "PlayerSpectrum.h"
#include "PlayerEnginePreferences.h"
#include "NetworkPreferences.h"
#include "PlaybackArtwork.h"
#include "SleepTimer.h"
#include "SystemSleepInhibitor.h"
#include "NativeBridge.h"
#include "cast/CastService.h"
#include "PlaybackAudioControls.h"
#include "AudioEffectCatalog.h"
#include "PlaybackQueueModel.h"
#include "PlaybackQueueCatalog.h"
#include "PlaybackRepository.h"
#include <QElapsedTimer>
#include "LyricModel.h"
#include "LyricAppearance.h"
#include "DynamicCoverController.h"
#include "LyricTextController.h"
#include "LyricSourceController.h"
#include "PlaybackSourceRequest.h"
#include "CloudAudioIndex.h"
#include "PlaybackProgressState.h"
#include "PlaybackClimaxModel.h"
#include "ListeningReporter.h"
#include "PersonalFmController.h"
#include "ListenTogetherController.h"
#include "SharingCommands.h"
#include "DiscoverQueueController.h"
#include "TrackTransitionController.h"
#include "PlaybackPreferences.h"
#include <QSettings>
#include <QPointer>
#include <QRandomGenerator>
#include <QTimer>
namespace EchoMusic
{
class SongMetadataService;
class PlaybackQueueController;
class PlaybackOutputController;
class PlaybackSessionController;
class PlaybackRecoveryController;
class PlaybackFailureController;
/// One GUI-thread playback authority. Requests bind both session and track
/// generation. Signed URLs are ephemeral; only catalog identities persist.
class PlaybackService final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(
        EchoMusic::PlayerEnginePreferences *enginePreferences READ enginePreferences CONSTANT)
    Q_PROPERTY(EchoMusic::PlaybackQueueModel *queue READ queue NOTIFY changed)
    Q_PROPERTY(EchoMusic::LyricModel *lyrics READ lyrics CONSTANT)
    Q_PROPERTY(EchoMusic::PlaybackClimaxModel *climax READ climax CONSTANT)
    Q_PROPERTY(EchoMusic::LyricAppearance *lyricAppearance READ lyricAppearance CONSTANT)
    Q_PROPERTY(EchoMusic::DynamicCoverController *dynamicCover READ dynamicCover CONSTANT)
    Q_PROPERTY(EchoMusic::PlaybackLibraryActions *libraryActions READ libraryActions NOTIFY changed)
    Q_PROPERTY(EchoMusic::SongCommands *songCommands READ songCommands CONSTANT)
    Q_PROPERTY(EchoMusic::SharingCommands *sharing READ sharing NOTIFY sharingChanged)
    Q_PROPERTY(EchoMusic::PersonalFmController *personalFm READ personalFm CONSTANT)
    Q_PROPERTY(EchoMusic::ListenTogetherController *listenTogether READ listenTogether CONSTANT)
    Q_PROPERTY(QString trackId READ trackId NOTIFY changed)
    Q_PROPERTY(QString title READ title NOTIFY changed)
    Q_PROPERTY(QString artist READ artist NOTIFY changed)
    Q_PROPERTY(QVariantList artistLinks READ artistLinks NOTIFY changed)
    Q_PROPERTY(QString albumId READ albumId NOTIFY changed)
    Q_PROPERTY(QString album READ album NOTIFY changed)
    Q_PROPERTY(QString cover READ cover NOTIFY changed)
    Q_PROPERTY(QColor coverColor READ coverColor NOTIFY changed)
    Q_PROPERTY(bool playing READ playing NOTIFY changed)
    Q_PROPERTY(bool loading READ loading NOTIFY changed)
    Q_PROPERTY(ProgressBusyReason progressBusyReason READ progressBusyReason NOTIFY
                   progressBusyReasonChanged)
    Q_PROPERTY(bool hasTrack READ hasTrack NOTIFY changed)
    Q_PROPERTY(qint64 positionMs READ positionMs NOTIFY positionChanged)
    Q_PROPERTY(qint64 durationMs READ durationMs NOTIFY changed)
    Q_PROPERTY(double volume READ volume WRITE setVolume NOTIFY changed)
    Q_PROPERTY(double speed READ speed WRITE setSpeed NOTIFY changed)
    Q_PROPERTY(PlayMode playMode READ playMode WRITE setPlayMode NOTIFY changed)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY changed)
    Q_PROPERTY(bool engineReady READ engineReady NOTIFY engineInitializationChanged)
    Q_PROPERTY(QString engineInitializationError READ engineInitializationError NOTIFY
                   engineInitializationChanged)
    Q_PROPERTY(QString preventSleepError READ preventSleepError NOTIFY changed)
    Q_PROPERTY(QString lyricState READ lyricState NOTIFY changed)
    Q_PROPERTY(EchoMusic::PlaybackQueueCatalog *queueCatalog READ queueCatalog CONSTANT)
    Q_PROPERTY(QString queueTitle READ queueTitle NOTIFY changed)
    /// Source identity of the active queue (the same key passed to playTracks).
    /// A discovery/fm page uses it to tell whether it is the live source.
    Q_PROPERTY(QString sourceKey READ sourceKey NOTIFY changed)
    Q_PROPERTY(QString quality READ quality WRITE setQuality NOTIFY changed)
    Q_PROPERTY(QString sourceKind READ sourceKind NOTIFY changed)
    Q_PROPERTY(QString sourceNotice READ sourceNotice NOTIFY changed)
    Q_PROPERTY(QString mvFallbackFailureReason READ mvFallbackFailureReason NOTIFY changed)
    Q_PROPERTY(QString resolvedQuality READ resolvedQuality NOTIFY changed)
    Q_PROPERTY(QString effectiveQuality READ effectiveQuality NOTIFY changed)
    Q_PROPERTY(QString songEffect READ songEffect WRITE setSongEffect NOTIFY changed)
    Q_PROPERTY(QString appliedSongEffect READ appliedSongEffect NOTIFY changed)
    Q_PROPERTY(QString songEffectError READ songEffectError NOTIFY changed)
    Q_PROPERTY(QVariantList qualities READ qualities NOTIFY changed)
    Q_PROPERTY(EchoMusic::SleepTimer *sleepTimer READ sleepTimer CONSTANT)
    Q_PROPERTY(EchoMusic::PlaybackAudioControls *audioControls READ audioControls CONSTANT)
    Q_PROPERTY(EchoMusic::AudioEffectCatalog *effectCatalog READ effectCatalog CONSTANT)
    Q_PROPERTY(EchoMusic::LyricTextController *lyricText READ lyricText CONSTANT)
    Q_PROPERTY(EchoMusic::LyricSourceController *lyricSource READ lyricSource CONSTANT)
    Q_PROPERTY(EchoMusic::TrackTransitionController *transition READ transition CONSTANT)
    Q_PROPERTY(EchoMusic::PlaybackPreferences *preferences READ preferences CONSTANT)
    Q_PROPERTY(EchoMusic::CastService *cast READ cast CONSTANT)
public:
    ListenTogetherController *listenTogether() const
    {
        return m_listenTogether.get();
    }
    PersonalFmController *personalFm() const
    {
        return m_personalFm.get();
    }
    DiscoverQueueController *discoverQueue() const
    {
        return m_discoverQueue.get();
    }
    CastService *cast() const
    {
        return m_cast.get();
    }
    enum PlayMode
    {
        ListLoop,
        SingleLoop,
        Shuffle,
        Sequential
    };
    Q_ENUM(PlayMode)
    enum ProgressBusyReason
    {
        ProgressReady,
        ProgressSeeking,
        ProgressBuffering
    };
    Q_ENUM(ProgressBusyReason)
    ProgressBusyReason progressBusyReason() const
    {
        return static_cast<ProgressBusyReason>(
            m_progress.reason(hasTrack() && !m_closing && m_error.isEmpty(), m_playing));
    }
    explicit PlaybackService(ApiClient &api, QSettings &settings, QObject *parent = nullptr,
                             const QString &storagePath = {},
                             NetworkPreferences *network = nullptr);
    ~PlaybackService() override;
    void attachMetadata(SongMetadataService &metadata);
    SongMetadataService *metadata() const;
    void attachLibrary(UserPlaylistModel &library, PlaylistMutationService &mutations);
    PlaybackLibraryActions *libraryActions()
    {
        return m_libraryActions.get();
    }
    /// 歌曲菜单共享命令（分享/复制信息/不感兴趣）；应用装配时 attach 账号与黑名单服务。
    NativeBridge &nativeBridge()
    {
        return m_native;
    }
    SharingCommands *sharing() const
    {
        return m_songCommands.sharing();
    }
    SongCommands *songCommands()
    {
        return &m_songCommands;
    }
    QString trackId() const
    {
        return m_track.id;
    }
    /// Corrected file facts (embedded tags) observed for an audio hash this
    /// session, so lists loaded after the play can replay them.
    QJsonObject playbackFacts(const QString &hash) const
    {
        const auto *facts = m_playbackFacts.object(hash.toLower());
        return facts ? *facts : QJsonObject();
    }
    const PlaybackTrack &currentTrack() const
    {
        return m_track;
    }
    PlaybackQueueModel *queue()
    {
        return m_catalog.activeModel();
    }
    PlaybackSourceExtensions *sourceExtensions()
    {
        return &m_sourceExtensions;
    }
    PlaybackQueueCatalog *queueCatalog()
    {
        return &m_catalog;
    }
    DynamicCoverController *dynamicCover()
    {
        return &m_dynamicCover;
    }
    LyricAppearance *lyricAppearance()
    {
        return &m_lyricAppearance;
    }
    PlaybackClimaxModel *climax()
    {
        return &m_climax;
    }
    LyricModel *lyrics()
    {
        return &m_lyrics;
    }
    LyricTextController *lyricText()
    {
        return &m_lyricText;
    }
    LyricSourceController *lyricSource()
    {
        return &m_lyricSource;
    }
    TrackTransitionController *transition()
    {
        return m_transition.get();
    }
    quint64 audioSequence() const
    {
        return m_audioSequence;
    }
    PlayerSpectrum *spectrum()
    {
        return &m_spectrum;
    }
    PlayerEnginePreferences *enginePreferences()
    {
        return &m_enginePreferences;
    }
    PlaybackPreferences *preferences()
    {
        return &m_preferences;
    }
    MembershipService *memberships()
    {
        return &m_membership;
    }
    QString title() const
    {
        return m_track.title.isEmpty() ? tr("未在播放") : m_track.title;
    }
    QString artist() const
    {
        return m_track.artist;
    }
    QVariantList artistLinks() const
    {
        return m_track.artistLinks();
    }
    QString albumId() const
    {
        return m_track.albumId ? QString::number(m_track.albumId) : QString();
    }
    QString album() const
    {
        return m_track.album;
    }
    QString cover() const
    {
        return m_track.cover.isEmpty() ? m_embeddedCover : m_track.cover;
    }
    /// Artwork as encoded PNG, or empty while it is still loading. Consumers
    /// that need pixels rather than a URL (native media integrations) read this
    /// instead of re-downloading the cover.
    QByteArray coverPng() const
    {
        return m_coverPng;
    }
    QColor coverColor() const
    {
        return m_coverColor;
    }
    bool playing() const
    {
        return m_playing;
    }
    bool loading() const
    {
        return m_loading;
    }
    bool hasTrack() const
    {
        return m_track.playable();
    }
    qint64 positionMs() const
    {
        return m_position;
    }
    qint64 durationMs() const
    {
        return m_duration;
    }
    double volume() const
    {
        return m_volume;
    }
    /// Output handoff snapshots belong to this volume owner, never to a view.
    void applyOutputVolume(double volume);
    void restoreLocalVolume();
    double localVolume() const
    {
        return m_remoteVolume ? m_localVolume : m_volume;
    }
    double speed() const
    {
        return m_cast && m_cast->remoteMedia() ? 1. : m_speed;
    }
    PlayMode playMode() const
    {
        return m_mode;
    }
    QString errorMessage() const
    {
        return m_error;
    }
    QString preventSleepError() const
    {
        return m_sleepInhibitor.error();
    }
    QString lyricState() const
    {
        return m_lyricState;
    }
    /// Opening the lyric page may precede the first audio load after queue restoration.
    Q_INVOKABLE void ensureLyrics();
    void revokePluginMedia(const QUrl &url);
    QString queueTitle() const
    {
        return m_queueTitle;
    }
    QString sourceKey() const
    {
        return m_sourceKey;
    }
    QString quality() const
    {
        return m_quality;
    }
    CloudAudioIndex *cloudSources()
    {
        return &m_cloudSources;
    }
    QString sourceNoticeCode() const
    {
        return m_sourceNoticeCode;
    }
    QString sourceKind() const
    {
        return m_sourceKind;
    }
    QString mvFallbackFailureReason() const
    {
        return m_mvFailure;
    }
    QString sourceNotice() const
    {
        return m_sourceKind == "mv"      ? tr("当前播放 MV 音频，歌词时间轴可能不同")
               : m_sourceKind == "cloud" ? tr("当前使用云盘文件播放")
                                         : QString();
    }
    QString resolvedQuality() const
    {
        return m_resolvedQuality;
    }
    // A source downgrade changes presentation, never the next-request preference.
    QString effectiveQuality() const
    {
        return m_sourceKind == "cloud" || m_sourceKind == "mv" ? m_sourceKind
               : m_resolvedQuality.isEmpty()                   ? m_quality
                                                               : m_resolvedQuality;
    }
    QString songEffect() const
    {
        return m_songEffect;
    }
    QString appliedSongEffect() const
    {
        return m_appliedEffect;
    }
    QString songEffectError() const
    {
        return m_effectError;
    }
    Q_INVOKABLE void setSongEffect(const QString &effect);
    QVariantList qualities() const;
    Q_INVOKABLE void ensureQualities();
    SleepTimer *sleepTimer()
    {
        return &m_sleepTimer;
    }
    PlaybackAudioControls *audioControls()
    {
        return &m_audioControls;
    }
    AudioEffectCatalog *effectCatalog()
    {
        return &m_effectCatalog;
    }
    void finishSleepAction(const QString &error = {})
    {
        m_sleepTimer.executionFinished(error);
    }
    Q_INVOKABLE void toggleMute();
    void playTracks(const PlaybackTracks &tracks, int index, const QString &title,
                    const QString &sourceKey);
    void playFromSelection(const PlaybackTracks &tracks, int index, const QString &title,
                           const QString &sourceKey, bool selectedSong);
    void queueAndPlayTrack(const PlaybackTrack &track, const QString &title,
                           const QString &sourceKey);
    void appendTracks(const PlaybackTracks &tracks, bool next = false);
    void enqueueTracks(const PlaybackTracks &tracks, bool cut);
    Q_INVOKABLE void playIndex(int index);
    Q_INVOKABLE void playViewedIndex(int index);
    Q_INVOKABLE void removeViewedIndex(int index);
    Q_INVOKABLE void resumeViewedQueue();
    Q_INVOKABLE void toggle();
    Q_INVOKABLE void play();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void previous();
    Q_INVOKABLE void next(bool automatic = false);
    Q_INVOKABLE void seek(qint64 positionMs);
    Q_INVOKABLE void setVolume(double volume);
    Q_INVOKABLE void setSpeed(double speed);
    Q_INVOKABLE void setPlayMode(PlayMode mode);
    Q_INVOKABLE void cycleMode();
    Q_INVOKABLE void setQuality(const QString &quality);
    Q_INVOKABLE void openLocalFiles();
    Q_INVOKABLE void retry();
    /// Local playback history is read/written through the storage worker. The
    /// page owns presentation; PlaybackService keeps the account snapshot.
    Q_INVOKABLE void requestHistory();
    Q_INVOKABLE void removeHistory(const QStringList &keys);
    Q_INVOKABLE void clearHistory();
    /// Internal request epoch for SDK work targeted at the current source.
    quint64 sourceRevision() const
    {
        return m_sourceRevision;
    }
    bool engineReady() const
    {
        return m_ready;
    }
    QString engineInitializationError() const
    {
        return m_initializationError;
    }
    void retryEngineInitialization();
    void setStartupHeld(bool held);
    void shutdown();
signals:
    /// Final failure only; source retries never notify the UI independently.
    void failureNotice(const QString &message, const QString &title);
    void sourceOperationFailed(const QString &message, const QString &title);
    void notice(const QString &message, int tone);
    void sharingChanged();
    void engineInitializationChanged();
    void sleepActionRequested(const QString &action);
    void changed();
    void positionChanged();
    void progressBusyReasonChanged();
    /// Track/source changes and seek confirmation reset presentation interpolation.
    void timelineReset();
    void playbackEnded();
    void seekCompleted();
    void historyLoaded(const QJsonArray &entries);
    void historyFailed(const QString &message);
    /// Corrected identity facts from the playing file's own tags. Identity is
    /// the track id the surfaces already know; consumers merge masked/missing
    /// presentation values only, never overwrite valid provider facts.
    void metadataServiceChanged();
    void trackFactsApplied(const QJsonObject &facts);
    void closed();
    void listeningReportsDrained();

private:
    friend class PersonalFmController;
    friend class ListenTogetherController;
    friend class PlaybackQueueController;
    friend class PlaybackOutputController;
    friend class PlaybackSessionController;
    friend class DiscoverQueueController;
    friend class TrackTransitionController;
    friend class PlaybackRecoveryController;
    friend class PlaybackFailureController;
    friend class PlaybackServiceTestAccess;
    void tryStartupAutoPlay();
    void invalidate(bool preserveLyrics = false);
    void cancelTrackProbe();
    void startIndex(int index, bool preserveFailureChain);
    void appendTracksImpl(const PlaybackTracks &tracks, bool next, bool sequential);
    void startTrack(const PlaybackTrack &track, int index, bool preserveFailureChain);
    /// Legacy "0" snapshots of unmatched cloud uploads adopt the file identity
    /// ("cloud:<hash>" + cloud file record + duration) once the index knows the
    /// audio hash. No-op for local files and catalog songs.
    bool adoptCloudIdentity(PlaybackTrack &track) const;
    /// Re-apply the live identity to an asynchronous source snapshot that may
    /// still carry the legacy view id observed when the request started.
    PlaybackTrack mergeResolvedTrack(const PlaybackTrack &track) const;
    void resolve();
    void loadNextUrl();
    void refreshCurrentSource();
    ApiClient::SoundEffect apiEffect() const;
    void onEvent(const QJsonObject &event);
    void consumeOutputEvent(const QJsonObject &event);
    void mediaRouteChanged();
    void publishProgressStatus();
    PlaybackProgressState m_progress;
    ProgressBusyReason m_publishedProgress = ProgressReady;
    void requestLyrics();
    void requestEmbeddedMetadata();
    void persist();
    void finishHistory();
    void publishMedia();
    ApiClient::PlaybackQuality apiQuality() const;
    QTimer m_mediaTimer;
    SleepTimer m_sleepTimer;
    SystemSleepInhibitor m_sleepInhibitor;
    QTemporaryDir m_embeddedDirectory;
    NativeBridge m_native;
    std::unique_ptr<CastService> m_cast;
    std::unique_ptr<PlaybackLibraryActions> m_libraryActions;
    SongCommands m_songCommands;
    ApiClient &m_api;
    QSettings &m_settings;
    PlaybackPreferences m_preferences;
    PlayerEnginePreferences m_enginePreferences;
    QPointer<NetworkPreferences> m_networkPreferences;
    MembershipService m_membership;
    AudioEngine m_engine;
    PlayerSpectrum m_spectrum;
    CloudAudioIndex m_cloudSources;
    QList<qint64> m_urlAudioTracks;
    PlaybackSourceExtensions m_sourceExtensions;
    PlaybackSourceRequest m_source;
    bool recoverSource(const QString &reason);
    bool tryMvFallback(const QString &reason);
    bool m_mvRecovery = false;
    QString m_sourceEffect = "none", m_sourceNoticeCode;
    QString m_sourceKind = "catalog", m_sourcePreference, m_sourceFailure, m_mvFailure;
    std::unique_ptr<PlaybackQueueController> m_queueController;
    std::unique_ptr<PlaybackOutputController> m_outputController;
    std::unique_ptr<PlaybackSessionController> m_sessionController;
    std::unique_ptr<TrackTransitionController> m_transition;
    std::unique_ptr<PlaybackRecoveryController> m_recovery;
    std::unique_ptr<PlaybackFailureController> m_failure;
    std::unique_ptr<PersonalFmController> m_personalFm;
    std::unique_ptr<ListenTogetherController> m_listenTogether;
    std::unique_ptr<DiscoverQueueController> m_discoverQueue;
    PlaybackAudioControls m_audioControls;
    AudioEffectCatalog m_effectCatalog;
    PlaybackArtwork m_artwork;
    QColor m_coverColor;
    QByteArray m_coverPng;
    QString m_embeddedCover;
    PlaybackQueueCatalog m_catalog;
    std::unique_ptr<PlaybackRepository> m_repository;
    QTimer m_saveTimer;
    QElapsedTimer m_listenTick;
    qint64 m_listenedMs = 0;
    quint64 m_account = 0, m_restoreSequence = 0;
    int m_closedWorkers = 0;
    bool m_restoring = false, m_loaded = false, m_queueRestorePending = false;
    LyricModel m_lyrics;
    PlaybackClimaxModel m_climax;
    ListeningReporter m_listening;
    LyricAppearance m_lyricAppearance;
    DynamicCoverController m_dynamicCover;
    LyricTextController m_lyricText;
    LyricSourceController m_lyricSource;
    PlaybackTrack m_track;
    QCache<QString, QJsonObject> m_playbackFacts{2000};
    QPointer<SongMetadataService> m_metadata;
    QString m_resolvedQuality;
    QString m_initializationError;
    bool m_startupHeld = false;
    QString m_queueTitle, m_sourceKey, m_error, m_lyricState = "idle", m_quality = "standard";
    QStringList m_urls;
    QString m_songEffect = "none", m_appliedEffect = "none", m_effectError, m_probedSource;
    quint64 m_trackProbe = 0, m_embeddedRequest = 0;
    qint64 m_audioTrack = 0;
    bool m_effectFallback = false, m_seamless = false;
    QJsonObject m_privileges;
    QJsonObject m_pendingLoudness;
    // Fresh volume preferences and unmute fallback both use 50%.
    static constexpr double DefaultVolume = 50.;
    double m_unmutedVolume = DefaultVolume;
    double m_localVolume = DefaultVolume, m_localUnmutedVolume = DefaultVolume;
    bool m_remoteVolume = false;
    qint64 m_resumePosition = -1;
    int m_urlIndex = 0;
    quint64 m_audioSequence = 0, m_audioSerial = 0, m_sourceRevision = 0;
    quint64 m_castRefreshSequence = 0;
    quint64 m_sequence = 0, m_revision = 0;
    qint64 m_position = 0, m_duration = 0;
    double m_volume = DefaultVolume, m_speed = 1;
    PlayMode m_mode = ListLoop;
    bool m_playing = false, m_loading = false, m_autoplay = false, m_ready = false,
         m_closing = false, m_startupAutoPlayPending = false;
};
} // namespace EchoMusic
