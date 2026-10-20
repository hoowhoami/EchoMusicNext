#include "ExploreService.h"
#include "PlaybackService.h"
#include <QJsonDocument>
#include <QQmlEngine>
namespace EchoMusic
{
namespace
{
const QStringList tabs{"playlists", "ranks", "albums", "songs", "artists"};
}
ExploreService::ExploreService(ApiClient &api, PlaybackService &playback, RouteController &router,
                               bool network, QObject *parent)
    : QObject(parent), m_api(api), m_playback(playback), m_router(router),
      m_newActions(m_newSongs, playback, this), m_rankActions(m_rankSongs, playback, this),
      m_network(network)
{
    QQmlEngine::setObjectOwnership(this, QQmlEngine::CppOwnership);
    m_categoryLabel = tr("全部");
    m_newActions.setQueueContext(tr("新歌速递"), "queue:explore:new-songs");
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
    return m_tab == 0   ? Playlists
           : m_tab == 1 ? RankSongs
           : m_tab == 2 ? Albums
           : m_tab == 3 ? NewSongs
                        : Artists;
}
bool ExploreService::loading() const
{
    if (m_tab == 1 && !m_rank)
        return m_states[RankCatalog] == Idle || m_states[RankCatalog] == Loading ||
               m_states[RankFallback] == Loading;
    const auto s = m_states[activePart()];
    return s == Idle || s == Loading;
}
QString ExploreService::errorMessage() const
{
    if (m_tab == 1 && !m_rank)
        return m_states[RankFallback] == Error ? m_errors[RankFallback] : QString{};
    return m_states[activePart()] == Error ? m_errors[activePart()] : QString{};
}
QStringList ExploreService::artistLetters() const
{
    QStringList values;
    for (const auto &g : m_artistGroups)
        values.append(g.toObject()["label"].toString());
    return values;
}
QString ExploreService::rankLabel() const
{
    for (const auto &r : m_ranks)
        if (quint64(r.toObject()["id"].toInteger()) == m_rank)
            return r.toObject()["title"].toString();
    return tr("排行榜");
}
void ExploreService::activate(RouteContext *context)
{
    if (m_closed)
        return;
    if (!context || context->name() != "explore") {
        m_active = false;
        cancelAll();
        return;
    }
    m_active = true;
    if (m_instance != context->instanceId()) {
        m_active = false;
        reset();
        m_instance = context->instanceId();
        m_active = true;
    }
    const int next = tabs.indexOf(context->query().value("tab", "playlists").toString());
    if (next >= 0 && next != m_tab) {
        cancel(activePart());
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
    m_ranks = {};
    m_artistGroups = {};
    m_sexFilters = {};
    m_typeFilters = {};
    m_albumRegions = {};
    m_cards.clear();
    m_newSongs.clear();
    m_rankSongs.clear();
    m_category = "0";
    m_categoryLabel = tr("全部");
    m_region = "all";
    m_sex = "0";
    m_artistType = "0:0";
    m_letter.clear();
    m_rank = 0;
    if (m_active && m_router.current() && m_router.current()->name() == "explore")
        loadCurrent();
    emit changed();
}
void ExploreService::selectTab(int tab)
{
    if (tab < 0 || tab >= tabs.size() || !m_active)
        return;
    auto query = m_router.current()->query();
    query["tab"] = tabs[tab];
    m_router.replaceQuery(query);
}
void ExploreService::loadCurrent()
{
    if (!m_active || !m_network || m_closed)
        return;
    if (m_tab == 0) {
        if (m_states[Categories] == Idle)
            request(Categories);
        if (m_states[Playlists] == Idle)
            request(Playlists);
    } else if (m_tab == 1) {
        if (!m_rank) {
            if (m_states[RankCatalog] == Idle)
                request(RankCatalog);
        } else if (m_states[RankSongs] == Idle)
            request(RankSongs);
    } else if (m_states[activePart()] == Idle)
        request(activePart());
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
    case RankCatalog:
        cancel(RankFallback);
        m_states[RankFallback] = Idle;
        m_errors[RankFallback].clear();
        id = m_api.rankCatalog();
        break;
    case RankFallback:
        id = m_api.recommendedRanks();
        break;
    case RankSongs:
        id = m_api.rankSongs(m_rank);
        break;
    case Albums:
        id = m_api.newAlbumCatalog();
        break;
    case NewSongs:
        id = m_api.newSongCatalog();
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
    case RankCatalog:
    case RankFallback:
        m_ranks = d.array();
        if (!m_ranks.isEmpty())
            selectRank(QString::number(m_ranks.first().toObject()["id"].toInteger()));
        break;
    case RankSongs:
        m_rankSongs.replaceSongs(o["songs"].toArray());
        m_rankSongs.setQuery({});
        break;
    case Albums:
        m_albumRegions = o["regions"].toObject();
        break;
    case NewSongs:
        m_newSongs.replaceSongs(o["songs"].toArray());
        m_newSongs.setQuery({});
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
    m_errors[p.part] =
        QJsonDocument::fromJson(bytes).object()["message"].toString(tr("内容加载失败"));
    if (p.part == RankCatalog)
        request(RankFallback);
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
void ExploreService::selectRank(const QString &id)
{
    for (const auto &r : m_ranks) {
        const auto rank = r.toObject();
        if (QString::number(rank["id"].toInteger()) == id) {
            const auto selected = quint64(rank["id"].toInteger());
            if (selected == m_rank)
                return;
            m_rank = selected;
            m_rankSongs.clear();
            m_rankActions.setQueueContext(rank["title"].toString(), "queue:explore:rank:" + id);
            request(RankSongs);
            emit changed();
            return;
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
    if (m_tab == 1 && !m_rank)
        request(RankCatalog);
    else
        request(activePart());
}
void ExploreService::projectCards()
{
    QJsonArray values;
    if (m_tab == 0)
        values = m_playlistCards;
    else if (m_tab == 2) {
        for (const auto &region : QStringList{"chn", "eur", "jpn", "kor"})
            if (m_region == "all" || m_region == region)
                for (const auto &v : m_albumRegions[region].toArray()) {
                    auto album = v.toObject();
                    const auto artist = album["creator"].toString();
                    const auto date = album["published_at"].toString();
                    album["extra_text"] =
                        artist + (!artist.isEmpty() && !date.isEmpty() ? " • " : "") + date;
                    values.append(album);
                }
    } else if (m_tab == 4) {
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
    if (m_tab != 1 && m_tab != 3)
        m_cards.replaceCards(values);
}
} // namespace EchoMusic

namespace EchoMusic
{
void ExploreService::registerRoutes(RouteController &router)
{
    router.registerPage({"explore",
                         "/main/explore",
                         QUrl("qrc:/qt/qml/EchoMusic/Discovery/ExplorePage.qml"),
                         {"tab"},
                         true,
                         true,
                         false,
                         {{"service", QVariant::fromValue(this)}}});
}
} // namespace EchoMusic
