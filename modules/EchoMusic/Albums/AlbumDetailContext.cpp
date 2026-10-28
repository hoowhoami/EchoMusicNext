#include "AlbumDetailContext.h"
#include <QJsonDocument>
#include <QQmlEngine>

namespace EchoMusic
{
AlbumDetailContext::AlbumDetailContext(ApiClient &api, PlaybackService &playback,
                                       RouteContext &route, bool networkEnabled,
                                       CommentAuthorService *authors)
    : QObject(&route), m_api(api), m_route(route), m_actions(m_songs, playback, this),
      m_comments(api, route, networkEnabled, this, authors), m_network(networkEnabled)
{
    QQmlEngine::setObjectOwnership(this, QQmlEngine::CppOwnership);
    m_actions.setQueueContext(tr("专辑"), "queue:album:" + route.resourceId());
    connect(&api, &ApiClient::succeeded, this, &AlbumDetailContext::accept);
    connect(&api, &ApiClient::failed, this,
            [this](quint64 id, int, const QByteArray &bytes) { fail(id, bytes); });
    connect(&api, &ApiClient::sessionChanged, this, [this] {
        cancel();
        m_detail = {};
        m_songs.clear();
        m_complete = m_refreshing = false;
        m_detailError.clear();
        m_songError.clear();
        if (m_route.active())
            reload();
        else
            emit changed();
    });
    connect(&api, &ApiClient::closed, this, &AlbumDetailContext::shutdown);
    connect(&route, &RouteContext::activeChanged, this, &AlbumDetailContext::activate);
    connect(&route, &RouteContext::queryChanged, this, &AlbumDetailContext::activateComments);
    connect(this, &AlbumDetailContext::changed, this, &AlbumDetailContext::activateComments);
    activate();
}
AlbumDetailContext::~AlbumDetailContext()
{
    shutdown();
}
bool AlbumDetailContext::detailLoading() const
{
    for (const auto &pending : m_requests)
        if (!pending.page)
            return true;
    return false;
}
bool AlbumDetailContext::songsLoading() const
{
    for (const auto &pending : m_requests)
        if (pending.page)
            return true;
    return false;
}
void AlbumDetailContext::cancel()
{
    m_comments.suspend();
    ++m_generation;
    const auto ids = m_requests.keys();
    m_requests.clear();
    m_pages.clear();
    m_errors.clear();
    m_refreshSongs = {};
    for (auto id : ids)
        m_api.cancel(id);
}
void AlbumDetailContext::cancelSongRequests()
{
    const auto ids = m_requests.keys();
    for (auto id : ids) {
        if (!m_requests.value(id).page)
            continue; // Keep the independent detail request alive.
        m_requests.remove(id);
        m_api.cancel(id);
    }
    m_pages.clear();
    m_errors.clear();
}
void AlbumDetailContext::shutdown()
{
    m_songs.setMetadataActive(false);
    if (m_closed)
        return;
    m_closed = true;
    m_comments.shutdown();
    cancel();
    disconnect(&m_api, nullptr, this, nullptr);
    emit changed();
}
void AlbumDetailContext::activate()
{
    m_songs.setMetadataActive(!m_closed && m_network && m_route.active());
    if (m_closed)
        return;
    if (!m_route.active()) {
        cancel();
        emit changed();
    } else if (m_detail.isEmpty() || !m_complete || m_refreshing)
        reload();
}
void AlbumDetailContext::reload()
{
    if (m_closed)
        return;
    cancel();
    m_refreshing = m_complete;
    if (!m_refreshing)
        m_songs.clear();
    // A cached refresh must accept new pages even though the previous catalog
    // was complete; completion is re-established when the refresh reaches EOF.
    m_complete = false;
    m_nextPage = 1;
    m_detailError.clear();
    m_songError.clear();
    if (m_network && m_route.active()) {
        bool valid = false;
        const auto albumId = m_route.resourceId().toULongLong(&valid);
        if (!valid || !albumId) {
            m_detailError = tr("专辑标识无效");
        } else {
            // Start metadata and page one together. Later pages use batches of three,
            // independently of metadata completion.
            const auto id = m_api.albumDetail(albumId);
            if (id)
                m_requests.insert(id, {0, m_api.sessionRevision(), m_generation});
            else
                m_detailError = tr("专辑请求未能启动");
            requestPage(1);
        }
    }
    emit changed();
}
void AlbumDetailContext::requestPage(quint32 page)
{
    if (m_closed || !m_network || !m_route.active() || !m_songError.isEmpty())
        return;
    const auto id = m_api.albumSongs(m_route.resourceId().toULongLong(), page, m_pageSize);
    if (id)
        m_requests.insert(id, {page, m_api.sessionRevision(), m_generation});
    else
        m_errors.insert(page, tr("专辑歌曲请求未能启动"));
}
void AlbumDetailContext::requestBatch()
{
    // Bound resource loading without constraining provider totals
    // or rejecting pages when counts change.
    if (m_nextPage > 100) {
        m_songError = tr("分页超过最大页数限制（100），请重新加载");
        m_refreshing = false;
        return;
    }
    for (quint32 page = m_nextPage; page < m_nextPage + 3 && page <= 100; ++page)
        requestPage(page);
    // requestPage may be unable to start a request (disabled transport or an
    // inactive route). Only continue when this batch produced a page/error;
    // otherwise consumeBatch -> requestBatch would recurse without progress.
    if (!songsLoading() && (m_pages.contains(m_nextPage) || m_errors.contains(m_nextPage)))
        consumeBatch();
}
void AlbumDetailContext::accept(quint64 id, const QByteArray &bytes)
{
    if (!m_requests.contains(id))
        return;
    const auto pending = m_requests.take(id);
    if (m_closed || pending.session != m_api.sessionRevision() ||
        pending.generation != m_generation || !m_route.active())
        return;
    // A short page ended the album; discard sibling pages from the same batch
    // instead of appending data beyond the discovered end.
    if (pending.page && m_complete)
        return;
    QJsonParseError parse;
    const auto doc = QJsonDocument::fromJson(bytes, &parse);
    if (parse.error != QJsonParseError::NoError || (!doc.isObject() && !doc.isNull())) {
        if (pending.page)
            finishPage(pending.page, {}, tr("专辑歌曲数据无法解码"));
        else
            m_detailError = tr("专辑详情数据无法解码");
    } else if (!pending.page) {
        if (doc.object().isEmpty())
            m_detailError = tr("未找到此专辑");
        else {
            m_detail = doc.object();
            m_detailError.clear();
            m_actions.setQueueContext(m_detail["title"].toString(),
                                      "queue:album:" + m_route.resourceId());
        }
    } else
        finishPage(pending.page, doc.object());
    emit changed();
}
void AlbumDetailContext::finishPage(quint32 page, const QJsonObject &value, const QString &error)
{
    if (error.isEmpty())
        m_pages.insert(page, value["page"].toObject());
    else
        m_errors.insert(page, error);
    if (!songsLoading())
        consumeBatch();
}
void AlbumDetailContext::consumeBatch()
{
    while (m_pages.contains(m_nextPage) || m_errors.contains(m_nextPage)) {
        if (m_errors.contains(m_nextPage)) {
            m_songError = m_errors.take(m_nextPage);
            // A failed refresh keeps the previous complete catalog playable.
            if (m_refreshing)
                m_complete = true;
            m_refreshing = false;
            m_refreshSongs = {};
            m_pages.clear();
            m_errors.clear();
            return;
        }
        const auto items = m_pages.take(m_nextPage)["items"].toArray();
        QJsonArray songs;
        for (const auto &value : items) {
            const auto entry = value.toObject();
            auto song = entry["song"].toObject();
            auto metadata = entry["metadata"].toObject();
            // Disc/position and the original flag are independent optional
            // facts. Preserve null and raw codes through queue/history SQL.
            for (const auto &key : {"disc", "position", "has_accompaniment", "accompaniment_code"})
                metadata.insert(key, entry.value(key));
            song["metadata"] = metadata;
            songs.append(song);
        }
        if (m_refreshing) {
            for (const auto &song : songs)
                m_refreshSongs.append(song);
        } else
            m_songs.appendSongs(songs);
        ++m_nextPage;
        // Use returned page length for EOF. A repeated full page is not EOF;
        // deduplication belongs to the model.
        if (items.size() < int(m_pageSize)) {
            if (m_refreshing)
                m_songs.replaceSongs(m_refreshSongs, true);
            m_refreshSongs = {};
            m_complete = true;
            m_refreshing = false;
            cancelSongRequests();
            return;
        }
    }
    requestBatch();
}
void AlbumDetailContext::fail(quint64 id, const QByteArray &bytes)
{
    if (!m_requests.contains(id))
        return;
    const auto pending = m_requests.take(id);
    if (m_closed || pending.session != m_api.sessionRevision() ||
        pending.generation != m_generation || !m_route.active())
        return;
    const auto message =
        QJsonDocument::fromJson(bytes).object()["message"].toString(tr("专辑加载失败"));
    if (pending.page) {
        finishPage(pending.page, {}, message);
    } else {
        m_detailError = message;
    }
    emit changed();
    emit notice(message, true);
}
} // namespace EchoMusic

namespace EchoMusic
{
void AlbumDetailContext::activateComments()
{
    if (m_closed)
        return;
    if (!m_route.active() || m_route.query().value("tab").toString() != "comments") {
        m_comments.suspend();
        return;
    }
    const auto id = m_route.resourceId();
    m_comments.configure(ApiClient::CommentResource::Album, id, m_detail.value("title").toString());
    m_comments.start();
}
} // namespace EchoMusic
