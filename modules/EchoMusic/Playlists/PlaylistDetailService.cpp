#include <QQmlEngine>
#include "PlaylistDetailService.h"
namespace EchoMusic
{
PlaylistDetailService::PlaylistDetailService(
    ApiClient &api, PlaybackService &playback, UserPlaylistModel &library,
    PlaylistMutationService &mutations, PlaylistOrderModel &order, SongMetadataService &metadata,
    RouteController &, bool networkEnabled, QObject *parent, CommentAuthorService *authors)
    : QObject(parent), m_authors(authors), m_api(api), m_playback(playback), m_library(library),
      m_mutations(mutations), m_order(order), m_metadata(metadata), m_network(networkEnabled)
{
    // QML borrows this application-owned object, including explicit invokable
    // returns. GC and engine teardown must never delete a C++ member/context.
    QQmlEngine::setObjectOwnership(this, QQmlEngine::CppOwnership);
}
PlaylistDetailService::~PlaylistDetailService()
{
    shutdown();
    // RouteController may outlive the application services at stack teardown.
    // Retire its child contexts while their borrowed service references live.
    for (const auto &context : std::as_const(m_contexts))
        delete context.data();
    m_contexts.clear();
}
PlaylistDetailContext *PlaylistDetailService::contextFor(RouteContext *route)
{
    if (m_closed || !route || route->name() != "playlist-detail")
        return nullptr;
    if (auto *existing = m_contexts.value(route).data())
        return existing;
    auto *context = new PlaylistDetailContext(m_api, m_playback, m_library, m_mutations, m_metadata,
                                              *route, m_network, m_authors);
    if (m_collections) {
        context->attachCollections(*m_collections);
        m_collections->ensureLoaded();
    }
    m_contexts.insert(route, context);
    connect(context->comments(), &CommentsController::notice, this, &PlaylistDetailService::notice);
    connect(route, &QObject::destroyed, this, [this, route] { m_contexts.remove(route); });
    return context;
}
void PlaylistDetailService::shutdown()
{
    if (m_closed)
        return;
    m_closed = true;
    for (const auto &context : std::as_const(m_contexts))
        if (context)
            context->shutdown();
}
} // namespace EchoMusic

namespace EchoMusic
{
void PlaylistDetailService::registerRoutes(RouteController &router)
{
    router.registerPage({"playlist-detail",
                         "/main/playlist/:id",
                         QUrl("qrc:/qt/qml/EchoMusic/Playlists/PlaylistDetailPage.qml"),
                         {"tab"},
                         true,
                         true,
                         false,
                         {{"service", QVariant::fromValue(this)}}});
}
} // namespace EchoMusic
