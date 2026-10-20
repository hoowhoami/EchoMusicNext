#pragma once
#include "ApiClient.h"
#include "RouteController.h"
#include "SocialUserModel.h"
#include "SongCatalogActions.h"
#include "PlaylistOrderModel.h"
#include "SongCatalogModel.h"
#include "SongMetadataService.h"
#include "UserPlaylistModel.h"
#include <QHash>
#include <QJsonArray>
#include <optional>
namespace EchoMusic
{
class PlaybackService;
class FavoritesServiceTestAccess;
/// Favorites data owner: automatically page liked songs in global server order,
/// split followed singers and users by singerid / iden_type, project favorite
/// albums from library rows with source_code == 2, and retain collected videos.
class FavoritesService final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by the application")
    Q_PROPERTY(EchoMusic::SongCatalogModel *songs READ songs CONSTANT)
    Q_PROPERTY(EchoMusic::SongCatalogActions *actions READ actions CONSTANT)
    Q_PROPERTY(EchoMusic::SocialUserModel *singers READ singers CONSTANT)
    Q_PROPERTY(EchoMusic::SocialUserModel *users READ users CONSTANT)
    Q_PROPERTY(EchoMusic::SongCatalogModel *albums READ albums CONSTANT)
    Q_PROPERTY(EchoMusic::SongCatalogModel *videos READ videos CONSTANT)
    Q_PROPERTY(EchoMusic::PlaybackService *playback READ playback CONSTANT)
    Q_PROPERTY(EchoMusic::PlaylistOrderModel *order READ order CONSTANT)
    Q_PROPERTY(int tab READ tab NOTIFY changed)
    Q_PROPERTY(bool canAdjustSongOrder READ canAdjustSongOrder NOTIFY changed)
    Q_PROPERTY(quint64 likedListId READ likedListId NOTIFY changed)
    Q_PROPERTY(QString likedTitle READ likedTitle NOTIFY changed)
    Q_PROPERTY(bool loading READ loading NOTIFY changed)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY changed)
    Q_PROPERTY(bool hasMore READ hasMore NOTIFY changed)
    Q_PROPERTY(bool signedIn READ signedIn NOTIFY changed)
public:
    /// Explicit UI composition; construction alone does not register pages.
    void registerRoutes(RouteController &router);
    FavoritesService(ApiClient &, PlaybackService &, UserPlaylistModel &, PlaylistOrderModel &,
                     SongMetadataService &, RouteController &, bool networkEnabled,
                     QObject *parent = nullptr);
    ~FavoritesService() override;
    SongCatalogModel *songs()
    {
        return &m_songs;
    }
    SongCatalogActions *actions()
    {
        return &m_actions;
    }
    SocialUserModel *singers()
    {
        return &m_singers;
    }
    SocialUserModel *users()
    {
        return &m_users;
    }
    PlaylistOrderModel *order()
    {
        return &m_order;
    }
    /// Reordering requires an authenticated owner and a positive cloud list ID.
    bool canAdjustSongOrder() const;
    quint64 likedListId() const;
    QString likedTitle() const;
    SongCatalogModel *albums()
    {
        return &m_albums;
    }
    SongCatalogModel *videos()
    {
        return &m_videos;
    }
    PlaybackService *playback()
    {
        return &m_playback;
    }
    int tab() const
    {
        return m_tab;
    }
    bool loading() const;
    QString errorMessage() const
    {
        return m_error;
    }
    bool hasMore() const;
    bool signedIn() const
    {
        return m_api.currentAccountUserId() != 0;
    }
    /// Route query tabs are songs, singers, users, albums and videos.
    /// Tab selection is reflected in the route.
    Q_INVOKABLE void selectTab(int tab);
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void loadMore();
    /// Nickname search filters only the singers model; the users tab has no search box.
    Q_INVOKABLE void setSingerQuery(const QString &query);
    void invalidateVideos();
    void invalidateFollow();
    void shutdown();

signals:
    void changed();

private:
    friend class FavoritesServiceTestAccess;
    enum class PendingKind
    {
        Follow,
        LikedSongs,
        Video
    };
    struct Pending
    {
        PendingKind kind;
        quint64 session;
    };
    struct MetadataPending
    {
        quint64 session, generation;
    };
    void completeSongs(const QJsonArray &entries);
    void resumeMetadata();
    void cancelMetadata();
    void acceptMetadata(quint64 ticket, const QJsonArray &presentations);
    void publishCover();
    void activate(RouteContext *context);
    void reset();
    void loadTab();
    void requestFollow();
    void requestLikedSongs();
    void requestLikedPage();
    void requestVideos();
    void projectAlbums();
    void projectFollow();
    bool hasPending(PendingKind kind) const;
    void cancelAll();
    void accept(quint64 id, const QByteArray &bytes);
    void fail(quint64 id, const QByteArray &bytes);
    static const QStringList &tabs();
    ApiClient &m_api;
    PlaybackService &m_playback;
    UserPlaylistModel &m_library;
    PlaylistOrderModel &m_order;
    SongMetadataService &m_metadata;
    RouteController &m_router;
    SongCatalogModel m_songs, m_albums, m_videos;
    SongCatalogActions m_actions;
    SocialUserModel m_singers, m_users;
    QHash<quint64, Pending> m_pending;
    QHash<quint64, MetadataPending> m_metadataRequests;
    QSet<QString> m_metadataScheduled;
    quint64 m_songGeneration = 0;
    std::optional<quint64> m_songRevision;
    bool m_songCoverConsistent = true;
    QJsonArray m_videoRows;
    QJsonArray m_followRows;
    QJsonArray m_songRows;
    QString m_singerQuery;
    quint64 m_likedId = 0, m_songTotal = 0, m_songReceived = 0;
    quint32 m_songPage = 1;
    bool m_songHasMore = false, m_keepSongsDuringRefresh = false;
    quint64 m_videoTotal = 0, m_videoReceived = 0;
    int m_tab = 0;
    QString m_instance, m_error;
    bool m_network = false, m_active = false, m_closed = false;
    bool m_likedLoaded = false, m_followLoaded = false, m_videosLoaded = false;
    bool m_videoExhausted = false, m_awaitingLibrary = false;
};
} // namespace EchoMusic
