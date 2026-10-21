#pragma once
#include "LibraryCollectionService.h"
#include "ApiClient.h"
#include "CommentsController.h"
#include "SongCatalogActions.h"
#include "SongCatalogModel.h"
#include "UserPlaylistModel.h"
#include "PlaylistMutationService.h"
#include "RouteController.h"
#include "SongMetadataService.h"
#include <QJsonObject>
#include <QMap>
namespace EchoMusic
{
class PlaylistDetailContextTestAccess;
/// One router-owned resource lifetime. Hidden pages cancel transport; cached
/// data belongs to this object, and eviction destroys requests and its model.
class PlaylistDetailContext final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by a mounted route")
    Q_PROPERTY(EchoMusic::CommentsController *comments READ comments CONSTANT)
    Q_PROPERTY(EchoMusic::SongCatalogModel *songs READ songs CONSTANT)
    Q_PROPERTY(EchoMusic::SongCatalogActions *actions READ actions CONSTANT)
    Q_PROPERTY(EchoMusic::PlaybackService *playback READ playback CONSTANT)
    Q_PROPERTY(QVariantMap detail READ detail NOTIFY changed)
    Q_PROPERTY(bool loading READ loading NOTIFY changed)
    Q_PROPERTY(bool complete READ complete NOTIFY changed)
    Q_PROPERTY(bool owned READ owned NOTIFY changed)
    Q_PROPERTY(quint64 orderListId READ orderListId NOTIFY changed)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY changed)
    Q_PROPERTY(quint64 total READ total NOTIFY changed)
    Q_PROPERTY(bool collectionAvailable READ collectionAvailable NOTIFY changed)
    Q_PROPERTY(bool collected READ collected NOTIFY changed)
    Q_PROPERTY(bool collectionBusy READ collectionBusy NOTIFY changed)
public:
    bool collectionAvailable() const
    {
        return m_collections && m_collections->available(false, m_detail);
    }
    bool collected() const
    {
        return m_collections && m_collections->collected(false, m_detail);
    }
    bool collectionBusy() const
    {
        return !m_collections || m_collections->busy();
    }
    void attachCollections(LibraryCollectionService &service)
    {
        m_collections = &service;
        connect(&service, &LibraryCollectionService::changed, this,
                &PlaylistDetailContext::changed);
    }
    Q_INVOKABLE void toggleCollection()
    {
        if (m_collections && m_route.active())
            m_collections->toggle(false, m_detail);
    }
    PlaylistDetailContext(ApiClient &, PlaybackService &, UserPlaylistModel &,
                          PlaylistMutationService &, SongMetadataService &, RouteContext &,
                          bool networkEnabled, CommentAuthorService *authors = nullptr);
    ~PlaylistDetailContext() override;
    SongCatalogModel *songs()
    {
        return &m_songs;
    }
    SongCatalogActions *actions()
    {
        return &m_actions;
    }
    PlaybackService *playback()
    {
        return &m_playback;
    }
    QVariantMap detail() const
    {
        if (m_detail.isEmpty())
            return {};
        auto value = m_detail.toVariantMap();
        value["cover_url"] =
            m_library.coverFor(m_detail["global_id"].toString(), m_detail["cover_url"].toString());
        return value;
    }
    bool loading() const
    {
        return !m_requests.isEmpty() || m_resolving;
    }
    bool complete() const
    {
        return m_complete;
    }
    bool owned() const
    {
        return m_cloud.has_value();
    }
    /// Resolve the current account's writable listid from its canonical library.
    /// A public collection ID or the creator's listid is never a write target.
    quint64 orderListId() const;
    QString errorMessage() const
    {
        return m_error;
    }
    quint64 total() const
    {
        return m_total;
    }
    CommentsController *comments()
    {
        return &m_comments;
    }
    Q_INVOKABLE void activateComments();
    Q_INVOKABLE void reload();
    /// 右键菜单「从歌单删除」：仅所有权云歌单，file_id 走共享单写者。
    Q_INVOKABLE bool removeSong(const QString &id);
    void shutdown();
signals:
    void notice(const QString &message, bool warning);
    void changed();

private:
    friend class PlaylistDetailContextTestAccess;
    struct Pending
    {
        quint32 page;
        quint64 session, generation;
    };
    void cancel();
    void cancelPageRequests();
    void activate();
    void resolveSource();
    void requestPage(quint32 page);
    void accept(quint64 id, const QByteArray &bytes);
    void fail(quint64 id, const QByteArray &bytes);
    void failMessage(const QString &message);
    void consumeBatch();
    void completePage(const QJsonArray &songs);
    void publishCover();
    LibraryCollectionService *m_collections = nullptr;
    ApiClient &m_api;
    PlaybackService &m_playback;
    UserPlaylistModel &m_library;
    SongMetadataService &m_metadata;
    PlaylistMutationService &m_mutations;
    RouteContext &m_route;
    SongCatalogModel m_songs;
    SongCatalogActions m_actions;
    CommentsController m_comments;
    QHash<quint64, Pending> m_requests;
    QMap<quint32, QJsonObject> m_pages;
    QMap<quint32, QString> m_pageErrors;
    QJsonObject m_detail;
    QList<QJsonObject> m_cloudEntries;
    QSet<QString> m_cloudEntryIds;
    QHash<quint64, Pending> m_metadataRequests;
    quint64 m_received = 0;
    QString m_error, m_publicId;
    std::optional<ApiClient::CloudPlaylistId> m_cloud;
    quint64 m_generation = 0, m_total = 0;
    std::optional<quint64> m_revision;
    quint32 m_nextPage = 1, m_pageSize = 200;
    bool m_complete = false, m_resolving = false, m_closed = false, m_network,
         m_metadataDirty = false;
};
} // namespace EchoMusic
