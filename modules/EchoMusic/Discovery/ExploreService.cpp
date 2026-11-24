#include "ExploreService.h"
#include "DiscoveryService.h"
#include "PlaybackService.h"
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
    return m_tab == 4 ? Artists : Playlists;
}
bool ExploreService::loading() const
{
    const auto state = m_states[activePart()];
    return state == Idle || state == Loading;
}
QString ExploreService::errorMessage() const
{
    return m_states[activePart()] == Error ? m_errors[activePart()] : QString{};
}
QStringList ExploreService::artistLetters() const
{
    QStringList values;
    for (const auto &g : m_artistGroups)
        values.append(g.toObject()["label"].toString());
    return values;
}

void ExploreService::activate(RouteContext *context)
{
    if (m_closed)
        return;
    if (!context || (context->name() != "explore" && context->name() != "artist-recommendations")) {
        m_active = false;
        cancelAll();
        return;
    }
    m_active = true;
    if (m_instances.value(context->name()) != context->instanceId()) {
        m_instances.insert(context->name(), context->instanceId());
        if (context->name() == "explore") {
            for (auto part : {Categories, Playlists, Albums}) {
                cancel(part);
                m_states[part] = Idle;
                m_errors[part].clear();
            }
            m_categories = {};
            m_playlistCards = {};
            m_albumRegions = {};
            m_albums.clear();
            m_category = "0";
            m_categoryLabel = tr("全部");
        } else {
            cancel(Artists);
            m_states[Artists] = Idle;
            m_errors[Artists].clear();
            m_artistGroups = {};
            m_sexFilters = {};
            m_typeFilters = {};
            m_letter.clear();
        }
    }
    const int next = context->name() == "artist-recommendations" ? 4 : 0;
    if (next >= 0 && next != m_tab) {
        cancelAll();
        m_tab = next;
    }
    projectCards();
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
    m_artistGroups = {};
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
    m_letter.clear();
    if (m_active && m_router.current() &&
        (m_router.current()->name() == "explore" ||
         m_router.current()->name() == "artist-recommendations"))
        loadCurrent();
    emit changed();
}

void ExploreService::loadCurrent()
{
    if (!m_active || !m_network || m_closed)
        return;
    if (m_tab == 0) {
        for (auto part : {Categories, Playlists, Albums})
            if (m_states[part] == Idle)
                request(part);
    } else if (m_states[Artists] == Idle)
        request(Artists);
}
void ExploreService::request(Part p)
{
    if (!m_active || !m_network || m_closed)
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
        break;
    case Playlists:
        m_playlistCards = d.array();
        break;
    case Albums:
        m_albumRegions = o["regions"].toObject();
        break;
    case Artists:
        m_artistGroups = o["groups"].toArray();
        m_sexFilters = o["sex_filters"].toArray();
        m_typeFilters = o["type_filters"].toArray();
        m_letter.clear();
        for (const auto &v : m_artistGroups) {
            const auto label = v.toObject()["label"].toString();
            if (m_letter.isEmpty())
                m_letter = label;
            if (label.size() > 1) {
                m_letter = label;
                break;
            }
        }
        break;
    case PartCount:
        break;
    }
    projectCards();
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
    projectCards();
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
    if (letter != "全部" && !artistLetters().contains(letter))
        return;
    if (m_letter == letter)
        return;
    m_letter = letter;
    projectCards();
    emit changed();
}
void ExploreService::retry()
{
    request(activePart());
}
void ExploreService::projectCards()
{
    QJsonArray values;
    if (m_tab == 0)
        values = m_playlistCards;
    else if (m_tab == 4) {
        for (const auto &g : m_artistGroups)
            if (m_letter == "全部" || g.toObject()["label"] == m_letter)
                for (const auto &v : g.toObject()["artists"].toArray()) {
                    auto a = v.toObject();
                    const auto m = a["metadata"].toObject();
                    QStringList labels;
                    const auto songs = m["song_count"].toInteger(),
                               albums = m["album_count"].toInteger(),
                               fans = m["fans_count"].toInteger();
                    if (songs)
                        labels.append(tr("%1 歌曲").arg(songs));
                    if (albums)
                        labels.append(tr("%1 专辑").arg(albums));
                    a["creator"] =
                        !labels.isEmpty() ? labels.join(" • ")
                        : fans ? (fans >= 10000 ? QString::number(fans / 10000., 'f', 1) + tr("万")
                                                : QString::number(fans)) +
                                     tr(" 粉丝")
                               : m["description"].toString();
                    values.append(a);
                }
    }
    m_cards.replaceCards(values);
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
