#include <QQmlEngine>
#include "DiscoveryService.h"
#include "AccountService.h"
#include "PlaybackService.h"
#include "PlaybackLibraryActions.h"
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>
namespace EchoMusic
{
DiscoveryService::DiscoveryService(ApiClient &api, PlaybackService &playback,
                                   AccountService &account, QObject *parent)
    : QObject(parent), m_api(api), m_playback(playback), m_account(account),
      m_styleActions(m_styleSongs, playback, this), m_dailyActions(m_dailySongs, playback, this),
      m_rankActions(m_rankSongs, playback, this), m_freeActions(m_freeListen, playback, this),
      m_discoverActions(m_discoverFlow, playback, this)
{
    // QML borrows this application-owned object, including explicit invokable
    // returns. GC and engine teardown must never delete a C++ member/context.
    QQmlEngine::setObjectOwnership(this, QQmlEngine::CppOwnership);

    connect(&api, &ApiClient::succeeded, this, &DiscoveryService::succeeded);
    connect(&api, &ApiClient::failed, this, [this](quint64 id, int, const QByteArray &message) {
        const auto domain = QJsonDocument::fromJson(message).object();
        failed(id, domain.value("message").toString(tr("内容加载失败")));
    });
    connect(&api, &ApiClient::sessionChanged, this, [this] { reset(); });
    connect(&account, &AccountService::changed, this, &DiscoveryService::changed);
    // DiscoverFlow follows the shared playback queue; its active flag and
    // current row are derived from PlaybackService, never duplicated here.
    connect(&playback, &PlaybackService::changed, this, [this] {
        syncDiscoverQueue();
        emit changed();
    });
    connect(playback.discoverQueue(), &DiscoverQueueController::completed, this, [this](bool) {
        if (!m_discoverFollowQueue || !discoverFlowActive())
            return;
        m_discoverAppendWait = false;
        syncDiscoverQueue();
        m_states[DiscoverFlow] = Ready;
        emit changed();
    });
    connect(playback.discoverQueue(), &DiscoverQueueController::notice, this,
            [this](const QString &message) { emit notice(message, 0); });
    if (auto *actions = playback.libraryActions())
        connect(actions, &PlaybackLibraryActions::changed, this, [this] {
            ++m_favoriteRevision;
            emit favoritesChanged();
        });
}
DiscoveryService::~DiscoveryService()
{
    shutdown();
}
QVariantList DiscoveryService::states() const
{
    QVariantList result;
    for (auto state : m_states)
        result.append(int(state));
    return result;
}
QStringList DiscoveryService::errors() const
{
    QStringList result;
    for (const auto &error : m_errors)
        result.append(error);
    return result;
}
SongCatalogActions *DiscoveryService::actions(SongCatalogModel *model)
{
    auto *result = model == &m_styleSongs     ? &m_styleActions
                   : model == &m_dailySongs   ? &m_dailyActions
                   : model == &m_rankSongs    ? &m_rankActions
                   : model == &m_freeListen   ? &m_freeActions
                   : model == &m_discoverFlow ? &m_discoverActions
                                              : nullptr;
    if (result)
        result->setQueueContext(queueTitle(model), queueKey(model));
    return result;
}
bool DiscoveryService::discoverFlowActive() const
{
    // The discovery queue uses the stable DISCOVER_QUEUE_ID identity.
    return m_playback.sourceKey() == QStringLiteral("queue:home-discover");
}
bool DiscoveryService::isFavorite(SongCatalogModel *model, const QString &id) const
{
    const auto *a = const_cast<DiscoveryService *>(this)->actions(model);
    return a && a->isFavorite(id);
}
void DiscoveryService::toggleFavorite(SongCatalogModel *model, const QString &id)
{
    if (auto *a = actions(model))
        a->toggleFavorite(id);
}
QString DiscoveryService::greeting() const
{
    const auto hour = QTime::currentTime().hour();
    const auto text = hour < 6    ? tr("凌晨好")
                      : hour < 9  ? tr("早上好")
                      : hour < 12 ? tr("上午好")
                      : hour < 14 ? tr("中午好")
                      : hour < 18 ? tr("下午好")
                                  : tr("晚上好");
    return m_account.userId() && !m_account.nickname().isEmpty()
               ? tr("Hi, %1 %2").arg(m_account.nickname(), text)
               : text;
}
QString DiscoveryService::todayLabel() const
{
    return QString::number(QDate::currentDate().day());
}
int DiscoveryService::state(Part part) const
{
    return part >= 0 && part < PartCount ? m_states[part] : Error;
}
QString DiscoveryService::error(Part part) const
{
    return part >= 0 && part < PartCount ? m_errors[part] : QString();
}
QString DiscoveryService::styleSummary() const
{
    QStringList labels;
    for (const auto &group : m_styleGroups)
        for (const auto &tag : group.toMap()["child"].toList()) {
            const auto entry = tag.toMap();
            if (m_selectedTags.contains(entry["id"].toString()))
                labels.append(entry["name"].toString());
        }
    return labels.isEmpty() ? tr("默认推荐") : labels.join(" / ");
}
QVariantMap DiscoveryService::selectedRank() const
{
    for (const auto &value : m_ranks)
        if (value.toMap()["id"].toULongLong() == m_selectedRank)
            return value.toMap();
    return {};
}
void DiscoveryService::cancel(Part part)
{
    const auto ids = m_pending.keys();
    for (auto id : ids)
        if (m_pending.value(id).part == part) {
            m_pending.remove(id);
            m_api.cancel(id);
        }
    if (m_states[part] == Loading)
        m_states[part] = Idle;
}
void DiscoveryService::cancelAll()
{
    for (int i = 0; i < PartCount; ++i)
        cancel(Part(i));
}
void DiscoveryService::shutdown()
{
    if (m_closed)
        return;
    m_closed = true;
    cancelAll();
    disconnect(&m_api, nullptr, this, nullptr);
}
void DiscoveryService::reset()
{
    if (m_closed)
        return;
    const auto instance = m_instances.value(m_activePage);
    cancelAll();
    m_instances.clear();
    m_states.fill(Idle);
    m_errors.fill({});
    m_styleSongs.clear();
    m_playlists.clear();
    m_editors.clear();
    m_dailySongs.clear();
    m_rankSongs.clear();
    m_freeListen.clear();
    m_discoverFlow.clear();
    m_freeListen.setQuery({});
    m_discoverFlow.setQuery({});
    m_discoverKeys.clear();
    m_styleGroups.clear();
    m_selectedTags.clear();
    m_ranks.clear();
    m_rankTop = {};
    m_rankCatalog = {};
    m_category = "0";
    m_selectedRank = 0;
    m_rankTotal = 0;
    m_activeGroup = 0;
    m_dailyDate.clear();
    m_dailySubtitle.clear();
    // A session change can leave the same route mounted; do not strand it Idle.
    setPage(m_activePage, instance, m_networkEnabled);
}
void DiscoveryService::setPage(const QString &name, const QString &instance, bool networkEnabled)
{
    if (m_closed)
        return;
    if (name != m_activePage)
        cancelAll();
    m_playback.personalFm()->setPageActive(name == "personal-fm", networkEnabled);
    m_activePage = name;
    m_networkEnabled = networkEnabled;
    if (!m_instances.contains(name) || m_instances.value(name) != instance) {
        m_instances.insert(name, instance);
        if (name == "home") {
            for (auto p : {Style, Playlists, Editors}) {
                cancel(p);
                m_states[p] = Idle;
            }
            m_category = "0";
            m_styleGroups.clear();
            m_selectedTags.clear();
            m_styleSongs.clear();
            m_playlists.clear();
            m_editors.clear();
            m_activeGroup = 0;
        }
        if (name == "recommend-songs") {
            cancel(Daily);
            m_states[Daily] = Idle;
            m_dailySongs.clear();
            m_dailySongs.setQuery({});
            m_dailyDate.clear();
            m_dailySubtitle.clear();
        }
        if (name == "ranking") {
            for (auto p : {RankTop, RankCatalog, RankSongs}) {
                cancel(p);
                m_states[p] = Idle;
            }
            m_rankTop = {};
            m_rankCatalog = {};
            m_ranks.clear();
            m_rankSongs.clear();
            m_rankSongs.setQuery({});
            m_selectedRank = 0;
            m_rankTotal = 0;
        }
        if (name == "free-listen") {
            cancel(FreeListen);
            m_states[FreeListen] = Idle;
            m_freeListen.clear();
            m_freeListen.setQuery({});
        }
        if (name == "discover-flow") {
            cancel(DiscoverFlow);
            m_states[DiscoverFlow] = Idle;
            m_discoverFlow.clear();
            m_discoverFlow.setQuery({});
            m_discoverKeys.clear();
            m_discoverFollowQueue = true;
            m_discoverMirror.clear();
            syncDiscoverQueue();
        }
    }
    if (networkEnabled) {
        if (name == "home")
            for (auto p : {Style, Playlists, Editors}) {
                if (m_states[p] == Idle)
                    request(p);
            }
        if (name == "recommend-songs" && m_states[Daily] == Idle)
            request(Daily);
        if (name == "ranking") {
            for (auto p : {RankTop, RankCatalog}) {
                if (m_states[p] == Idle)
                    request(p);
            }
            if (m_selectedRank && m_states[RankSongs] == Idle)
                request(RankSongs);
        }
        if (name == "free-listen" && m_states[FreeListen] == Idle)
            request(FreeListen);
        if (name == "discover-flow" && m_states[DiscoverFlow] == Idle)
            request(DiscoverFlow);
    }
    emit changed();
}
void DiscoveryService::request(Part part, bool append)
{
    if (m_closed || !m_networkEnabled)
        return;
    const bool home = part <= Editors && m_activePage == "home";
    const bool daily = part == Daily && m_activePage == "recommend-songs";
    const bool ranking = part >= RankTop && part <= RankSongs && m_activePage == "ranking";
    const bool freeListen = part == FreeListen && m_activePage == "free-listen";
    const bool discoverFlow = part == DiscoverFlow && m_activePage == "discover-flow";
    if (!home && !daily && !ranking && !freeListen && !discoverFlow)
        return;
    cancel(part);
    m_errors[part].clear();
    m_states[part] = Loading;
    quint64 id = 0;
    switch (part) {
    case Style:
        id = m_api.styleRecommendations(m_selectedTags);
        break;
    case Playlists:
        id = m_api.recommendedPlaylists(m_category);
        break;
    case Editors:
        id = m_api.editorPlaylists();
        break;
    case Daily:
        id = m_api.dailyRecommendations();
        break;
    case RankTop:
        id = m_api.recommendedRanks();
        break;
    case RankCatalog:
        id = m_api.rankCatalog();
        break;
    case RankSongs:
        id = m_api.rankSongs(m_selectedRank);
        break;
    case FreeListen:
        id = m_api.freeListenSongs();
        break;
    case DiscoverFlow:
        id = m_api.discoverRecommendations(4);
        break;
    case PartCount:
        break;
    }
    if (id)
        m_pending.insert(id, {part, m_api.sessionRevision(), append});
    else {
        m_states[part] = Error;
        m_errors[part] = tr("请求未能启动");
    }
    emit changed();
}
void DiscoveryService::loadFreeListen()
{
    request(FreeListen);
}
QJsonArray DiscoveryService::discoverRows(const QJsonObject &payload)
{
    QJsonArray result;
    for (const auto &value : DiscoverQueueController::songs(payload)) {
        const auto key = value.toObject().value("id").toString();
        // Drop rows without a key and deduplicate repeated keys.
        if (key.isEmpty() || m_discoverKeys.contains(key))
            continue;
        m_discoverKeys.insert(key);
        result.append(value);
    }
    return result;
}
void DiscoveryService::loadDiscoverFlow(bool append)
{
    if (!append) {
        m_discoverFollowQueue = false;
        m_discoverAppendWait = false;
    }
    if (append && m_discoverFollowQueue && discoverFlowActive()) {
        m_discoverAppendWait = true;
        m_states[DiscoverFlow] = Loading;
        emit changed();
        if (!m_playback.discoverQueue()->replenish()) {
            m_discoverAppendWait = false;
            m_states[DiscoverFlow] = Ready;
            emit notice(tr("暂时没有新的推荐歌曲，请稍后重试"), 0);
            emit changed();
        }
        return;
    }
    request(DiscoverFlow, append);
}
void DiscoveryService::syncDiscoverQueue()
{
    if (m_closed || !m_discoverFollowQueue || !discoverFlowActive())
        return;
    auto *queue = m_playback.queue();
    if (m_discoverMirror != queue || m_discoverMirrorRevision != queue->revision()) {
        m_discoverMirror = queue;
        m_discoverMirrorRevision = queue->revision();
        QJsonArray rows;
        for (const auto &track : queue->tracks())
            rows.append(track.toCatalog());
        m_discoverFlow.replaceSongs(rows);
    }
    if (!m_discoverAppendWait)
        m_states[DiscoverFlow] = Ready;
}
void DiscoveryService::playDiscover(const QString &id)
{
    const auto tracks = m_discoverFlow.tracks();
    for (int index = 0; index < tracks.size(); ++index)
        if (tracks[index].id == id) {
            cancel(DiscoverFlow);
            m_discoverAppendWait = false;
            // A swipe always adopts the page's complete context, independently
            // of the ordinary single-song queue preference.
            m_discoverFollowQueue = true;
            m_playback.playTracks(tracks, index, tr("刷歌"), DiscoverQueueController::queueKey());
            syncDiscoverQueue();
            return;
        }
}
void DiscoveryService::notify(const QString &message, int tone)
{
    if (!message.isEmpty())
        emit notice(message, tone);
}
void DiscoveryService::succeeded(quint64 id, const QByteArray &bytes)
{
    if (!m_pending.contains(id))
        return;
    const auto pending = m_pending.take(id);
    if (pending.session != m_api.sessionRevision() || m_closed)
        return;
    const auto document = QJsonDocument::fromJson(bytes);
    const auto o = document.object();
    const auto part = pending.part;
    m_states[part] = Ready;
    switch (part) {
    case Style: {
        const bool initial = m_styleGroups.isEmpty();
        const auto groups = o["groups"].toArray();
        if (!groups.isEmpty())
            m_styleGroups = groups.toVariantList();
        if (m_activeGroup >= m_styleGroups.size())
            m_activeGroup = 0;
        if (initial && m_selectedTags.isEmpty())
            for (const auto &group : m_styleGroups)
                for (const auto &tag : group.toMap()["child"].toList()) {
                    const auto entry = tag.toMap();
                    if (entry["default"].toInt() == 1)
                        m_selectedTags.append(entry["id"].toString());
                }
        m_styleSongs.replaceSongs(o["songs"].toArray());
        break;
    }
    case Playlists:
        m_playlists.replaceCards(document.array());
        break;
    case Editors:
        m_editors.replaceCards(document.array());
        break;
    case Daily:
        m_dailySongs.replaceSongs(o["songs"].toArray());
        m_dailyDate = QDate::fromString(o["date"].toString(), "yyyyMMdd").toString(Qt::ISODate);
        m_dailySubtitle = o["subtitle"].toString();
        break;
    case RankTop:
        m_rankTop = document.array();
        mergeRanks();
        break;
    case RankCatalog:
        m_rankCatalog = document.array();
        mergeRanks();
        break;
    case RankSongs:
        m_rankSongs.replaceSongs(o["songs"].toArray());
        m_rankTotal = quint64(o["total"].toInteger());
        break;
    case FreeListen:
        // Replace the whole list, including when the response is empty.
        m_freeListen.replaceSongs(o["songs"].toArray());
        break;
    case DiscoverFlow:
        if (pending.append && m_discoverFlow.count() > 0) {
            const auto rows = discoverRows(o);
            if (rows.isEmpty())
                // Show an informational notice when an append batch returns no new rows.
                emit notice(tr("暂时没有新的推荐歌曲，请稍后重试"), 0);
            else
                m_discoverFlow.appendSongs(rows);
        } else {
            m_discoverKeys.clear();
            const auto rows = discoverRows(o);
            m_discoverFlow.replaceSongs(rows);
            // A successful fetch without items shows the no-recommendations error card.
            if (rows.isEmpty()) {
                m_states[part] = Error;
                m_errors[part] = tr("暂时没有推荐歌曲");
            }
        }
        break;
    case PartCount:
        break;
    }
    emit changed();
}
void DiscoveryService::failed(quint64 id, const QString &error)
{
    if (!m_pending.contains(id))
        return;
    const auto pending = m_pending.take(id);
    if (pending.session != m_api.sessionRevision() || m_closed)
        return;
    if (pending.part == DiscoverFlow && pending.append && m_discoverFlow.count() > 0) {
        // A failed append keeps existing rows usable and clears loadingMore
        // without replacing the page with an error state.
        m_states[pending.part] = Ready;
        m_errors[pending.part].clear();
        emit changed();
        return;
    }
    m_states[pending.part] = Error;
    m_errors[pending.part] = error;
    if (pending.part == DiscoverFlow && !pending.append && m_activePage == "discover-flow")
        emit notice(tr("推荐歌曲加载失败，请稍后重试"), 2);
    if (pending.part == Style)
        m_styleSongs.clear();
    if (pending.part == FreeListen)
        m_freeListen.clear();
    if (pending.part == RankTop || pending.part == RankCatalog)
        mergeRanks();
    emit changed();
}
void DiscoveryService::mergeRanks()
{
    // Wait for both independent requests to settle.
    if (m_states[RankTop] == Loading || m_states[RankCatalog] == Loading ||
        m_states[RankTop] == Idle || m_states[RankCatalog] == Idle)
        return;
    const auto source = m_rankTop.isEmpty() ? m_rankCatalog : m_rankTop;
    m_ranks.clear();
    for (const auto &value : source) {
        auto rank = value.toObject();
        for (const auto &detail : m_rankCatalog) {
            const auto d = detail.toObject();
            if (d["id"] != rank["id"])
                continue;
            if (rank["description"].toString().isEmpty())
                rank["description"] = d["description"];
            if (rank["update_frequency"].toString().isEmpty())
                rank["update_frequency"] = d["update_frequency"];
            if (!d["published_at"].isNull())
                rank["published_at"] = d["published_at"];
        }
        m_ranks.append(rank.toVariantMap());
    }
    if (!m_ranks.isEmpty() && !m_selectedRank) {
        m_selectedRank = m_ranks.first().toMap()["id"].toULongLong();
        request(RankSongs);
    }
}
void DiscoveryService::retry(Part part)
{
    if (part < 0 || part >= PartCount)
        return;
    if (part == RankTop || part == RankCatalog) {
        request(RankTop);
        request(RankCatalog);
    } else
        request(part);
}
void DiscoveryService::selectCategory(const QString &value)
{
    if (m_activePage != "home" || m_category == value || (value != "0" && value != "11292"))
        return;
    m_category = value;
    request(Playlists);
    emit changed();
}
void DiscoveryService::selectStyleGroup(int value)
{
    if (value < 0 || value >= m_styleGroups.size() || value == m_activeGroup)
        return;
    m_activeGroup = value;
    emit changed();
}
void DiscoveryService::toggleTag(const QString &value)
{
    bool known = false;
    for (const auto &group : m_styleGroups)
        for (const auto &tag : group.toMap()["child"].toList())
            known |= tag.toMap()["id"] == value;
    if (!known || m_activePage != "home")
        return;
    if (m_selectedTags.contains(value))
        m_selectedTags.removeAll(value);
    else
        m_selectedTags.append(value);
    request(Style);
    emit changed();
}
void DiscoveryService::selectRank(quint64 id)
{
    if (m_activePage != "ranking" || id == m_selectedRank)
        return;
    if (std::none_of(m_ranks.cbegin(), m_ranks.cend(),
                     [&](const auto &v) { return v.toMap()["id"].toULongLong() == id; }))
        return;
    m_selectedRank = id;
    m_rankTotal = 0;
    m_rankSongs.clear();
    request(RankSongs);
    emit changed();
}
QString DiscoveryService::queueKey(const SongCatalogModel *m) const
{
    if (m == &m_styleSongs)
        return "queue:home:style-recommend:" +
               (m_selectedTags.isEmpty() ? "default" : m_selectedTags.join("-"));
    if (m == &m_dailySongs)
        return "queue:daily-recommend";
    if (m == &m_freeListen)
        return "queue:free-listen";
    if (m == &m_discoverFlow)
        return "queue:home-discover";
    return "queue:ranking:" + QString::number(m_selectedRank);
}
QString DiscoveryService::queueTitle(const SongCatalogModel *m) const
{
    return m == &m_styleSongs     ? tr("风格推荐")
           : m == &m_dailySongs   ? tr("每日推荐")
           : m == &m_freeListen   ? tr("免费听")
           : m == &m_discoverFlow ? tr("刷歌")
                                  : selectedRank()["title"].toString();
}
void DiscoveryService::play(SongCatalogModel *model, const QString &id, bool replaceContext,
                            bool selectedOnly)
{
    if (auto *a = actions(model))
        a->play(id, replaceContext, selectedOnly);
}
void DiscoveryService::appendSelected(SongCatalogModel *model)
{
    if (auto *a = actions(model))
        a->appendSelected();
}
void DiscoveryService::addSelection(SongCatalogModel *model)
{
    if (auto *a = actions(model))
        a->addSelection();
}
void DiscoveryService::addSong(SongCatalogModel *model, const QString &id)
{
    if (auto *a = actions(model))
        a->addSong(id);
}
void DiscoveryService::appendSong(SongCatalogModel *model, const QString &id, bool next)
{
    if (auto *a = actions(model))
        a->appendSong(id, next);
}
void DiscoveryService::copySong(SongCatalogModel *model, const QString &id)
{
    if (auto *a = actions(model))
        a->copySong(id);
}
} // namespace EchoMusic
