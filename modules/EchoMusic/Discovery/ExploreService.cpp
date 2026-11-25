#include "ExploreService.h"
#include "DiscoveryService.h"
#include "PlaybackService.h"
#include "SongMetadataService.h"
#include <QJsonDocument>
#include <QQmlEngine>
namespace EchoMusic
{
ExploreService::ExploreService(ApiClient &api, PlaybackService &playback, RouteController &router,
                               bool network, QObject *parent)
    : QObject(parent), m_api(api), m_playback(playback), m_router(router), m_network(network)
{
    QQmlEngine::setObjectOwnership(this, QQmlEngine::CppOwnership);
    m_categoryLabel = tr("全部");
    if (auto *metadata = playback.metadata())
        m_artists.attach(metadata->sessionDatabase());
    m_artists.setActive(false);
    connect(&m_artists, &ArtistDirectoryModel::readFailed, this, [this](const QString &error) {
        m_states[Artists] = Error;
        m_errors[Artists] = error;
        m_artists.finishRecovery();
        emit changed();
    });
    connect(&m_artists, &ArtistDirectoryModel::invalidated, this, [this] {
        cancel(Artists);
        m_states[Artists] = Idle;
        if (m_active && m_tab == 4)
            request(Artists);
        emit changed();
    });
    connect(&router, &RouteController::pageActivated, this, &ExploreService::activate);
    connect(&api, &ApiClient::succeeded, this, &ExploreService::accept);
    connect(&api, &ApiClient::failed, this,
            [this](quint64 id, int, const QByteArray &bytes) { fail(id, bytes); });
    connect(&api, &ApiClient::sessionChanged, this, &ExploreService::reset);
    connect(&api, &ApiClient::closed, this, &ExploreService::shutdown);
}
ExploreService::~ExploreService()
{
    shutdown();
}
void ExploreService::shutdown()
{
    if (m_closed)
        return;
    m_closed = true;
    m_artists.setActive(false);
    cancelAll();
}
void ExploreService::cancel(Part p)
{
    for (auto id : m_pending.keys())
        if (m_pending[id].part == p) {
            m_pending.remove(id);
            m_api.cancel(id);
        }
    if (m_states[p] == Loading)
        m_states[p] = Idle;
}
void ExploreService::cancelAll()
{
    for (int i = 0; i < PartCount; ++i)
        cancel(Part(i));
}
ExploreService::Part ExploreService::activePart() const
{
    return m_tab == 4 ? Artists : m_tab == 0 ? Albums : Playlists;
}
bool ExploreService::loading() const
{
    const auto state = m_states[activePart()];
    return state == Idle || state == Loading;
}
QString ExploreService::errorMessage() const
{
    if (m_states[activePart()] == Error)
        return m_errors[activePart()];
    return m_tab == 1 && m_states[Categories] == Error ? m_errors[Categories] : QString{};
}
QStringList ExploreService::artistLetters() const
{
    return m_artists.letters();
}

void ExploreService::activate(RouteContext *context)
{
    if (m_closed)
        return;
    if (!context || (context->name() != "explore" && context->name() != "playlist-categories" &&
                     context->name() != "artist-recommendations")) {
        m_active = false;
        m_artists.setActive(false);
        cancelAll();
        return;
    }
    m_active = true;
    if (m_instances.value(context->name()) != context->instanceId()) {
        m_instances.insert(context->name(), context->instanceId());
        if (context->name() == "explore") {
            cancel(Albums);
            m_states[Albums] = Idle;
            m_errors[Albums].clear();
            m_albumRegions = {};
            m_albums.clear();
        } else if (context->name() == "playlist-categories") {
            for (auto part : {Categories, Playlists}) {
                cancel(part);
                m_states[part] = Idle;
                m_errors[part].clear();
            }
            m_categories = {};
            m_playlistCards = {};
            m_cards.clear();
            m_category = "0";
            m_categoryLabel = tr("全部");
        } else {
            cancel(Artists);
            m_states[Artists] = Idle;
            m_errors[Artists].clear();
            m_artists.clear();
            m_sexFilters = {};
            m_typeFilters = {};
        }
    }
    const int next = context->name() == "artist-recommendations" ? 4
                     : context->name() == "playlist-categories"  ? 1
                                                                 : 0;
    if (next >= 0 && next != m_tab) {
        cancelAll();
        m_tab = next;
    }
    m_artists.setActive(next == 4);
    loadCurrent();
    emit changed();
}
void ExploreService::reset()
{
    cancelAll();
    m_states.fill(Idle);
    m_errors.fill({});
    m_categories = {};
    m_playlistCards = {};
    m_artists.clear();
    m_sexFilters = {};
    m_typeFilters = {};
    m_albumRegions = {};
    m_albums.clear();
    m_cards.clear();
    m_category = "0";
    m_categoryLabel = tr("全部");
    m_region = "all";
    m_sex = "0";
    m_artistType = "0:0";

    if (m_active && m_router.current() &&
        (m_router.current()->name() == "explore" ||
         m_router.current()->name() == "artist-recommendations" ||
         m_router.current()->name() == "playlist-categories"))
        loadCurrent();
    emit changed();
}

void ExploreService::loadCurrent()
{
    if (!m_active || !m_network || m_closed)
        return;
    if (m_tab == 0) {
        if (m_states[Albums] == Idle)
            request(Albums);
    } else if (m_tab == 1) {
        if (m_states[Categories] == Idle)
            request(Categories);
        if (m_states[Categories] != Idle && m_states[Categories] != Loading &&
            m_states[Playlists] == Idle)
            request(Playlists);
    } else if (m_states[Artists] == Idle)
        request(Artists);
}
void ExploreService::request(Part p)
{
    if (!m_active || !m_network || m_closed)
        return;
    if (!((m_tab == 0 && p == Albums) || (m_tab == 1 && (p == Categories || p == Playlists)) ||
          (m_tab == 4 && p == Artists)))
        return;
    cancel(p);
    m_errors[p].clear();
    m_states[p] = Loading;
    quint64 id = 0;
    switch (p) {
    case Categories:
        id = m_api.playlistCategories();
        break;
    case Playlists:
        id = m_api.recommendedPlaylists(m_category);
        break;
    case Albums:
        id = m_api.newAlbumCatalog();
        break;
    case Artists: {
        const auto parts = m_artistType.split(':');
        id =
            m_api.artistDirectory(m_sex.toUInt(), parts.value(0).toUInt(), parts.value(1).toUInt());
        break;
    }
    case PartCount:
        break;
    }
    if (id)
        m_pending.insert(id, {p, m_api.sessionRevision()});
    else {
        m_states[p] = Error;
        m_errors[p] = tr("请求未能启动");
    }
    emit changed();
}
void ExploreService::accept(quint64 id, const QByteArray &bytes)
{
    if (!m_pending.contains(id))
        return;
    const auto pending = m_pending.take(id);
    if (m_closed || pending.session != m_api.sessionRevision() || !m_active)
        return;
    const auto p = pending.part;
    const auto d = QJsonDocument::fromJson(bytes);
    const auto o = d.object();
    m_states[p] = Ready;
    switch (p) {
    case Categories:
        m_categories = d.array();
        if (m_category == "0" && !m_categories.isEmpty()) {
            const auto children = m_categories.first().toObject()["children"].toArray();
            if (!children.isEmpty())
                selectCategory(children.first().toObject()["id"].toString());
        }
        if (m_tab == 1 && m_states[Playlists] == Idle)
            request(Playlists);
        break;
    case Playlists:
        m_playlistCards = d.array();
        break;
    case Albums:
        m_albumRegions = o["regions"].toObject();
        break;
    case Artists:
        m_artists.replace(o["groups"].toArray());
        m_sexFilters = o["sex_filters"].toArray();
        m_typeFilters = o["type_filters"].toArray();
        break;
    case PartCount:
        break;
    }
    if (p == Playlists)
        projectCards();
    else if (p == Albums)
        projectAlbums();
    emit changed();
}
void ExploreService::fail(quint64 id, const QByteArray &bytes)
{
    if (!m_pending.contains(id))
        return;
    const auto p = m_pending.take(id);
    if (p.session != m_api.sessionRevision() || m_closed || !m_active)
        return;
    m_states[p.part] = Error;
    m_errors[p.part] = ApiClient::errorMessage(bytes, tr("内容加载失败"));
    if (p.part == Artists)
        m_artists.finishRecovery();
    if (p.part == Categories && m_tab == 1 && m_states[Playlists] == Idle)
        request(Playlists);
    emit changed();
}
void ExploreService::selectCategory(const QString &id)
{
    for (const auto &group : m_categories) {
        const auto g = group.toObject();
        for (const auto &v : g["children"].toArray()) {
            const auto child = v.toObject();
            if (child["id"] == id) {
                if (m_category == id)
                    return;
                m_category = id;
                m_categoryLabel = g["name"].toString() + " - " + child["name"].toString();
                m_playlistCards = {};
                projectCards();
                request(Playlists);
                emit changed();
                return;
            }
        }
    }
}

void ExploreService::selectRegion(const QString &id)
{
    if (!QStringList{"all", "chn", "eur", "jpn", "kor"}.contains(id) || m_region == id)
        return;
    m_region = id;
    projectAlbums();
    if (m_states[Albums] != Ready)
        request(Albums);
    emit changed();
}
void ExploreService::selectSex(const QString &id)
{
    for (const auto &v : m_sexFilters)
        if (v.toObject()["id"] == id && m_sex != id) {
            m_sex = id;
            request(Artists);
            emit changed();
            return;
        }
}
void ExploreService::selectArtistType(const QString &id)
{
    for (const auto &v : m_typeFilters)
        if (v.toObject()["id"] == id && m_artistType != id) {
            m_artistType = id;
            request(Artists);
            emit changed();
            return;
        }
}
void ExploreService::selectLetter(const QString &letter)
{
    if (m_artists.selectLetter(letter))
        emit changed();
}
void ExploreService::retry()
{
    if (m_tab == 4 && m_artists.retryRead()) {
        m_states[Artists] = Ready;
        m_errors[Artists].clear();
        emit changed();
        return;
    }
    if (m_tab == 1 && m_states[Categories] == Error) {
        request(Categories);
        if (m_states[Playlists] != Error && m_states[Playlists] != Idle)
            return;
    }
    request(activePart());
}
void ExploreService::projectCards()
{
    m_cards.replaceCards(m_playlistCards);
}
void ExploreService::projectAlbums()
{
    QJsonArray albums;
    for (const auto &region : QStringList{"chn", "eur", "jpn", "kor"})
        if (m_region == "all" || m_region == region)
            for (const auto &v : m_albumRegions[region].toArray())
                albums.append(v);
    m_albums.replaceCards(albums);
}
} // namespace EchoMusic

namespace EchoMusic
{
void ExploreService::registerRoutes(RouteController &router)
{
    router.registerPage({"artist-recommendations",
                         "/main/artists",
                         QUrl("qrc:/qt/qml/EchoMusic/Discovery/ArtistRecommendationsPage.qml"),
                         {},
                         true,
                         true,
                         false,
                         {{"service", QVariant::fromValue(this)}}});
    router.registerPage({"playlist-categories",
                         "/main/playlist-categories",
                         QUrl("qrc:/qt/qml/EchoMusic/Discovery/PlaylistCategoriesPage.qml"),
                         {},
                         true,
                         true,
                         false,
                         {{"service", QVariant::fromValue(this)}}});
    router.registerPage({"explore",
                         "/main/explore",
                         QUrl("qrc:/qt/qml/EchoMusic/Discovery/ExplorePage.qml"),
                         {},
                         true,
                         true,
                         false,
                         {{"service", QVariant::fromValue(this)},
                          {"discovery", QVariant::fromValue(m_discovery)}}});
}
} // namespace EchoMusic
