#pragma once

#include "DeviceIdentity.h"
#include "ListenTogetherApi.h"
#include <QHash>
#include <QElapsedTimer>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QStringList>
#include <QThread>
#include <cstdint>
#include <optional>
#include <memory>

struct EchoRuntime;
struct EchoResult;

namespace EchoMusic
{
class ApiClientTestAccess;
enum class AuthenticationMode
{
    // Credential injection only; endpoint signing/encryption is still required.
    CurrentAccount,
    // Keep the persistent device, omit userid/token/t1 (including registration/login).
    DeviceOnly,
    // Explicitly use a separate anonymous device; not EchoMusic's X-Skip-Auth.
    IndependentDevice
};
struct RequestOptions
{
    AuthenticationMode authentication = AuthenticationMode::CurrentAccount;
    bool automaticVerification = true;
    /// Complete a challenge without replaying the original upload/write.
    bool replayAfterVerification = true;
};

/// App-owned bridge. Public methods and terminal delivery belong to the GUI thread.
/// Workers copy borrowed Rust bytes into the dispatcher; dispatcher outlives drain.
class ApiClient final : public QObject
{
    Q_OBJECT
public:
    explicit ApiClient(QObject *parent = nullptr);
    explicit ApiClient(DeviceIdentity device, QObject *parent = nullptr);
    ~ApiClient() override;
    bool initializeRuntime();
    bool runtimeAvailable() const
    {
        return m_runtime != nullptr && !m_closing;
    }
    quint64 capabilities();
    enum class SearchKind
    {
        Song,
        Album,
        Artist,
        Playlist,
        Lyric,
        Mv
    };
    /// Refresh uses the current immutable userid/token/t1/device snapshot.
    quint64 refreshLogin();
    /// Global API limit for new requests; zero disables the global deadline.
    /// Endpoint-specific limits remain. Called by the host settings owner, not QML.
    bool setNetworkTimeout(double seconds);
    bool setNetworkTransport(quint16 loopbackPort);
    /// Register the persistent device without any account credentials.
    quint64 registerDevice();
    quint64 recognizeAudio(const QByteArray &pcm);
    DeviceIdentity deviceIdentitySnapshot() const
    {
        return m_device;
    }
    bool setDeviceIdentity(DeviceIdentity device);
    /// Trusted diagnostic commands only; never exposed through the QML metaobject.
    QString diagnosticAuthorization() const;
    QVariantList diagnosticIdentityEntries() const;
    quint64 currentAccountUserId() const
    {
        return m_userId;
    }
    enum class QrProvider
    {
        Kugou,
        Qq,
        Wechat
    };
    quint64 createProviderQr(QrProvider provider);
    quint64 checkProviderQr(QrProvider provider, const QJsonObject &session);
    quint64 sendSmsCode(const QString &mobile);
    quint64 smsLogin(const QString &mobile, const QString &code,
                     std::optional<quint64> selectedUser = {});
    quint64 exchangeWechatCode(const QString &code);
    quint64 finishProviderLogin(QrProvider provider, const QJsonObject &authorization);
    quint64 createQrLogin();
    quint64 checkQrLogin(const QString &key);
    /// Password login always skips the current account; a challenge suspends the
    /// original request. Successful credential conversion is pending sampling.
    quint64 passwordLogin(const QString &username, const QString &password);
    quint64 searchHot(RequestOptions options = {});
    quint64 searchDefault(RequestOptions options = {});
    quint64 search(SearchKind kind, const QString &keyword, quint32 page = 1, quint32 pageSize = 30,
                   RequestOptions options = {});
    quint64 searchSuggest(const QString &keyword, RequestOptions options = {});
    quint64 albumDynamicCover(quint64 albumAudioId, quint64 albumId);
    /// Optional metadata for a committed track; current-account snapshot, no challenge UI.
    quint64 songClimax(const QString &hash);
    /// Discovery commands return typed domain DTOs.
    quint64 recommendedPlaylists(const QString &categoryId);
    quint64 editorPlaylists();
    quint64 styleRecommendations(const QStringList &tagIds = {});
    quint64 dailyRecommendations();
    /// A fixed 30-song batch without pagination.
    quint64 freeListenSongs();
    /// Discovery feed using a device snapshot without account credentials.
    quint64 discoverRecommendations(quint32 pageSize = 4);
    enum class FmMode
    {
        Normal,
        Small,
        Peak,
        Radio
    };
    enum class FmAction
    {
        Play,
        Login,
        Garbage,
        ClickRed,
        CancelRed,
        ChangeSongPool
    };
    struct FmRequest
    {
        FmMode mode = FmMode::Normal;
        FmAction action = FmAction::Play;
        quint32 pool = 0, remaining = 0;
        bool overplay = false;
        QString hash, mark;
        std::optional<quint64> audioId, playSeconds;
    };
    /// Preview/buffer/feedback share the source account and explicit SSA policy.
    quint64 personalFm(const FmRequest &request);
    quint64 listenTogetherRoom(const ListenRoomRequest &request);
    quint64 listenTogetherMusic(const ListenMusicRequest &request);
    quint64 listenTogetherChat(const ListenChatRequest &request);
    quint64 newSongCatalog();
    quint64 newAlbumCatalog();
    quint64 playlistCategories();
    quint64 artistDirectory(quint32 sex, quint32 category, quint32 musician);
    quint64 recommendedRanks();
    quint64 rankCatalog();
    quint64 rankSongs(quint64 rankId);
    enum class CommentResource
    {
        Music,
        Album,
        Playlist
    };
    enum class CommentSection
    {
        All,
        Category,
        HotWord,
        Floor
    };
    struct CommentQuery
    {
        CommentResource resource = CommentResource::Music;
        CommentSection section = CommentSection::All;
        QString id, threadId, hotWord, code;
        quint64 mixSongId = 0, categoryId = 0;
        quint32 page = 1, pageSize = 30;
    };
    struct CommentWrite
    {
        CommentResource resource = CommentResource::Music;
        QString id, name, content, commentId, threadId, parentId, replyUserName, replyContent, code;
        quint64 mixSongId = 0;
    };
    quint64 comments(const CommentQuery &query);
    /// Upstream SSA rejection may suspend for explicit human proof and one
    /// replay. No transport/business failure or uncertain commit is retried.
    quint64 sendComment(const CommentWrite &write);
    quint64 replyComment(const CommentWrite &write);
    quint64 deleteComment(const CommentWrite &write);
    quint64 songRanking(quint64 mixSongId, bool details = false);
    quint64 commentCount(const QString &hash);
    quint64 favoriteCount(quint64 mixSongId);
    quint64 songMv(quint64 mixSongId);
    quint64 videoDetail(const QString &videoId);
    quint64 videoPrivilege(const QString &hash);
    quint64 videoUrl(const QString &hash);
    quint64 collectVideo(quint64 videoId, bool collect);
    quint64 commentAuthorProducts(const QList<quint64> &userIds);
    quint64 songBarrage(const QString &hash, quint32 pageSize = 100);
    quint64 sendSongBarrage(const QString &hash, const QString &name, const QString &content);
    quint64 videoBarrage(const QString &hash, quint32 pageSize = 100);
    quint64 sendVideoBarrage(const QString &hash, const QString &name, const QString &content);
    quint64 albumDetail(quint64 albumId,
                        RequestOptions options = {AuthenticationMode::CurrentAccount, false});
    quint64 albumSongs(quint64 albumId, quint32 page = 1, quint32 pageSize = 30,
                       RequestOptions options = {});
    quint64 artistDetail(quint64 artistId,
                         RequestOptions options = {AuthenticationMode::CurrentAccount, false});
    quint64 artistSongs(quint64 artistId, quint32 page = 1, quint32 pageSize = 100,
                        const QString &sort = QStringLiteral("new"),
                        RequestOptions options = {AuthenticationMode::CurrentAccount, false});
    quint64 artistAlbums(quint64 artistId, quint32 page = 1, quint32 pageSize = 30,
                         const QString &sort = QStringLiteral("hot"),
                         RequestOptions options = {AuthenticationMode::CurrentAccount, false});
    quint64 userDetail(RequestOptions options = {});
    quint64 userInfo();
    quint64 userVipDetail();
    quint64 userGradeInfo(std::optional<quint64> userId = {}, bool automaticVerification = true);
    enum class ListeningEvent
    {
        Start,
        End
    };
    /// Non-replayable accounting writes, always skip automatic verification.
    quint64 reportListeningEvent(ListeningEvent event, quint64 songId, qint64 durationMs = 0,
                                 bool completed = false);
    quint64 reportListeningProgress(quint64 baselineSeconds, quint64 incrementSeconds);
    enum class SocialList
    {
        Follow,
        Friends,
        Fans,
        Visitors
    };
    quint64 socialUsers(SocialList list);
    quint64 followUser(quint64 id, bool following);
    quint64 followArtist(quint64 id, bool following);
    quint64 socialMessages(quint64 target, const QString &tag = {});
    quint64 sendSocialMessage(quint64 target, const QString &tag, const QString &text,
                              const QString &nickname);
    quint64 loginDevices();
    struct LoginDeviceTarget
    {
        QString mid, transportFingerprint;
        quint64 activeAtSeconds;
        quint32 appId, clientVersion;
    };
    /// Explicit account write; no automatic verification/replay.
    quint64 kickLoginDevice(const LoginDeviceTarget &target);
    struct ProfilePatch
    {
        std::optional<QString> nickname, birthday, signature, province, city;
        std::optional<quint32> gender;
    };
    /// Account write: never automatic verification/replay after uncertain commit.
    quint64 updateUserProfile(const ProfilePatch &patch);
    /// Upload bytes only; callers own preparation and any subsequent profile write.
    quint64 uploadUserAvatar(const QByteArray &image);
    /// Submit a previously uploaded filename, without uploading or changing local state.
    quint64 updateUserAvatar(const QString &fileName);
    struct PreferencePatch
    {
        std::optional<QString> mode, gender, age, languages, styles, recommendation;
    };
    enum class BlacklistKind
    {
        Song,
        Singer
    };
    quint64 contentBlacklist(BlacklistKind kind, quint32 page = 1, quint32 pageSize = 30);
    quint64 editContentBlacklist(BlacklistKind kind, const QString &key, const QString &name,
                                 const QString &mixSongId, bool remove);
    quint64 listeningPreferences();
    quint64 updateListeningPreferences(const PreferencePatch &patch);
    quint64 userPlaylists(quint32 page = 1, quint32 pageSize = 30);
    /// Cloud drive listing/deletion. kv_id is the only deletion identity the
    /// upstream accepts (albumAudioId defaults to 0); a successful delete ack
    /// does not prove removal, so callers re-read the list.
    quint64 userCloud(quint32 page = 1, quint32 pageSize = 30);
    quint64 cloudSource(const QJsonObject &source);
    struct CloudDeleteTarget
    {
        quint64 kvId = 0;
        quint64 albumAudioId = 0;
    };
    /// Cloud upload stages are private host operations; QML never receives proof tokens.
    enum class CloudUploadStage
    {
        Auth,
        Initiate,
        Part,
        Complete,
        Add
    };
    quint64 cloudUpload(CloudUploadStage stage, QJsonObject parameters);
    quint64 cloudMatch(const QString &hash);
    quint64 userCloudDelete(const QList<CloudDeleteTarget> &targets);
    quint64 userVideoCollect(quint32 page = 1, quint32 pageSize = 30);
    /// Purchased goods rows are not mapped; return container facts without fabricated fields.
    quint64 purchasedSongs(quint32 page = 1, quint32 pageSize = 30);
    quint64 purchasedAlbums(quint32 page = 1, quint32 pageSize = 30);
    /// These tags prevent a public detail's numeric ID being implicitly passed
    /// to an account cloud endpoint. Only /user/playlist supplies CloudPlaylistId.
    struct CloudPlaylistId
    {
        quint64 value;
    };
    struct PublicCollectionId
    {
        QString value;
    };
    /// Import creates provider-side tasks. Creation/upload never auto-replay;
    /// stopping monitoring does not cancel a task already accepted upstream.
    quint64 importPlaylistLink(const QString &text);
    quint64 submitPlaylistImportImage(const QString &taskSerial, const QByteArray &image);
    quint64 importPlaylistScreenshot(const QString &taskSerial, CloudPlaylistId id,
                                     const QString &title);
    quint64 playlistImportStatus(quint64 taskId);
    quint64 playlistImportResult(CloudPlaylistId id, quint32 page = 1, quint32 pageSize = 100);
    quint64 playlistDetails(const QList<PublicCollectionId> &ids, RequestOptions options = {});
    quint64 publicPlaylistTracks(const PublicCollectionId &id, quint32 page = 1,
                                 quint32 pageSize = 30, RequestOptions options = {});
    quint64 cloudPlaylistTracks(CloudPlaylistId id, quint32 page = 1, quint32 pageSize = 300);
    /// Presentation facts only. Bounded 100-ID batches never resolve audio/rights.
    quint64 songMetadata(const QList<quint64> &ids,
                         RequestOptions options = {AuthenticationMode::CurrentAccount, false});
    /// Writes always require the current account and never enter automatic
    /// verification/replay. A transport failure may follow a committed mutation.
    struct PlaylistTrackFileId
    {
        quint64 value;
    };
    struct PlaylistTrackInput
    {
        QString title, hash;
        quint64 albumId, songId;
    };
    struct PlaylistTrackPosition
    {
        PlaylistTrackFileId fileId;
        quint32 position;
    };
    enum class PlaylistKind
    {
        Owned,
        Collected
    };
    struct PlaylistPosition
    {
        CloudPlaylistId id;
        PlaylistKind kind;
        quint32 position;
    };
    struct PlaylistUpdate
    {
        CloudPlaylistId id;
        quint64 totalRevision;
        qint64 sortIndex;
        std::optional<QString> title;
        QString description, tags;
        std::optional<QString> coverReference = {};
        PlaylistKind kind = PlaylistKind::Owned;
    };
    quint64 sortPlaylists(quint64 totalRevision, const QList<PlaylistPosition> &positions);
    /// Account-free fixed image origin, never sends credentials/device fields.
    /// Accepts the editor's already prepared PNG (image work belongs off GUI).
    quint64 uploadPlaylistCover(const QByteArray &png);
    quint64 createPlaylist(const QString &title, bool isPrivate);
    struct CollectionTarget
    {
        QString title;
        quint32 source = 1;
        std::optional<quint64> creatorId;
        quint64 creatorListId = 0;
        QString creatorGlobalId;
    };
    quint64 collectPlaylist(const CollectionTarget &target);
    quint64 addPlaylistTracks(CloudPlaylistId id, const QList<PlaylistTrackInput> &tracks);
    quint64 removePlaylistTracks(CloudPlaylistId id, const QList<PlaylistTrackFileId> &files);
    quint64 sortPlaylistTracks(CloudPlaylistId id, quint64 revision,
                               const QList<PlaylistTrackPosition> &positions);
    quint64 updatePlaylist(const PlaylistUpdate &update);
    quint64 deletePlaylist(CloudPlaylistId id);
    enum class EffectCategory
    {
        Artist,
        HeadphoneBrands,
        HeadphoneModels,
        Market
    };
    quint64 audioEffects(EffectCategory category, quint32 page = 1, quint32 pageSize = 20,
                         quint32 sort = 2, quint64 brand = 0);
    quint64 commonHeadphoneEffect();
    quint64 lyricCandidates(const QString &hash, quint64 songId, quint64 durationMs,
                            bool allCandidates = false);
    /// The same lyric transport, with no track hash/album ID. Automatic fallback
    /// preserves the requested full-catalog or best-match mode; manual uses full.
    quint64 lyricCandidatesByKeywords(const QString &keywords, quint64 durationMs,
                                      bool allCandidates = true);
    quint64 lyric(const QString &id, const QString &accessKey);
    quint64 userVerify(RequestOptions options = {});
    enum class PlaybackQuality
    {
        Standard,
        Higher,
        Flac,
        HiRes,
        ViperTape,
        ViperClear,
        ViperAtmos,
        Automatic
    };
    enum class SoundEffect
    {
        None,
        Piano,
        Vocal,
        Accompaniment,
        Subwoofer,
        Ancient,
        Surnay,
        Dj
    };
    quint64 playbackSource(const QString &hash, quint64 albumId, quint64 songId,
                           PlaybackQuality quality, const QString &authorization = QString(),
                           RequestOptions options = {}, SoundEffect effect = SoundEffect::None,
                           const QString &originPage = {});
    struct PrivilegeResource
    {
        QString hash;
        std::optional<quint64> albumId;
    };
    quint64 songPrivileges(const QList<PrivilegeResource> &resources, RequestOptions options = {});
    /// Only account service supplies login results. Changes invalidate all old
    /// requests. Empty token/userId=0 clears the account; partial identities fail.
    bool setAccount(quint64 userId, QString token, QString t1 = {});
    /// Check after direct Qt signals: adoption must not overwrite a reentrant account change.
    bool matchesAccountSnapshot(quint64 userId, const QString &token, const QString &t1) const;
    /// Verification operations inherit the suspended request snapshot, including
    /// anonymous/device-only policy. Acknowledgement permits exactly one retry.
    quint64 requestVerification(const QString &eventId);
    quint64 verificationInfo(quint64 requestId);
    /// Type 38 explicitly authorizes a login transition, detached from ordinary
    /// SSA proof. Other session changes still discard every old request.
    bool awaitLoginVerification(quint64 requestId);
    void cancelLoginVerification();
    quint64 submitVerification(quint64 requestId, quint32 verifyType, const QString &proof);
    quint64 sessionRevision() const
    {
        return m_revision;
    }
    void advanceSession();
    /// Cancel the GUI-owned logical request, including a finished native job
    /// whose callback is still queued. Its later result cannot resume verification.
    bool cancel(quint64 requestId);
    /// Scope-owned cancellation preserves other waiters for a shared challenge.
    bool cancelRequest(quint64 requestId);
    void bindRequestToSession(quint64 requestId);
    void shutdown();

signals:
    void succeeded(quint64 requestId, const QByteArray &payload);
    void failed(quint64 requestId, int status, const QByteArray &message);
    void closed();
    void verificationRequired(quint64 requestId, const QString &eventId);
    void verificationDismissed();
    void authenticationExpired();
    void sessionChanged(quint64 revision);

private:
    struct AuthClient;
    std::unique_ptr<AuthClient> m_auth;
    struct CatalogClient;
    std::unique_ptr<CatalogClient> m_catalog;
    struct CloudClient;
    std::unique_ptr<CloudClient> m_cloud;
    struct SocialClient;
    std::unique_ptr<SocialClient> m_social;
    struct LibraryClient;
    std::unique_ptr<LibraryClient> m_library;
    struct MediaClient;
    std::unique_ptr<MediaClient> m_media;

    friend class ApiClientTestAccess;
    struct Pending
    {
        quint64 revision = 0;
        quint64 logicalId = 0;
        QElapsedTimer attemptTimer;
        QJsonObject request;
        bool automaticVerification = true;
        bool replayAfterVerification = true;
        bool retried = false;
        bool externalVerification = false;
        bool sessionBound = false;
        QString eventId;
        quint64 verifiesRequest = 0;
    };
    quint64 submit(QJsonObject request, bool includeIdentity = true, RequestOptions options = {});
    quint64 submitPrepared(Pending pending);
    void presentNextVerification();
    void retryVerifiedEvent(const QString &eventId);
    static void callback(void *context, const EchoResult *result) noexcept;
    void deliver(quint64 id, quint64 revision, int status, const QByteArray &payload);
    EchoRuntime *m_runtime = nullptr;
    QObject *m_dispatcher = nullptr;
    QThread *m_closer = nullptr;
    QHash<quint64, Pending> m_pending;
    QHash<quint64, Pending> m_suspended;
    QHash<quint64, Pending> m_loginConfirmation;
    QList<quint64> m_verificationQueue;
    quint64 m_externalVerificationSequence = (quint64(1) << 63);
    quint64 m_activeVerification = 0;
    quint64 m_revision = 1;
    bool m_closing = false;
    DeviceIdentity m_device;
    DeviceIdentity m_independentDevice = DeviceIdentity::temporary();
    quint64 m_userId = 0;
    QString m_token;
    QString m_t1;
};
} // namespace EchoMusic
