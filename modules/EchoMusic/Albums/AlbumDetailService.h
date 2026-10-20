#pragma once
#include "AlbumDetailContext.h"
#include <QPointer>
namespace EchoMusic
{
class AlbumDetailService final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by the application")
public:
    /// Explicit UI composition; construction alone does not register pages.
    void registerRoutes(RouteController &router);
    void attachCollections(LibraryCollectionService &service)
    {
        m_collections = &service;
    }
    AlbumDetailService(ApiClient &, PlaybackService &, RouteController &,
                       bool networkEnabled = true, QObject *parent = nullptr,
                       CommentAuthorService *authors = nullptr);
    ~AlbumDetailService() override;
    Q_INVOKABLE EchoMusic::AlbumDetailContext *contextFor(EchoMusic::RouteContext *route);
    void shutdown();

signals:
    void notice(const QString &message, bool warning);

private:
    CommentAuthorService *m_authors;
    LibraryCollectionService *m_collections = nullptr;
    ApiClient &m_api;
    PlaybackService &m_playback;
    QHash<RouteContext *, QPointer<AlbumDetailContext>> m_contexts;
    bool m_network, m_closed = false;
};
} // namespace EchoMusic
