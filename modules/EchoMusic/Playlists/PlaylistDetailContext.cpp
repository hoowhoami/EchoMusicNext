#include <QQmlEngine>
#include "PlaylistDetailContext.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QUrl>
#include <algorithm>
namespace EchoMusic
{
PlaylistDetailContext::PlaylistDetailContext(ApiClient &api, PlaybackService &playback,
                                             UserPlaylistModel &library,
                                             PlaylistMutationService &mutations,
                                             SongMetadataService &metadata, RouteContext &route,
                                             bool networkEnabled, CommentAuthorService *authors)
    : QObject(&route), m_api(api), m_playback(playback), m_library(library), m_metadata(metadata),
      m_mutations(mutations), m_route(route), m_actions(m_songs, playback, this),
      m_comments(api, route, networkEnabled, this, authors), m_network(networkEnabled)
{
    // QML borrows this application-owned object, including explicit invokable
    // returns. GC and engine teardown must never delete a C++ member/context.
    QQmlEngine::setObjectOwnership(this, QQmlEngine::CppOwnership);

    connect(&api, &ApiClient::succeeded, this, &PlaylistDetailContext::accept);
    connect(&api, &ApiClient::failed, this,
            [this](quint64 id, int, const QByteArray &bytes) { fail(id, bytes); });
    connect(&api, &ApiClient::sessionChanged, this, &PlaylistDetailContext::reload);
    connect(&api, &ApiClient::closed, this, &PlaylistDetailContext::shutdown);
    connect(&route, &RouteContext::activeChanged, this, &PlaylistDetailContext::activate);
    connect(&library, &UserPlaylistModel::changed, this, [this] {
        if (m_resolving)
            resolveSource();
        emit changed();
    });
    connect(&library, &UserPlaylistModel::coverChanged, this, [this](const QString &global) {
        if (global == m_detail["global_id"].toString())
            emit changed();
    });
    connect(&metadata, &SongMetadataService::completed, this,
            [this](quint64 ticket, const QJsonArray &values) {
                if (!m_metadataRequests.contains(ticket))
                    return;
                const auto pending = m_metadataRequests.take(ticket);
                if (m_closed || !m_route.active() || pending.session != m_api.sessionRevision() ||
                    pending.generation != m_generation)
                    return;
                m_songs.applyPresentations(values);
                // Keep the ordered source entries current so a later page/reorder cannot
                // restore the pre-completion cover/artist snapshot.
                QHash<QString, QJsonObject> presentations;
                for (const auto &v : values) {
                    const auto p = v.toObject();
                    presentations.insert(p["id"].toString(), p);
                }
                for (auto &entry : m_cloudEntries) {
                    const auto song = entry["song"].toObject();
                    const auto id = song["id"].toString();
                    if (presentations.contains(id)) {
                        auto full = song;
                        full["metadata"] = entry["metadata"];
                        const auto completed =
                            SongMetadataService::completeSong(full, presentations[id]);
                        auto updated = completed;
                        updated.remove("metadata");
                        entry["song"] = updated;
                        entry["metadata"] = completed["metadata"];
                    }
                }
                if (m_complete && m_metadataRequests.isEmpty())
                    publishCover();
            });
    connect(&mutations, &PlaylistMutationService::tracksInvalidated, this, [this](quint64 id) {
        if (m_cloud && m_cloud->value == id)
            reload();
    });
    connect(&mutations, &PlaylistMutationService::libraryInvalidated, this, [this] {
        // Membership writes refresh the account catalog, not public songs/detail.
        if (m_collections && m_collections->writingCollection())
            return;
        m_metadataDirty = true;
        QMetaObject::invokeMethod(
            this,
            [this] {
                if (m_metadataDirty && m_route.active() && !m_closed)
                    reload();
            },
            Qt::QueuedConnection);
    });
    connect(&route, &RouteContext::queryChanged, this, &PlaylistDetailContext::activateComments);
    connect(this, &PlaylistDetailContext::changed, this, &PlaylistDetailContext::activateComments);
    activate();
}
PlaylistDetailContext::~PlaylistDetailContext()
{
    shutdown();
}
void PlaylistDetailContext::cancelPageRequests()
{
    // Completion already settled every page needed by the catalog. Cancelling
    // sibling requests prevents late pages from extending past EOF.
    const auto ids = m_requests.keys();
    for (auto id : ids) {
        if (!m_requests.value(id).page)
            continue;
        m_requests.remove(id);
        m_api.cancel(id);
    }
    m_pages.clear();
    m_pageErrors.clear();
}
void PlaylistDetailContext::shutdown()
{
    if (m_closed)
        return;
    m_closed = true;
    m_comments.shutdown();
    cancel();
    disconnect(&m_api, nullptr, this, nullptr);
}
void PlaylistDetailContext::cancel()
{
    m_comments.suspend();
    ++m_generation;
    for (auto ticket : m_metadataRequests.keys())
        m_metadata.cancel(ticket);
    m_metadataRequests.clear();
    const auto ids = m_requests.keys();
    m_requests.clear();
    for (auto id : ids)
        m_api.cancel(id);
    m_pages.clear();
    m_pageErrors.clear();
    m_resolving = false;
}
void PlaylistDetailContext::activate()
{
    if (m_closed)
        return;
    if (!m_route.active()) {
        cancel();
        emit changed();
        return;
    }
    if (m_detail.isEmpty() || m_metadataDirty) {
        reload();
        return;
    }
    // A kept-alive page can have finished pagination while optional metadata
    // was canceled on hide. Resume those consumers without reloading tracks.
    if (m_network && m_metadataRequests.isEmpty())
        if (const auto ticket = m_metadata.request(m_songs.incompletePresentationIds()))
            m_metadataRequests.insert(ticket, {0, m_api.sessionRevision(), m_generation});
    if (!m_complete && m_error.isEmpty()) {
        resolveSource();
    }
}
void PlaylistDetailContext::reload()
{
    if (m_closed)
        return;
    cancel();
    m_metadataDirty = false;
    m_cloudEntries.clear();
    m_cloudEntryIds.clear();
    m_songs.clear();
    m_detail = {};
    m_cloud.reset();
    m_publicId.clear();
    m_revision.reset();
    m_total = 0;
    m_received = 0;
    m_nextPage = 1;
    m_complete = false;
    m_error.clear();
    if (m_route.active() && m_network) {
        const auto id = m_api.playlistDetails({{m_route.resourceId()}},
                                              {AuthenticationMode::CurrentAccount, false});
        if (id)
            m_requests.insert(id, {0, m_api.sessionRevision(), m_generation});
        else
            m_error = tr("歌单请求未能启动");
    }
    emit changed();
}
quint64 PlaylistDetailContext::orderListId() const
{
    const auto global = m_detail["global_id"].toString();
    if (m_closed || global.isEmpty() || !m_api.currentAccountUserId())
        return 0;
    for (int row = 0; row < m_library.rowCount(); ++row) {
        if (m_library.index(row).data(UserPlaylistModel::GlobalIdRole).toString() != global)
            continue;
        const auto id = m_library.cloudIdAt(row);
        // The numeric editor property requires an ID exactly representable by QML.
        return id && id->value <= 9007199254740991ULL ? id->value : 0;
    }
    return 0;
}
void PlaylistDetailContext::resolveSource()
{
    if (m_closed || !m_route.active() || !m_network || m_detail.isEmpty())
        return;
    m_resolving = false;
    m_cloud.reset();
    const auto global = m_detail["global_id"].toString();
    for (int row = 0; row < m_library.rowCount(); ++row) {
        const auto index = m_library.index(row);
        if (index.data(UserPlaylistModel::GlobalIdRole).toString() != global)
            continue;
        m_cloud = m_library.cloudIdAt(row);
        break;
    }
    const auto creator = quint64(m_detail["creator_id"].toInteger());
    if (!m_cloud && creator && creator == m_api.currentAccountUserId()) {
        // Ownership alone is insufficient for the account-local cloud list ID.
        // Read the canonical user library; never decode it from collection_3_*.
        if (m_library.state() == UserPlaylistModel::State::Loading)
            m_resolving = true;
        else if (m_library.canFetchMore({})) {
            m_resolving = true;
            m_library.fetchMore({});
        } else {
            failMessage(m_library.errorMessage().isEmpty()
                            ? tr("当前账号目录中未找到此歌单，请刷新后重试")
                            : m_library.errorMessage());
            return;
        }
        emit changed();
        return;
    }
    m_pageSize = m_cloud ? 300 : 200;
    m_publicId = m_detail["creator_global_id"].toString();
    if (m_publicId.isEmpty())
        m_publicId = global;
    m_actions.setQueueContext(m_detail["title"].toString(), "queue:playlist:" + global);
    requestPage(m_nextPage);
    emit changed();
}
void PlaylistDetailContext::requestPage(quint32 page)
{
    if (m_closed || !m_network || !m_route.active() || !m_error.isEmpty())
        return;
    const auto id = m_cloud
                        ? m_api.cloudPlaylistTracks(*m_cloud, page, m_pageSize)
                        : m_api.publicPlaylistTracks({m_publicId}, page, m_pageSize,
                                                     {AuthenticationMode::CurrentAccount, false});
    if (id)
        m_requests.insert(id, {page, m_api.sessionRevision(), m_generation});
    else
        failMessage(tr("歌曲请求未能启动"));
}
void PlaylistDetailContext::accept(quint64 id, const QByteArray &bytes)
{
    if (!m_requests.contains(id))
        return;
    const auto pending = m_requests.take(id);
    if (m_closed || pending.session != m_api.sessionRevision() ||
        pending.generation != m_generation || !m_route.active())
        return;
    if (pending.page && m_complete)
        return;
    const auto doc = QJsonDocument::fromJson(bytes);
    if (!pending.page) {
        if (doc.array().isEmpty()) {
            failMessage(tr("未找到此歌单"));
            return;
        }
        m_detail = doc.array().first().toObject();
        m_total = quint64(m_detail["song_count"].toInteger());
        resolveSource();
    } else {
        auto page = doc.object();
        if (m_cloud) {
            const auto revision = quint64(page["revision"].toInteger());
            if (m_revision && *m_revision != revision) {
                failMessage(tr("歌单在加载期间已改变，请重新加载"));
                return;
            }
            m_revision = revision;
            page = page["page"].toObject();
        }
        m_pages.insert(pending.page, page);
        // Load page one, then bounded batches of three;
        // accept pages in request order after each batch settles.
        if (m_requests.isEmpty())
            consumeBatch();
    }
    emit changed();
}
void PlaylistDetailContext::consumeBatch()
{
    while (m_pages.contains(m_nextPage) || m_pageErrors.contains(m_nextPage)) {
        if (m_pageErrors.contains(m_nextPage)) {
            failMessage(m_pageErrors.take(m_nextPage));
            return;
        }
        const auto page = m_pages.take(m_nextPage);
        const auto items = page["items"].toArray();
        m_received += quint64(items.size());
        QJsonArray songs;
        const auto total = quint64(page["total"].toInteger());
        m_total = total;
        QJsonArray accepted;
        QSet<QString> acceptedIds;
        for (const auto &v : items) {
            const auto entry = v.toObject();
            auto song = entry["song"].toObject();
            song.insert("metadata", entry["metadata"]);
            songs.append(song);
            const auto id = song["id"].toString();
            if (!acceptedIds.contains(id) && !m_songs.track(id)) {
                acceptedIds.insert(id);
                accepted.append(song);
            }
        }
        if (m_cloud) {
            // Cloud order is supplied by the server sort index, not transport
            // array order. Sorting inside each page is insufficient; merge all
            // loaded entries below before replacing only if their order differs.
            for (const auto &v : items) {
                const auto entry = v.toObject();
                const auto id = entry["song"].toObject()["id"].toString();
                if (!m_cloudEntryIds.contains(id)) {
                    m_cloudEntryIds.insert(id);
                    m_cloudEntries.append(entry);
                }
            }
            std::stable_sort(m_cloudEntries.begin(), m_cloudEntries.end(),
                             [](const auto &a, const auto &b) {
                                 return a["sort_index"].toInteger() < b["sort_index"].toInteger();
                             });
            songs = {};
            for (const auto &entry : m_cloudEntries) {
                auto song = entry["song"].toObject();
                song.insert("metadata", entry["metadata"]);
                songs.append(song);
            }
            bool prefix = m_songs.totalCount() <= songs.size();
            for (int i = 0; prefix && i < m_songs.totalCount(); ++i)
                prefix = m_songs.sourceItem(i)["resourceId"].toString() ==
                         songs[i].toObject()["id"].toString();
            if (prefix) {
                QJsonArray added;
                for (int i = m_songs.totalCount(); i < songs.size(); ++i)
                    added.append(songs[i]);
                m_songs.appendSongs(added);
            } else
                m_songs.replaceSongs(songs, true);
        } else
            m_songs.appendSongs(songs);
        // Only newly accepted identities need optional presentation enrichment.
        completePage(accepted);
        ++m_nextPage;
        if (items.isEmpty() || (m_total > 0 && m_received >= m_total)) {
            m_complete = true;
            cancelPageRequests();
            publishCover();
            return;
        }
    }
    const auto first = m_nextPage;
    for (quint32 p = first; p < first + 3; ++p)
        requestPage(p);
}
void PlaylistDetailContext::completePage(const QJsonArray &songs)
{
    if (!m_network)
        return;
    QStringList ids;
    for (const auto &value : songs) {
        const auto song = value.toObject();
        if (SongMetadataService::needsCompletion(song))
            ids.append(song["id"].toString());
    }
    if (const auto ticket = m_metadata.request(ids))
        m_metadataRequests.insert(ticket, {0, m_api.sessionRevision(), m_generation});
}
void PlaylistDetailContext::publishCover()
{
    if (!m_cloud || !m_complete || !m_revision || !m_route.active())
        return;
    QString cover;
    // Entries already have stable sort ascending order, independent of arrival.
    // Use original covers first; enrichment only fills missing presentation.
    for (const auto &entry : m_cloudEntries) {
        const auto candidate = entry["song"].toObject()["cover_url"].toString();
        const QUrl url(candidate);
        if (url.isValid() && (url.scheme() == "https" || url.scheme() == "http")) {
            cover = candidate;
            break;
        }
    }
    m_library.publishAutoCover(*m_cloud, *m_revision, cover, m_api.sessionRevision());
}
void PlaylistDetailContext::fail(quint64 id, const QByteArray &bytes)
{
    if (!m_requests.contains(id))
        return;
    const auto pending = m_requests.take(id);
    if (m_closed || !m_route.active() || pending.session != m_api.sessionRevision() ||
        pending.generation != m_generation)
        return;
    const auto message =
        QJsonDocument::fromJson(bytes).object()["message"].toString(tr("歌单加载失败"));
    if (!pending.page) {
        failMessage(message);
        return;
    }
    m_pageErrors.insert(pending.page, message);
    emit notice(message, true);
    if (m_requests.isEmpty())
        consumeBatch();
    emit changed();
}
bool PlaylistDetailContext::removeSong(const QString &id)
{
    // 只有歌单所有者可删除曲目；写操作由共享服务串行执行。
    // 成功后通过 tracksInvalidated 重新加载服务端数据。
    if (m_closed || !m_cloud || !m_revision || !m_complete || !m_route.active())
        return false;
    quint64 file = 0;
    for (const auto &entry : m_cloudEntries)
        if (entry["song"].toObject()["id"].toString() == id) {
            file = quint64(entry["file_id"].toInteger());
            break;
        }
    if (!file)
        return false;
    return m_mutations.removeTracks(*m_cloud, {ApiClient::PlaylistTrackFileId{file}});
}
void PlaylistDetailContext::failMessage(const QString &message)
{
    cancel();
    m_error = message;
    emit changed();
    if (!m_closed && m_route.active())
        emit notice(message, true);
}
} // namespace EchoMusic

namespace EchoMusic
{
void PlaylistDetailContext::activateComments()
{
    if (m_closed)
        return;
    if (!m_route.active() || m_route.query().value("tab").toString() != "comments") {
        m_comments.suspend();
        return;
    }
    auto id = m_detail.value("global_id").toString();
    if (id.isEmpty())
        id = m_detail.value("creator_global_id").toString();
    if (id.isEmpty()) {
        const auto creator = m_detail.value("creator_id").toInteger();
        const auto list = m_detail.value("creator_list_id").toInteger();
        if (creator > 0 && list > 0)
            id = QString("collection_3_%1_%2_0").arg(creator).arg(list);
    }
    if (id.isEmpty())
        id = m_route.resourceId();
    m_comments.configure(ApiClient::CommentResource::Playlist, id,
                         m_detail.value("title").toString());
    m_comments.start();
}
} // namespace EchoMusic
