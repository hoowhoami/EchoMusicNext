#pragma once
#include "PlaylistDetailContext.h"
#include "SongMetadataService.h"
#include "PlaylistOrderModel.h"
#include "PlaylistEditModel.h"
#include "LibraryController.h"
#include <QHash>
#include <QPointer>
namespace EchoMusic
{
/// Route contexts follow the router's bounded cache. At most one additional
/// context reads a playlist for an explicit navigation-free playback intent.
class PlaylistDetailService final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by the application")
    Q_PROPERTY(EchoMusic::PlaylistEditModel *editor READ editor CONSTANT)
    Q_PROPERTY(EchoMusic::PlaylistOrderModel *order READ order CONSTANT)
    Q_PROPERTY(EchoMusic::LibraryController *libraryController READ libraryController CONSTANT)
public:
    /// Explicit UI composition; construction alone does not register pages.
    void registerRoutes(RouteController &router);
    void attachLibraryController(LibraryController &);
    LibraryController *libraryController() const
    {
        return m_controller;
    }
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
    friend class PlaylistDetailServiceTestAccess;
    void playPlaylist(const QString &resourceId, quint64 cloudId);
    void settlePlayback();
    void cancelPlayback(bool replacing = false);
    CommentAuthorService *m_authors;
    LibraryCollectionService *m_collections = nullptr;
    ApiClient &m_api;
    PlaybackService &m_playback;
    UserPlaylistModel &m_library;
    PlaylistMutationService &m_mutations;
    PlaylistOrderModel &m_order;
    PlaylistEditModel m_editor;
    SongMetadataService &m_metadata;
    RouteController &m_router;
    QPointer<LibraryController> m_controller;
    QPointer<PlaylistDetailContext> m_pendingPlayback;
    quint64 m_pendingPlayListId = 0;
    QString m_pendingResource;
    QString m_pendingPlaySequence;
    QHash<RouteContext *, QPointer<PlaylistDetailContext>> m_contexts;
    bool m_network, m_closed = false;
};
} // namespace EchoMusic
