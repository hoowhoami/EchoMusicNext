#pragma once
#include "ApiClient.h"
#include "ListenTogetherListModel.h"
#include "PlaybackTrack.h"
#include <QElapsedTimer>
#include <QHash>
#include <QJsonObject>
#include <QJsonArray>
#include <QPointer>
#include <QSet>
#include <QTimer>
#include <functional>
namespace EchoMusic
{
class AccountService;
class PlaybackService;
class PlaybackRepository;
class SongMetadataService;
/// Playback-owned room session. Discovery/preview are separate from membership;
/// hiding its page never ends a joined room or creates a second audio player.
class ListenTogetherController final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(Phase phase READ phase NOTIFY changed)
    Q_PROPERTY(bool signedIn READ signedIn NOTIFY changed)
    Q_PROPERTY(bool joined READ joined NOTIFY changed)
    Q_PROPERTY(bool owner READ owner NOTIFY changed)
    Q_PROPERTY(bool playbackDetached READ playbackDetached NOTIFY changed)
    Q_PROPERTY(QString roomId READ roomId NOTIFY changed)
    Q_PROPERTY(QString nickname READ nickname NOTIFY changed)
    Q_PROPERTY(QString currentUserId READ currentUserId NOTIFY changed)
    Q_PROPERTY(int sendCooldownSeconds READ sendCooldownSeconds NOTIFY sendCooldownChanged)
    Q_PROPERTY(int onlineCount READ onlineCount NOTIFY changed)
    Q_PROPERTY(QVariantMap activeRoom READ activeRoom NOTIFY changed)
    Q_PROPERTY(QVariantMap previewRoom READ previewRoom NOTIFY changed)
    Q_PROPERTY(QVariantMap currentSong READ currentSong NOTIFY changed)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY changed)
    Q_PROPERTY(bool loadingRooms READ loadingRooms NOTIFY changed)
    Q_PROPERTY(bool roomsEnded READ roomsEnded NOTIFY changed)
    Q_PROPERTY(bool loadingPreview READ loadingPreview NOTIFY changed)
    Q_PROPERTY(bool loadingRoom READ loadingRoom NOTIFY changed)
    Q_PROPERTY(bool loadingSongs READ loadingSongs NOTIFY changed)
    Q_PROPERTY(bool loadingOrders READ loadingOrders NOTIFY changed)
    Q_PROPERTY(bool sendingMessage READ sendingMessage NOTIFY changed)
    Q_PROPERTY(bool updatingChat READ updatingChat NOTIFY changed)
    Q_PROPERTY(bool addingSongs READ addingSongs NOTIFY changed)
    Q_PROPERTY(int roomScope READ roomScope WRITE setRoomScope NOTIFY changed)
    Q_PROPERTY(QString search READ search WRITE setSearch NOTIFY changed)
    Q_PROPERTY(EchoMusic::ListenTogetherListModel *rooms READ rooms CONSTANT)
    Q_PROPERTY(EchoMusic::ListenTogetherListModel *members READ members CONSTANT)
    Q_PROPERTY(EchoMusic::ListenTogetherListModel *previewMembers READ previewMembers CONSTANT)
    Q_PROPERTY(EchoMusic::ListenTogetherListModel *messages READ messages CONSTANT)
    Q_PROPERTY(EchoMusic::ListenTogetherListModel *songs READ songs CONSTANT)
    Q_PROPERTY(EchoMusic::ListenTogetherListModel *orders READ orders CONSTANT)
public:
    enum Phase
    {
        Idle,
        Joining,
        Creating,
        Joined,
        Leaving,
        Error
    };
    Q_ENUM(Phase)
    ListenTogetherController(PlaybackService &, ApiClient &);
    ~ListenTogetherController() override;
    static QString queueKey()
    {
        return QStringLiteral("queue:listen-together");
    }
    void attachAccount(AccountService &);
    void attachMetadata(SongMetadataService &);
    void attachRepository(PlaybackRepository &);
    void setPageActive(bool active, bool networkEnabled = true, bool recoverSession = true);
    void openSharedRoom(const QString &id, bool networkEnabled = true);
    void shutdown();
    Phase phase() const
    {
        return m_phase;
    }
    bool signedIn() const
    {
        return m_api.currentAccountUserId() != 0;
    }
    bool joined() const
    {
        return !m_closed && m_phase == Joined && !m_roomId.isEmpty();
    }
    bool owner() const;
    bool playbackDetached() const;
    QString nickname() const;
    QString currentUserId() const
    {
        const auto id = m_api.currentAccountUserId();
        return id ? QString::number(id) : QString();
    }
    int sendCooldownSeconds() const
    {
        return m_api.messageSendCooldownSeconds();
    }
    int onlineCount() const;
    QString roomId() const
    {
        return m_roomId;
    }
    QVariantMap activeRoom() const
    {
        return m_activeRoom;
    }
    QVariantMap previewRoom() const
    {
        return m_previewRoom;
    }
    QVariantMap currentSong() const;
    QString errorMessage() const
    {
        return m_error;
    }
    bool loadingRooms() const
    {
        return m_loadingRooms;
    }
    bool roomsEnded() const
    {
        return m_roomsEnded;
    }
    bool loadingPreview() const
    {
        return m_loadingPreview;
    }
    bool loadingRoom() const
    {
        return m_loadingRoom;
    }
    bool loadingSongs() const
    {
        return m_loadingSongs;
    }
    bool loadingOrders() const
    {
        return m_loadingOrders;
    }
    bool sendingMessage() const
    {
        return m_sendingMessage;
    }
    bool updatingChat() const
    {
        return m_updatingChat;
    }
    bool addingSongs() const
    {
        return m_addingSongs;
    }
    int roomScope() const
    {
        return m_scope;
    }
    void setRoomScope(int scope);
    QString search() const
    {
        return m_search;
    }
    void setSearch(const QString &text);
    ListenTogetherListModel *rooms()
    {
        return &m_rooms;
    }
    ListenTogetherListModel *members()
    {
        return &m_members;
    }
    ListenTogetherListModel *previewMembers()
    {
        return &m_previewMembers;
    }
    ListenTogetherListModel *messages()
    {
        return &m_messages;
    }
    ListenTogetherListModel *songs()
    {
        return &m_songs;
    }
    ListenTogetherListModel *orders()
    {
        return &m_orders;
    }
    Q_INVOKABLE void loadRooms(bool reset = true);
    Q_INVOKABLE void inspectRoom(const QString &id);
    Q_INVOKABLE void closePreview();
    Q_INVOKABLE void createRoom(const QString &name, int privacy);
    Q_INVOKABLE void joinRoom(const QString &id);
    Q_INVOKABLE void leaveRoom(bool dismiss = false);
    Q_INVOKABLE void dismissOwnedRoom(const QString &id);
    Q_INVOKABLE void refreshSongs();
    Q_INVOKABLE void refreshOrders();
    Q_INVOKABLE void sendMessage(const QString &text);
    Q_INVOKABLE void setChatEnabled(bool enabled);
    Q_INVOKABLE void playRoomSong(const QString &id);
    Q_INVOKABLE void resumeRoomPlayback();
    Q_INVOKABLE void addSongs(const QVariantList &domainSongs);
    Q_INVOKABLE void requestSong(const QVariantMap &domainSong);
    Q_INVOKABLE void approveOrder(const QString &key);
    Q_INVOKABLE void rejectOrder(const QString &key);
    void localPlayRequested();
    void localPauseRequested();
    void localSeekRequested(qint64 position);
    bool suppressAutomaticNext() const;
    void nativeSeekCompleted();
    void nativeTrackEnded();
    void nativeTrackReady();
    void localTrackRequested();
    bool roomSourceAllowed(const PlaybackTrack &) const;
signals:
    void sendCooldownChanged();
    void changed();
    void previewRequested();
    void previewClosed();
    void loginRequested();
    void notice(const QString &text, int level);
    void messageSent();
    void songsSubmitted();
    void sessionReady();

private:
    friend class ListenTogetherControllerTestAccess;
    using Success = std::function<void(const QJsonObject &)>;
    using Failure = std::function<void(const QJsonObject &)>;
    enum class Scope
    {
        Account,
        Page,
        Preview,
        Room
    };
    struct Pending
    {
        quint64 epoch = 0, session = 0, view = 0;
        Scope scope = Scope::Account;
        QString room;
        Success done;
        Failure failed;
    };
    void watch(quint64 request, Scope scope, Success done, Failure failed = {});
    void succeeded(quint64 request, const QByteArray &);
    void failed(quint64 request, const QByteArray &);
    bool current(const Pending &) const;
    void cancel(Scope scope);
    void cancelAll();
    bool requireLogin();
    void after(quint64 request, std::function<void()> terminal);
    void finishHydration();
    void mergeMessages(const QVariantList &messages);
    void reportError(const QJsonObject &, bool toast = true);
    bool dissolved(const QJsonObject &) const;
    void accountChanged();
    void updateRoomsProjection();
    QVariantMap findRoom(const QString &id) const;
    void loadOwnedRooms();
    void upsertOwned(const QVariantMap &);
    void rememberDissolved(const QString &id);
    void saveRoomIndex();
    void recover(Success done, Failure failed = {});
    void transition(const QString &key, Phase phase, std::function<void()> action);
    void leaveInternal(bool dismiss, std::function<void()> continuation = {});
    void hydrate(const QString &, const QVariantMap &);
    void clearSession(bool preserveTransition = false);
    void loadRoomDetail();
    void loadMembers(std::function<void()> terminal = {});
    void loadMessages(std::function<void()> terminal = {});
    void loadSongs(bool force = false);
    void loadSongsPage(const std::optional<ListenRoomAudio> &, quint64 generation);
    void acceptSongPage(const QJsonObject &, quint64 generation, bool recent);
    void updateQueue(bool activate = false);
    void publishSongs();
    void completeSongs();
    void cancelMetadata();
    void restoreQueue();
    void startTimers();
    void fastPoll();
    void slowPoll();
    void heartbeat();
    void syncPlayback(bool force = false, std::function<void()> terminal = {});
    void applyRemote(bool force);
    void playbackChanged();
    void positionChanged();
    void releaseRemoteApply();
    ListenRoomProgress currentProgress() const;
    ListenRoomAudio trackAudio(const PlaybackTrack &) const;
    int remoteSongIndex() const;
    void startRemoteTrack(int index, bool force);
    void enqueueOwner(const ListenMusicRequest &);
    void sendNextOwnerCommand();
    void addTracks(const PlaybackTracks &, const QString &requester = {});
    PlaybackService &m_playback;
    ApiClient &m_api;
    QPointer<AccountService> m_account;
    QPointer<PlaybackRepository> m_repository;
    QPointer<SongMetadataService> m_metadata;
    QHash<quint64, quint64> m_metadataRequests;
    QHash<quint64, Pending> m_pending;
    QHash<quint64, QList<std::function<void()>>> m_terminals;
    ListenTogetherListModel m_rooms, m_members, m_previewMembers, m_messages, m_songs, m_orders;
    QVariantList m_publicRooms, m_ownedRooms;
    QSet<QString> m_dissolved;
    QStringList m_dissolvedOrder;
    QHash<QString, QVariantMap> m_memberSnapshot;
    QVariantMap m_activeRoom, m_previewRoom;
    PlaybackTracks m_tracks, m_initialTracks;
    std::optional<ListenRoomProgress> m_initialProgress;
    QJsonArray m_songPages;
    QJsonObject m_remote;
    QHash<QString, QJsonObject> m_roomFacts;
    QList<ListenMusicRequest> m_ownerCommands;
    QTimer m_fastTimer, m_slowTimer, m_heartbeatTimer;
    QElapsedTimer m_remoteReceived, m_seekTimer, m_retryClock;
    QHash<QString, qint64> m_playbackRetry;
    quint64 m_memberRequest = 0, m_messageRequest = 0, m_syncRequest = 0, m_orderRequest = 0;
    quint64 m_epoch = 0, m_pageRevision = 0, m_previewRevision = 0, m_songGeneration = 0,
            m_syncRevision = 0;
    QByteArray m_savedIndex;
    QString m_roomId, m_error, m_search, m_previousQueue, m_transitionKey, m_version,
        m_songPageVersion, m_createdName;
    QString m_lastTrackKey, m_lastSource, m_lastMessageId = "0", m_seekKey;
    Phase m_phase = Idle;
    int m_scope = 0, m_page = 0, m_heartbeatFailures = 0;
    int m_lastMode = 0;
    qint64 m_ownerGraceUntil = 0;
    qint64 m_remoteTarget = 0, m_pendingLocalSeek = -1, m_seekStarted = -1, m_remoteSeekLatency = 0;
    quint64 m_remoteSequence = 0, m_lastReadySequence = 0;
    bool m_pageActive = false, m_networkEnabled = true, m_closed = false, m_recoverOnEntry = true;
    bool m_loadingRooms = false, m_roomsEnded = false, m_loadingPreview = false,
         m_loadingRoom = false, m_loadingSongs = false, m_loadingOrders = false;
    bool m_sendingMessage = false, m_updatingChat = false, m_addingSongs = false;
    bool m_fastPending = false, m_slowPending = false, m_heartbeatPending = false,
         m_ownerPending = false;
    bool m_detached = false, m_guestPaused = false, m_remoteApplying = false,
         m_applyDispatching = false, m_forceAfterLoad = false, m_remoteSeekPending = false;
    bool m_initialSongsDone = false, m_initialSyncDone = false, m_memberSnapshotReady = false;
    bool m_lastPlaying = false, m_ownerAutomatic = false, m_mineComplete = false,
         m_indexDirty = false;
};
} // namespace EchoMusic
