#pragma once
#include "ApiClient.h"

namespace EchoMusic
{
// Typed domain adapters share ApiClient transport, request IDs, immutable
// identity snapshots, verification and shutdown. They own no second session.
struct ApiClient::AuthClient
{
    explicit AuthClient(ApiClient &owner) : api(owner) {}
    quint64 refreshLogin();
    quint64 registerDevice();
    quint64 createQrLogin();
    quint64 checkQrLogin(const QString &key);
    quint64 createProviderQr(QrProvider provider);
    quint64 checkProviderQr(QrProvider provider, const QJsonObject &session);
    quint64 exchangeWechatCode(const QString &code);
    quint64 finishProviderLogin(QrProvider provider, const QJsonObject &authorization);
    quint64 sendSmsCode(const QString &mobile);
    quint64 smsLogin(const QString &mobile, const QString &code,
                     std::optional<quint64> selectedUser);
    quint64 passwordLogin(const QString &username, const QString &password);
    quint64 loginDevices();
    quint64 kickLoginDevice(const LoginDeviceTarget &target);
    quint64 userVerify(RequestOptions options);

private:
    ApiClient &api;
};

struct ApiClient::CatalogClient
{
    explicit CatalogClient(ApiClient &owner) : api(owner) {}
    quint64 searchHot(RequestOptions options);
    quint64 searchDefault(RequestOptions options);
    quint64 searchSuggest(const QString &keyword, RequestOptions options);
    quint64 search(SearchKind kind, const QString &keyword, quint32 page, quint32 pageSize,
                   RequestOptions options);
    quint64 recommendedPlaylists(const QString &categoryId);
    quint64 editorPlaylists();
    quint64 styleRecommendations(const QStringList &tagIds);
    quint64 dailyRecommendations();
    quint64 freeListenSongs();
    quint64 discoverRecommendations(quint32 pageSize);
    quint64 newSongCatalog();
    quint64 newAlbumCatalog();
    quint64 playlistCategories();
    quint64 artistDirectory(quint32 sex, quint32 category, quint32 musician);
    quint64 recommendedRanks();
    quint64 rankCatalog();
    quint64 rankSongs(quint64 rankId);
    quint64 albumDetail(quint64 albumId, RequestOptions options);
    quint64 albumSongs(quint64 albumId, quint32 page, quint32 pageSize, RequestOptions options);
    quint64 artistDetail(quint64 artistId, RequestOptions options);
    quint64 artistSongs(quint64 artistId, quint32 page, quint32 pageSize, const QString &sort,
                        RequestOptions options);
    quint64 artistAlbums(quint64 artistId, quint32 page, quint32 pageSize, const QString &sort,
                         RequestOptions options);

private:
    ApiClient &api;
};

struct ApiClient::CloudClient
{
    explicit CloudClient(ApiClient &owner) : api(owner) {}
    quint64 userCloud(quint32 page, quint32 pageSize);
    quint64 cloudMatch(const QString &hash);
    quint64 cloudUpload(CloudUploadStage stage, QJsonObject parameters);
    quint64 userCloudDelete(const QList<CloudDeleteTarget> &targets);

private:
    ApiClient &api;
};

struct ApiClient::SocialClient
{
    explicit SocialClient(ApiClient &owner) : api(owner) {}
    quint64 comments(const CommentQuery &q);
    quint64 sendComment(const CommentWrite &w);
    quint64 replyComment(const CommentWrite &w);
    quint64 deleteComment(const CommentWrite &w);
    quint64 commentCount(const QString &hash);
    quint64 favoriteCount(quint64 mix);
    quint64 collectVideo(quint64 id, bool collect);
    quint64 commentAuthorProducts(const QList<quint64> &ids);
    quint64 songBarrage(const QString &hash, quint32 size);
    quint64 sendSongBarrage(const QString &hash, const QString &name, const QString &content);
    quint64 videoBarrage(const QString &hash, quint32 size);
    quint64 sendVideoBarrage(const QString &hash, const QString &name, const QString &content);
    quint64 userDetail(RequestOptions options);
    quint64 userInfo();
    quint64 userVipDetail();
    quint64 userGradeInfo(std::optional<quint64> userId, bool automaticVerification);
    quint64 socialUsers(SocialList list);
    quint64 followUser(quint64 id, bool following);
    quint64 followArtist(quint64 id, bool following);
    quint64 socialMessages(quint64 target, const QString &tag);
    quint64 sendSocialMessage(quint64 target, const QString &tag, const QString &text,
                              const QString &nickname);
    quint64 updateUserProfile(const ProfilePatch &patch);
    quint64 uploadUserAvatar(const QByteArray &image);
    quint64 updateUserAvatar(const QString &fileName);
    quint64 contentBlacklist(BlacklistKind kind, quint32 page, quint32 pageSize);
    quint64 editContentBlacklist(BlacklistKind kind, const QString &key, const QString &name,
                                 const QString &mixSongId, bool remove);
    quint64 listeningPreferences();
    quint64 updateListeningPreferences(const PreferencePatch &patch);
    quint64 userVideoCollect(quint32 page, quint32 pageSize);

private:
    ApiClient &api;
};

struct ApiClient::LibraryClient
{
    explicit LibraryClient(ApiClient &owner) : api(owner) {}
    quint64 userPlaylists(quint32 page, quint32 pageSize);
    quint64 importPlaylistLink(const QString &text);
    quint64 submitPlaylistImportImage(const QString &serial, const QByteArray &image);
    quint64 importPlaylistScreenshot(const QString &serial, CloudPlaylistId id,
                                     const QString &title);
    quint64 playlistImportStatus(quint64 task);
    quint64 playlistImportResult(CloudPlaylistId id, quint32 page, quint32 size);
    quint64 purchasedSongs(quint32 page, quint32 pageSize);
    quint64 purchasedAlbums(quint32 page, quint32 pageSize);
    quint64 playlistDetails(const QList<PublicCollectionId> &ids, RequestOptions options);
    quint64 publicPlaylistTracks(const PublicCollectionId &id, quint32 page, quint32 pageSize,
                                 RequestOptions options);
    quint64 cloudPlaylistTracks(CloudPlaylistId id, quint32 page, quint32 pageSize);
    quint64 createPlaylist(const QString &title, bool isPrivate);
    quint64 collectPlaylist(const CollectionTarget &target);
    quint64 addPlaylistTracks(CloudPlaylistId id, const QList<PlaylistTrackInput> &tracks);
    quint64 removePlaylistTracks(CloudPlaylistId id, const QList<PlaylistTrackFileId> &files);
    quint64 sortPlaylistTracks(CloudPlaylistId id, quint64 revision,
                               const QList<PlaylistTrackPosition> &positions);
    quint64 sortPlaylists(quint64 totalRevision, const QList<PlaylistPosition> &positions);
    quint64 uploadPlaylistCover(const QByteArray &png);
    quint64 updatePlaylist(const PlaylistUpdate &update);
    quint64 deletePlaylist(CloudPlaylistId id);

private:
    ApiClient &api;
};

struct ApiClient::MediaClient
{
    explicit MediaClient(ApiClient &owner) : api(owner) {}
    quint64 recognizeAudio(const QByteArray &pcm);
    quint64 personalFm(const FmRequest &request);
    quint64 songClimax(const QString &hash);
    quint64 audioImages(const QList<AudioImageResource> &resources, quint32 count);
    quint64 albumDynamicCover(quint64 audio, quint64 album);
    quint64 songRanking(quint64 mix, bool details);
    quint64 songMv(quint64 mix);
    quint64 videoDetail(const QString &id);
    quint64 videoPrivilege(const QString &hash);
    quint64 videoUrl(const QString &hash);
    quint64 reportListeningEvent(ListeningEvent event, quint64 songId, qint64 durationMs,
                                 bool completed);
    quint64 reportListeningProgress(quint64 baselineSeconds, quint64 incrementSeconds);
    quint64 songMetadata(const QList<quint64> &ids, RequestOptions options);
    quint64 audioEffects(EffectCategory category, quint32 page, quint32 pageSize, quint32 sort,
                         quint64 brand);
    quint64 commonHeadphoneEffect();
    quint64 lyricCandidates(const QString &hash, quint64 songId, quint64 durationMs,
                            bool allCandidates);
    quint64 lyric(const QString &id, const QString &accessKey);
    quint64 lyricCandidatesByKeywords(const QString &keywords, quint64 durationMs,
                                      bool allCandidates);
    quint64 playbackSource(const QString &hash, quint64 albumId, quint64 songId,
                           PlaybackQuality quality, const QString &authorization,
                           RequestOptions options, SoundEffect effect, const QString &originPage);
    quint64 songPrivileges(const QList<PrivilegeResource> &resources, RequestOptions options);

private:
    ApiClient &api;
};
} // namespace EchoMusic
