#include "FavoritesService.h"
#include "FavoriteSongIndex.h"
#include "PlaybackService.h"
#include <QJsonDocument>
#include <QQmlEngine>
#include <QJsonObject>
#include <QUrl>
#include <algorithm>
#include <utility>
namespace EchoMusic
{
namespace
{
QJsonArray splitFollow(const QJsonArray &rows, bool singers)
{
    QJsonArray output;
    for (const auto &value : rows) {
        const auto row = value.toObject();
        const auto artistId = row.value("artist_id");
        // Singer entries require singerid with iden_type == 1.
        const bool artist =
            artistId.isDouble() && artistId.toInteger() > 0 &&
            row.value("identity_facts").toObject().value("identity_type").toInteger() == 1;
        if (artist == singers)
            output.append(row);
    }
    return output;
}
} // namespace
FavoritesService::FavoritesService(ApiClient &api, PlaybackService &playback,
                                   UserPlaylistModel &library, PlaylistOrderModel &order,
                                   SongMetadataService &metadata, RouteController &router,
                                   bool network, QObject *parent)
    : QObject(parent), m_api(api), m_playback(playback), m_library(library), m_order(order),
      m_metadata(metadata), m_router(router), m_actions(m_songs, playback, this), m_singers(this),
      m_users(this), m_network(network)
{
    m_songs.attachMetadata(&metadata);
    m_songs.setMetadataActive(false);
    QQmlEngine::setObjectOwnership(this, QQmlEngine::CppOwnership);
    m_actions.setQueueContext(tr("我最喜爱"), "queue:favorites");
    connect(&router, &RouteController::pageActivated, this, &FavoritesService::activate);
    connect(&api, &ApiClient::succeeded, this, &FavoritesService::accept);
    connect(&api, &ApiClient::failed, this,
            [this](quint64 id, int, const QByteArray &bytes) { fail(id, bytes); });
    connect(&api, &ApiClient::sessionChanged, this, &FavoritesService::reset);
    connect(&api, &ApiClient::closed, this, &FavoritesService::shutdown);
    connect(&metadata, &SongMetadataService::completed, this, &FavoritesService::acceptMetadata);
    connect(&library, &UserPlaylistModel::changed, this, [this] {
        if (m_closed)
            return;
        projectAlbums();
        emit changed();
    });
    connect(&library, &UserPlaylistModel::coverChanged, this, [this](const QString &global) {
        const auto row = m_library.likedRow();
        if (!m_closed && row >= 0 &&
            m_library.data(m_library.index(row), UserPlaylistModel::GlobalIdRole).toString() ==
                global)
            emit changed();
    });
    connect(m_library.favoriteSongs(), &FavoriteSongIndex::snapshotChanged, this, [this] {
        if (!m_closed && m_active && m_tab == 0)
            syncLikedSongs();
        else
            m_likedLoaded = false;
    });
    connect(m_library.favoriteSongs(), &FavoriteSongIndex::changed, this, [this] {
        if (!m_closed && m_active && m_tab == 0) {
            m_error = m_library.favoriteSongs()->errorMessage();
            emit changed();
        }
    });
    connect(&order, &PlaylistOrderModel::saved, this, [this, &order] {
        if (!m_closed && order.mode() == 1 &&
            (order.listId() == m_likedId || order.listId() == likedListId()))
            m_library.favoriteSongs()->refresh();
    });
}
FavoritesService::~FavoritesService()
{
    shutdown();
}
void FavoritesService::attachPlaylistMutations(PlaylistMutationService &mutations)
{
    disconnect(m_mutationStateConnection);
    m_mutations = &mutations;
    m_library.favoriteSongs()->attachMutations(mutations);
    m_mutationStateConnection =
        connect(&mutations, &PlaylistMutationService::changed, this, [this] {
            if (m_removeSession && m_removeSession != m_api.sessionRevision())
                m_removeSession = 0;
            if (m_removeSession && m_mutations->state() == PlaylistMutationService::State::Ready) {
                m_removeSession = 0;
                const auto error = m_mutations->errorMessage();
                emit notice(error.isEmpty() ? tr("已从歌单移除") : error, !error.isEmpty());
            }
            emit changed();
        });
    emit changed();
}
bool FavoritesService::canRemoveSongs() const
{
    return !m_closed && m_active && m_tab == 0 && m_mutations && m_likedLoaded && m_likedId &&
           m_likedId == likedListId();
}
bool FavoritesService::removeBusy() const
{
    return !m_mutations || m_mutations->state() != PlaylistMutationService::State::Ready;
}
bool FavoritesService::removeSongs(const QStringList &ids)
{
    if (!canRemoveSongs() || removeBusy())
        return false;
    const QSet<QString> selected(ids.begin(), ids.end());
    QSet<quint64> seen;
    QList<ApiClient::PlaylistTrackFileId> files;
    for (const auto &value : m_songRows) {
        const auto entry = value.toObject();
        if (!selected.contains(entry["song"].toObject()["id"].toString()))
            continue;
        const auto file = entry["file_id"].toInteger();
        if (file <= 0)
            return false;
        if (!seen.contains(quint64(file))) {
            seen.insert(quint64(file));
            files.append(ApiClient::PlaylistTrackFileId{quint64(file)});
        }
    }
    if (files.isEmpty())
        return false;
    m_removeSession = m_api.sessionRevision();
    if (m_mutations->removeTracks({m_likedId}, files))
        return true;
    m_removeSession = 0;
    return false;
}
const QStringList &FavoritesService::tabs()
{
    static const QStringList value{"songs", "singers", "users", "albums", "videos"};
    return value;
}
bool FavoritesService::hasPending(PendingKind kind) const
{
    for (const auto &pending : m_pending)
        if (pending.kind == kind)
            return true;
    return false;
}
bool FavoritesService::loading() const
{
    if (!m_pending.isEmpty() || (m_active && m_tab == 0 && m_library.favoriteSongs()->loading()))
        return true;
    return m_active && m_tab == 3 && m_library.state() == UserPlaylistModel::State::Loading;
}
bool FavoritesService::hasMore() const
{
    if (m_tab == 0)
        return !m_library.favoriteSongs()->complete() && m_library.favoriteSongs()->loading();
    if (m_tab == 4)
        return !m_videoExhausted && m_videoTotal > 0 && m_videoReceived < m_videoTotal;
    return false;
}
void FavoritesService::selectTab(int tab)
{
    if (!m_active || tab < 0 || tab >= tabs().size() || !m_router.current())
        return;
    auto query = m_router.current()->query();
    query["tab"] = tabs().at(tab);
    m_router.replaceQuery(query);
}
void FavoritesService::activate(RouteContext *context)
{
    m_songs.setMetadataActive(!m_closed && m_network && context && context->name() == "favorites");
    if (m_closed)
        return;
    if (!context || context->name() != "favorites") {
        m_active = false;
        cancelAll();
        return;
    }
    if (m_instance != context->instanceId()) {
        m_active = false;
        reset();
        m_instance = context->instanceId();
    }
    m_active = true;
    const int next = tabs().indexOf(context->query().value("tab", "songs").toString());
    if (next >= 0 && next != m_tab)
        m_tab = next;
    projectAlbums();
    loadTab();
    emit changed();
}
void FavoritesService::loadTab()
{
    if (!m_active || !m_network || m_closed || !m_api.currentAccountUserId())
        return;
    switch (m_tab) {
    case 0:
        requestLikedSongs();
        break;
    case 1:
    case 2:
        if (!m_followLoaded)
            requestFollow();
        break;
    case 3:
        if (m_library.state() == UserPlaylistModel::State::Idle)
            m_library.reload();
        projectAlbums();
        break;
    case 4:
        if (!m_videosLoaded)
            requestVideos();
        break;
    default:
        break;
    }
}
void FavoritesService::invalidateFollow()
{
    const auto ids = m_pending.keys();
    for (auto id : ids)
        if (m_pending.value(id).kind == PendingKind::Follow) {
            m_pending.remove(id);
            m_api.cancel(id);
        }
    m_followLoaded = false;
    if (m_active && (m_tab == 1 || m_tab == 2))
        loadTab();
    emit changed();
}
void FavoritesService::requestFollow()
{
    if (hasPending(PendingKind::Follow))
        return;
    const auto request = m_api.socialUsers(ApiClient::SocialList::Follow);
    if (!request) {
        m_error = tr("关注列表加载失败");
        emit changed();
        return;
    }
    m_pending.insert(request, {PendingKind::Follow, m_api.sessionRevision()});
    m_error.clear();
    emit changed();
}
void FavoritesService::requestLikedSongs()
{
    auto *favorites = m_library.favoriteSongs();
    favorites->ensureLoaded();
    syncLikedSongs();
}
void FavoritesService::syncLikedSongs()
{
    auto *favorites = m_library.favoriteSongs();
    if (m_songSnapshotGeneration != favorites->generation()) {
        cancelMetadata();
        m_songSnapshotGeneration = favorites->generation();
    }
    m_likedId = favorites->listId();
    m_songRevision = favorites->revision();
    m_songCoverConsistent = favorites->coverConsistent();
    m_songRows = favorites->rows();
    m_songReceived = favorites->received();
    m_songTotal = quint64(m_songRows.size());
    m_songHasMore = !favorites->complete() && favorites->loading();
    m_likedLoaded = favorites->complete();
    m_error = favorites->errorMessage();
    QJsonArray songs;
    for (const auto &value : m_songRows) {
        const auto row = value.toObject();
        auto song = row.value("song").toObject();
        song.insert("metadata", row.value("metadata"));
        songs.append(song);
    }
    m_songs.replaceSongs(songs, true);
    completeSongs(m_songRows);
    if (m_likedLoaded)
        publishCover();
    emit changed();
}
void FavoritesService::requestVideos()
{
    if (hasPending(PendingKind::Video))
        return;
    const auto request = m_api.userVideoCollect(1, 30);
    if (!request) {
        m_error = tr("收藏视频加载失败");
        emit changed();
        return;
    }
    m_pending.insert(request, {PendingKind::Video, m_api.sessionRevision()});
    m_error.clear();
    emit changed();
}
void FavoritesService::refresh()
{
    if (m_closed)
        return;
    m_error.clear();
    switch (m_tab) {
    case 0:
        m_likedLoaded = false;
        cancelAll();
        if (m_active) {
            m_library.favoriteSongs()->refresh();
            syncLikedSongs();
        }
        break;
    case 1:
    case 2:
        m_followLoaded = false;
        cancelAll();
        if (m_active)
            loadTab();
        break;
    case 3:
        projectAlbums();
        if (m_library.state() == UserPlaylistModel::State::Idle)
            m_library.reload();
        break;
    case 4:
        m_videosLoaded = false;
        m_videoExhausted = false;
        m_videoRows = {};
        m_videos.replaceCards({});
        cancelAll();
        if (m_active)
            loadTab();
        break;
    default:
        break;
    }
    emit changed();
}
void FavoritesService::loadMore()
{
    if (m_closed || !m_active || !m_network || !m_api.currentAccountUserId())
        return;
    if (m_tab == 4 && hasMore() && !hasPending(PendingKind::Video)) {
        const auto request = m_api.userVideoCollect(quint32(m_videoReceived / 30 + 1), 30);
        if (request)
            m_pending.insert(request, {PendingKind::Video, m_api.sessionRevision()});
        emit changed();
    }
}
void FavoritesService::projectAlbums()
{
    QJsonArray cards;
    for (int row = 0; row < m_library.rowCount(); ++row) {
        const auto i = m_library.index(row);
        if (m_library.data(i, UserPlaylistModel::SourceCodeRole).toUInt() != 2)
            continue;
        // Album routes prefer listCreateListid over the row ID.
        const auto creatorList =
            quint64(m_library.data(i, UserPlaylistModel::CreatorListIdRole).toULongLong());
        const auto cloud =
            quint64(m_library.data(i, UserPlaylistModel::CloudListIdRole).toULongLong());
        const auto id = creatorList ? creatorList : cloud;
        if (!id)
            continue;
        cards.append(QJsonObject{
            {"id", QString::number(id)},
            {"title", m_library.data(i, UserPlaylistModel::TitleRole).toString()},
            {"cover_url", m_library.data(i, UserPlaylistModel::CoverUrlRole).toString()},
            {"creator", m_library.data(i, UserPlaylistModel::CreatorNameRole).toString()},
        });
    }
    m_albums.replaceCards(cards);
}
void FavoritesService::projectFollow()
{
    const auto singers = splitFollow(m_followRows, true);
    const auto query = m_singerQuery.trimmed();
    if (query.isEmpty())
        m_singers.replace(singers);
    else {
        QJsonArray filtered;
        for (const auto &value : singers)
            if (value.toObject().value("nickname").toString().contains(query, Qt::CaseInsensitive))
                filtered.append(value);
        m_singers.replace(filtered);
    }
    m_users.replace(splitFollow(m_followRows, false));
}
bool FavoritesService::canAdjustSongOrder() const
{
    const auto row = m_library.likedRow();
    if (row < 0)
        return false;
    const auto id = m_library.cloudIdAt(row);
    // cloudIdAt requires account ownership and source_code==1;
    // reordering also requires a positive liked-list ID.
    return id.has_value() && id->value > 0;
}
quint64 FavoritesService::likedListId() const
{
    const auto row = m_library.likedRow();
    if (row < 0)
        return 0;
    const auto id = m_library.cloudIdAt(row);
    return id ? id->value : 0;
}
QString FavoritesService::likedTitle() const
{
    const auto row = m_library.likedRow();
    if (row < 0)
        return {};
    return m_library.data(m_library.index(row), UserPlaylistModel::TitleRole).toString();
}
QString FavoritesService::coverUrl() const
{
    const auto row = m_library.likedRow();
    return row < 0
               ? QString()
               : m_library.data(m_library.index(row), UserPlaylistModel::CoverUrlRole).toString();
}
void FavoritesService::invalidateVideos()
{
    if (m_closed)
        return;
    const auto ids = m_pending.keys();
    for (auto id : ids)
        if (m_pending[id].kind == PendingKind::Video) {
            m_pending.remove(id);
            m_api.cancel(id);
        }
    m_videosLoaded = m_videoExhausted = false;
    m_videoReceived = m_videoTotal = 0;
    m_videoRows = {};
    m_videos.replaceCards({});
    if (m_active && m_tab == 4)
        loadTab();
    emit changed();
}
void FavoritesService::setSingerQuery(const QString &query)
{
    if (m_closed || m_singerQuery == query)
        return;
    m_singerQuery = query;
    projectFollow();
    emit changed();
}
void FavoritesService::cancelMetadata()
{
    ++m_songGeneration;
    const auto tickets = m_metadataRequests.keys();
    m_metadataRequests.clear();
    m_metadataScheduled.clear();
    for (auto ticket : tickets)
        m_metadata.cancel(ticket);
}
void FavoritesService::completeSongs(const QJsonArray &entries)
{
    if (m_closed || !m_active || !m_network || !signedIn())
        return;
    QStringList ids;
    for (const auto &value : entries) {
        const auto entry = value.toObject();
        auto song = entry.value("song").toObject();
        song["metadata"] = entry.value("metadata");
        const auto id = song.value("id").toString();
        if (!id.isEmpty() && !m_metadataScheduled.contains(id) &&
            SongMetadataService::needsCompletion(song)) {
            m_metadataScheduled.insert(id);
            ids.append(id);
        }
    }
    if (const auto ticket = m_metadata.request(ids))
        m_metadataRequests.insert(ticket, {m_api.sessionRevision(), m_songGeneration});
}
void FavoritesService::resumeMetadata()
{
    completeSongs(m_songRows);
}
void FavoritesService::acceptMetadata(quint64 ticket, const QJsonArray &values)
{
    if (!m_metadataRequests.contains(ticket))
        return;
    const auto pending = m_metadataRequests.take(ticket);
    if (m_closed || !m_active || pending.session != m_api.sessionRevision() ||
        pending.generation != m_songGeneration)
        return;
    QHash<QString, QJsonObject> presentations;
    for (const auto &value : values) {
        const auto presentation = value.toObject();
        presentations.insert(presentation.value("id").toString(), presentation);
    }
    // Keep accepted source pages current before notifying the model. A later
    // page/global reorder must not restore the pre-completion presentation.
    for (qsizetype i = 0; i < m_songRows.size(); ++i) {
        auto entry = m_songRows[i].toObject();
        auto song = entry.value("song").toObject();
        const auto found = presentations.constFind(song.value("id").toString());
        if (found == presentations.cend())
            continue;
        song["metadata"] = entry.value("metadata");
        auto completed = SongMetadataService::completeSong(song, *found);
        entry["metadata"] = completed.take("metadata");
        entry["song"] = completed;
        m_songRows[i] = entry;
    }
    m_library.favoriteSongs()->applyPresentations(values);
    m_songs.applyPresentations(values);
    if (m_metadataRequests.isEmpty())
        publishCover();
}
void FavoritesService::publishCover()
{
    if (m_closed || !m_active || !m_likedLoaded || !m_likedId || !m_songRevision ||
        !m_songCoverConsistent)
        return;
    QString cover;
    std::optional<qint64> position;
    bool found = false;
    for (const auto &value : std::as_const(m_songRows)) {
        const auto entry = value.toObject();
        const auto candidate = entry.value("song").toObject().value("cover_url").toString();
        const QUrl url(candidate);
        if (!url.isValid() || (url.scheme() != "https" && url.scheme() != "http"))
            continue;
        const auto sort = entry.value("sort_index");
        const auto current =
            sort.isDouble() ? std::optional<qint64>(sort.toInteger()) : std::nullopt;
        if (!found || (current && (!position || *current < *position))) {
            cover = candidate;
            position = current;
            found = true;
        }
    }
    m_library.publishAutoCover({m_likedId}, *m_songRevision, cover, m_api.sessionRevision());
}
void FavoritesService::cancelAll()
{
    cancelMetadata();
    for (const auto id : m_pending.keys()) {
        m_pending.remove(id);
        m_api.cancel(id);
    }
}
void FavoritesService::reset()
{
    if (m_pending.isEmpty() && !m_likedLoaded && !m_followLoaded && !m_videosLoaded &&
        m_songRows.isEmpty() && m_metadataRequests.isEmpty())
        return;
    cancelAll();
    m_likedId = 0;
    m_songTotal = m_songReceived = 0;
    m_songRows = {};
    m_songHasMore = false;
    m_videoTotal = m_videoReceived = 0;
    m_videoRows = {};
    m_songs.clear();
    m_albums.replaceCards({});
    m_videos.replaceCards({});
    m_singers.replace({});
    m_users.replace({});
    m_followRows = {};
    m_singerQuery.clear();
    m_likedLoaded = m_followLoaded = m_videosLoaded = m_videoExhausted = false;
    m_error.clear();
    if (m_active && m_router.current() && m_router.current()->name() == "favorites")
        loadTab();
    emit changed();
}
void FavoritesService::accept(quint64 id, const QByteArray &bytes)
{
    if (!m_pending.contains(id))
        return;
    const auto pending = m_pending.take(id);
    if (m_closed || pending.session != m_api.sessionRevision())
        return;
    const auto object = QJsonDocument::fromJson(bytes).object();
    switch (pending.kind) {
    case PendingKind::Follow: {
        m_followRows = object.value("users").toArray();
        projectFollow();
        m_followLoaded = true;
        m_error.clear();
        break;
    }
    case PendingKind::Video: {
        const auto page = object.value("page").toObject();
        const auto items = page.value("items").toArray();
        for (const auto &value : items) {
            const auto item = value.toObject();
            // Video hash and related album-audio ID identify distinct resources.
            // Keep both in card metadata; do not substitute the audio hash for the video hash.
            QString hash;
            for (const auto &variantValue : item.value("variants").toArray()) {
                const auto variant = variantValue.toObject();
                if (variant.value("quality").toString() == QLatin1String("mkv_sd") &&
                    !variant.value("hash").toString().isEmpty())
                    hash = variant.value("hash").toString();
            }
            if (hash.isEmpty())
                for (const auto &variantValue : item.value("variants").toArray())
                    if (variantValue.toObject().value("quality").toString() == QLatin1String("mv"))
                        hash = variantValue.toObject().value("hash").toString();
            auto metadata = item.value("provider_extension").toObject();
            if (!hash.isEmpty())
                metadata.insert("hash", hash);
            if (item.value("related_song").isObject())
                metadata.insert("album_audio_id",
                                item.value("related_song").toObject().value("album_audio_id"));
            m_videoRows.append(QJsonObject{
                {"id", item.value("id")},
                {"title", item.value("title")},
                {"cover_url", item.value("cover_url")},
                {"creator", item.value("artist")},
                {"duration_ms", item.value("duration_ms")},
                {"metadata", metadata},
            });
        }
        m_videoReceived += quint64(items.size());
        if (page.value("total").isDouble()) {
            const auto total = quint64(qMax<qint64>(0, page.value("total").toInteger()));
            m_videoTotal = qMax(m_videoTotal, total);
        }
        m_videoExhausted = items.isEmpty() || (m_videoTotal > 0 && m_videoReceived >= m_videoTotal);
        m_videos.replaceCards(m_videoRows);
        m_videosLoaded = true;
        m_error.clear();
        break;
    }
    }
    emit changed();
}
void FavoritesService::fail(quint64 id, const QByteArray &bytes)
{
    if (!m_pending.contains(id))
        return;
    const auto pending = m_pending.take(id);
    if (m_closed || pending.session != m_api.sessionRevision())
        return;
    m_error = ApiClient::errorMessage(
        bytes, pending.kind == PendingKind::Follow  ? tr("关注列表加载失败")
               : pending.kind == PendingKind::Video ? tr("收藏视频加载失败")
                                                    : tr("收藏歌曲加载失败"));
    emit changed();
    if (m_active && (pending.kind == PendingKind::Follow || pending.kind == PendingKind::Video))
        emit notice(m_error, true);
}
void FavoritesService::shutdown()
{
    m_removeSession = 0;
    m_songs.setMetadataActive(false);
    if (m_closed)
        return;
    m_closed = true;
    cancelAll();
    emit changed();
}
} // namespace EchoMusic

namespace EchoMusic
{
void FavoritesService::registerRoutes(RouteController &router)
{
    router.registerPage({"favorites",
                         "/main/favorites",
                         QUrl("qrc:/qt/qml/EchoMusic/Favorites/FavoritesPage.qml"),
                         {"tab"},
                         true,
                         true,
                         false,
                         {{"service", QVariant::fromValue(this)}}});
}
} // namespace EchoMusic
