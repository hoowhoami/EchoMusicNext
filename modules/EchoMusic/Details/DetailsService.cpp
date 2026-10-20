#include "DetailsService.h"
#include "NetworkPreferences.h"
#include "ShellController.h"
namespace EchoMusic
{
DetailsService::DetailsService(ApiClient &a, PlaybackService &p, RouteController &, bool network,
                               QObject *parent, CommentAuthorService *authors,
                               WindowController *window)
    : QObject(parent), m_window(window), m_authors(authors), m_api(a), m_playback(p),
      m_collection(a, network, this), m_discussion(a, p, network, this, authors), m_network(network)
{
    QQmlEngine::setObjectOwnership(this, QQmlEngine::CppOwnership);
    connect(&a, &ApiClient::sessionChanged, this, [this] { m_entryTrack.reset(); });
    connect(&m_discussion, &PlaybackDiscussion::notice, this, &DetailsService::notice);
    connect(&m_collection, &VideoCollectionService::notice, this, &DetailsService::notice);
}
DetailsService::~DetailsService()
{
    shutdown();
    for (auto c : std::as_const(m_songs))
        delete c.data();
    for (auto c : std::as_const(m_videos))
        delete c.data();
}
SongDetailContext *DetailsService::songFor(RouteContext *r)
{
    if (m_closed || !r || r->name() != "song-detail")
        return nullptr;
    if (auto *c = m_songs.value(r).data())
        return c;
    auto *c = new SongDetailContext(m_api, m_playback, *r, m_network, m_authors);
    if (m_entryTrack && m_entrySession == m_api.sessionRevision() &&
        (m_entryTrack->songId ? QString::number(m_entryTrack->songId) : m_entryTrack->id) ==
            r->resourceId()) {
        c->adoptEntryTrack(*m_entryTrack);
        m_entryTrack.reset();
    }
    m_songs.insert(r, c);
    connect(c, &SongDetailContext::notice, this, &DetailsService::notice);
    connect(r, &QObject::destroyed, this, [this, r] { m_songs.remove(r); });
    return c;
}
MvDetailContext *DetailsService::mvFor(RouteContext *r)
{
    if (m_closed || !r || r->name() != "mv-detail")
        return nullptr;
    if (auto *c = m_videos.value(r).data())
        return c;
    auto *c = new MvDetailContext(m_api, m_playback, m_collection, *r, m_network);
    c->setNetworkPreferences(m_networkPreferences);
    m_videos.insert(r, c);
    connect(c, &MvDetailContext::notice, this, &DetailsService::notice);
    connect(r, &QObject::destroyed, this, [this, r] { m_videos.remove(r); });
    return c;
}
void DetailsService::attachShell(ShellController &shell)
{
    disconnect(m_entryConnection);
    // One bounded handoff of immutable public catalog facts, not URL payload
    // or another writable queue. Shell emits this before its route command.
    m_entryConnection = connect(&shell, &ShellController::songDetailOpening, this,
                                [this](const PlaybackTrack &track) {
                                    if (!m_closed) {
                                        m_entryTrack = track;
                                        m_entrySession = m_api.sessionRevision();
                                    }
                                });
}
void DetailsService::setNetworkPreferences(NetworkPreferences *network)
{
    m_networkPreferences = network;
    for (auto c : std::as_const(m_videos))
        if (c)
            c->setNetworkPreferences(network);
}
void DetailsService::shutdown()
{
    if (m_closed)
        return;
    m_closed = true;
    disconnect(m_entryConnection);
    m_entryTrack.reset();
    m_collection.shutdown();
    m_discussion.shutdown();
    for (auto c : std::as_const(m_songs))
        if (c)
            c->shutdown();
    for (auto c : std::as_const(m_videos))
        if (c)
            c->shutdown();
}
} // namespace EchoMusic

namespace EchoMusic
{
void DetailsService::registerRoutes(RouteController &router)
{
    router.registerPage({"song-detail",
                         "/main/detail/:id",
                         QUrl("qrc:/qt/qml/EchoMusic/Details/SongDetailPage.qml"),
                         {"tab", "mainTab", "commentTab"},
                         false,
                         true,
                         false,
                         {{"service", QVariant::fromValue(this)}}});
    router.registerPage({"mv-detail",
                         "/main/mv/:id",
                         QUrl("qrc:/qt/qml/EchoMusic/Details/MvDetailPage.qml"),
                         {},
                         false,
                         true,
                         false,
                         {{"service", QVariant::fromValue(this)},
                          {"windowService", QVariant::fromValue(m_window)}}});
}
} // namespace EchoMusic
