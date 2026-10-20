#include <QQmlEngine>
#include "ArtistDetailService.h"
namespace EchoMusic
{
ArtistDetailService::ArtistDetailService(ApiClient &api, PlaybackService &playback,
                                         RouteController &, ShellSettings &settings,
                                         bool networkEnabled, QObject *parent)
    : QObject(parent), m_api(api), m_playback(playback), m_settings(settings),
      m_network(networkEnabled)
{
    QQmlEngine::setObjectOwnership(this, QQmlEngine::CppOwnership);
}
ArtistDetailService::~ArtistDetailService()
{
    shutdown();
    for (const auto &context : std::as_const(m_contexts))
        delete context.data();
    m_contexts.clear();
}
ArtistDetailContext *ArtistDetailService::contextFor(RouteContext *route)
{
    if (m_closed || !route || route->name() != "artist-detail")
        return nullptr;
    if (auto *existing = m_contexts.value(route).data())
        return existing;
    if (m_profile)
        m_profile->ensureArtistFollows();
    auto *context = new ArtistDetailContext(m_api, m_playback, *route, m_settings, m_network);
    if (m_profile)
        context->attachProfile(*m_profile);
    m_contexts.insert(route, context);
    connect(route, &QObject::destroyed, this, [this, route] { m_contexts.remove(route); });
    return context;
}
void ArtistDetailService::shutdown()
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
void ArtistDetailService::registerRoutes(RouteController &router)
{
    router.registerPage({"artist-detail",
                         "/main/artist/:id",
                         QUrl("qrc:/qt/qml/EchoMusic/Artists/ArtistDetailPage.qml"),
                         {"tab"},
                         true,
                         true,
                         false,
                         {{"service", QVariant::fromValue(this)}}});
}
} // namespace EchoMusic
