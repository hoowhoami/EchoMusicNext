#include "AlbumDetailService.h"
#include <QQmlEngine>
namespace EchoMusic
{
AlbumDetailService::AlbumDetailService(ApiClient &api, PlaybackService &playback, RouteController &,
                                       bool networkEnabled, QObject *parent,
                                       CommentAuthorService *authors)
    : QObject(parent), m_authors(authors), m_api(api), m_playback(playback),
      m_network(networkEnabled)
{
    QQmlEngine::setObjectOwnership(this, QQmlEngine::CppOwnership);
}
AlbumDetailService::~AlbumDetailService()
{
    shutdown();
    for (const auto &context : std::as_const(m_contexts))
        delete context.data();
}
AlbumDetailContext *AlbumDetailService::contextFor(RouteContext *route)
{
    if (m_closed || !route || route->name() != "album-detail")
        return nullptr;
    if (auto *context = m_contexts.value(route).data())
        return context;
    auto *context = new AlbumDetailContext(m_api, m_playback, *route, m_network, m_authors);
    if (m_collections) {
        context->attachCollections(*m_collections);
        m_collections->ensureLoaded();
    }
    m_contexts.insert(route, context);
    connect(context, &AlbumDetailContext::notice, this, &AlbumDetailService::notice);
    connect(context->comments(), &CommentsController::notice, this, &AlbumDetailService::notice);
    connect(context->comments(), &CommentsController::readFailed, this,
            [this](const QString &message) { emit notice(message, true); });
    connect(route, &QObject::destroyed, this, [this, route] { m_contexts.remove(route); });
    return context;
}
void AlbumDetailService::shutdown()
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
void AlbumDetailService::registerRoutes(RouteController &router)
{
    router.registerPage({"album-detail",
                         "/main/album/:id",
                         QUrl("qrc:/qt/qml/EchoMusic/Albums/AlbumDetailPage.qml"),
                         {"tab", "locateTrack", "locateToken"},
                         true,
                         true,
                         false,
                         {{"service", QVariant::fromValue(this)}}});
}
} // namespace EchoMusic
