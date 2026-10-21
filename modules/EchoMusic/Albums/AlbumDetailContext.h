#pragma once
#include "LibraryCollectionService.h"
#include "ApiClient.h"
#include "CommentsController.h"
#include "RouteController.h"
#include "SongCatalogActions.h"
#include "SongCatalogModel.h"
#include <QMap>

namespace EchoMusic
{
/// One album in the router's bounded cache. Only Rust domain values enter the
/// model; transport, pagination and session lifetimes remain on the GUI thread.
class AlbumDetailContext final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by a mounted route")
    Q_PROPERTY(EchoMusic::CommentsController *comments READ comments CONSTANT)
    Q_PROPERTY(EchoMusic::SongCatalogModel *songs READ songs CONSTANT)
    Q_PROPERTY(EchoMusic::SongCatalogActions *actions READ actions CONSTANT)
    Q_PROPERTY(QVariantMap detail READ detail NOTIFY changed)
    Q_PROPERTY(bool detailLoading READ detailLoading NOTIFY changed)
    Q_PROPERTY(bool songsLoading READ songsLoading NOTIFY changed)
    Q_PROPERTY(bool complete READ complete NOTIFY changed)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY changed)
    Q_PROPERTY(bool collectionAvailable READ collectionAvailable NOTIFY changed)
    Q_PROPERTY(bool collected READ collected NOTIFY changed)
    Q_PROPERTY(bool collectionBusy READ collectionBusy NOTIFY changed)
public:
    bool collectionAvailable() const
    {
        return m_collections && m_collections->available(true, m_detail);
    }
    bool collected() const
    {
        return m_collections && m_collections->collected(true, m_detail);
    }
    bool collectionBusy() const
    {
        return !m_collections || m_collections->busy();
    }
    void attachCollections(LibraryCollectionService &service)
    {
        m_collections = &service;
        connect(&service, &LibraryCollectionService::changed, this, &AlbumDetailContext::changed);
    }
    Q_INVOKABLE void toggleCollection()
    {
        if (m_collections && m_route.active())
            m_collections->toggle(true, m_detail);
    }
    AlbumDetailContext(ApiClient &, PlaybackService &, RouteContext &, bool networkEnabled,
                       CommentAuthorService *authors = nullptr);
    ~AlbumDetailContext() override;
    SongCatalogModel *songs()
    {
        return &m_songs;
    }
    SongCatalogActions *actions()
    {
        return &m_actions;
    }
    QVariantMap detail() const
    {
        return m_detail.toVariantMap();
    }
    bool detailLoading() const;
    bool songsLoading() const;
    bool complete() const
    {
        return m_complete;
    }
    QString errorMessage() const
    {
        return m_detail.isEmpty() ? m_detailError : m_songError;
    }
    CommentsController *comments()
    {
        return &m_comments;
    }
    Q_INVOKABLE void activateComments();
    Q_INVOKABLE void reload();
    void shutdown();
signals:
    void notice(const QString &message, bool warning);
    void changed();

private:
    friend class AlbumDetailContextTestAccess;
    struct Pending
    {
        quint32 page;
        quint64 session, generation;
    };
    void activate();
    void cancel();
    void cancelSongRequests();
    void requestPage(quint32 page);
    void requestBatch();
    void consumeBatch();
    void accept(quint64 id, const QByteArray &bytes);
    void fail(quint64 id, const QByteArray &bytes);
    void finishPage(quint32 page, const QJsonObject &value, const QString &error = {});
    LibraryCollectionService *m_collections = nullptr;
    ApiClient &m_api;
    RouteContext &m_route;
    SongCatalogModel m_songs;
    SongCatalogActions m_actions;
    CommentsController m_comments;
    QJsonObject m_detail;
    QHash<quint64, Pending> m_requests;
    QMap<quint32, QJsonObject> m_pages;
    QMap<quint32, QString> m_errors;
    // A refresh of an already complete album is committed only after all pages
    // succeed, so a failed refresh cannot truncate a playable cached catalog.
    QJsonArray m_refreshSongs;
    quint64 m_generation = 0;
    quint32 m_nextPage = 1, m_pageSize = 30;
    QString m_detailError, m_songError;
    bool m_network, m_closed = false, m_complete = false, m_refreshing = false;
};
} // namespace EchoMusic
