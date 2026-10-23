#pragma once
#include "PlaylistDetailContext.h"
#include "SongMetadataService.h"
#include "PlaylistOrderModel.h"
#include "PlaylistEditModel.h"
#include <QHash>
#include <QPointer>
namespace EchoMusic
{
/// Factory follows the router's bounded cache. Contexts are children of their
/// route; there is no second unbounded resource cache or QML-owned network task.
class PlaylistDetailService final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by the application")
    Q_PROPERTY(EchoMusic::PlaylistEditModel *editor READ editor CONSTANT)
    Q_PROPERTY(EchoMusic::PlaylistOrderModel *order READ order CONSTANT)
public:
    /// Explicit UI composition; construction alone does not register pages.
    void registerRoutes(RouteController &router);
    void attachCollections(LibraryCollectionService &service)
    {
        m_collections = &service;
    }
    PlaylistDetailService(ApiClient &, PlaybackService &, UserPlaylistModel &,
                          PlaylistMutationService &, PlaylistOrderModel &, SongMetadataService &,
                          RouteController &, bool networkEnabled = true, QObject *parent = nullptr,
                          CommentAuthorService *authors = nullptr);
    ~PlaylistDetailService() override;
    Q_INVOKABLE EchoMusic::PlaylistDetailContext *contextFor(EchoMusic::RouteContext *route);
    PlaylistEditModel *editor()
    {
        return &m_editor;
    }
    PlaylistOrderModel *order()
    {
        return &m_order;
    }
    void shutdown();

signals:
    void notice(const QString &message, bool warning);

private:
    CommentAuthorService *m_authors;
    LibraryCollectionService *m_collections = nullptr;
    ApiClient &m_api;
    PlaybackService &m_playback;
    UserPlaylistModel &m_library;
    PlaylistMutationService &m_mutations;
    PlaylistOrderModel &m_order;
    PlaylistEditModel m_editor;
    SongMetadataService &m_metadata;
    QHash<RouteContext *, QPointer<PlaylistDetailContext>> m_contexts;
    bool m_network, m_closed = false;
};
} // namespace EchoMusic
