#include "ArtistDetailContext.h"
#include <QJsonDocument>
#include <QQmlEngine>

namespace EchoMusic
{
ArtistDetailContext::ArtistDetailContext(ApiClient &api, PlaybackService &playback,
                                         RouteContext &route, ShellSettings &settings,
                                         bool networkEnabled)
    : QObject(&route), m_api(api), m_route(route), m_settings(settings),
      m_actions(m_songs, playback, this), m_network(networkEnabled)
{
    m_sort = settings.artistSongSort();
    m_albumSort = settings.artistAlbumSort();
    QQmlEngine::setObjectOwnership(this, QQmlEngine::CppOwnership);
    m_actions.setQueueContext(tr("歌手"), "queue:artist:" + route.resourceId());
    connect(&api, &ApiClient::succeeded, this, &ArtistDetailContext::accept);
    connect(&api, &ApiClient::failed, this,
            [this](quint64 id, int, const QByteArray &bytes) { fail(id, bytes); });
    connect(&api, &ApiClient::sessionChanged, this, [this] {
        cancel();
        cancelAlbumRequests();
        m_detail = {};
        m_songs.clear();
        m_albumCards = {};
        m_albums.replaceCards({});
        m_complete = false;
        m_albumComplete = false;
        m_detailError.clear();
        m_songError.clear();
        m_albumError.clear();
        if (m_route.active()) {
            reload();
            // A tab the user already opened reloads under the new session.
            if (m_albumsRequested)
                ensureAlbums();
        } else
            emit changed();
    });
    connect(&route, &RouteContext::activeChanged, this, &ArtistDetailContext::activate);
    activate();
}
void ArtistDetailContext::attachProfile(ProfileService &profile)
{
    m_profile = &profile;
    connect(&profile, &ProfileService::changed, this, &ArtistDetailContext::changed);
    connect(profile.account(), &AccountService::sessionAdopted, this, [this] {
        if (m_route.active()) {
            m_profile->ensureArtistFollows();
            if (interactionAvailable())
                m_profile->blacklist()->ensureSingerIndex();
        }
    });
    connect(profile.blacklist(), &ContentBlacklistService::changed, this,
            &ArtistDetailContext::changed);
    connect(&m_route, &RouteContext::activeChanged, this, [this] {
        if (m_route.active()) {
            m_profile->ensureArtistFollows();
            if (interactionAvailable())
                m_profile->blacklist()->ensureSingerIndex();
        }
    });
    profile.ensureArtistFollows();
    if (interactionAvailable())
        profile.blacklist()->ensureSingerIndex();
}
ArtistDetailContext::~ArtistDetailContext()
{
    shutdown();
}
bool ArtistDetailContext::detailLoading() const
{
    for (const auto &pending : m_requests)
        if (!pending.page)
            return true;
    return false;
}
bool ArtistDetailContext::songsLoading() const
{
    for (const auto &pending : m_requests)
        if (pending.page)
            return true;
    return false;
}
bool ArtistDetailContext::albumsLoading() const
{
    return !m_albumRequests.isEmpty();
}
void ArtistDetailContext::cancel()
{
    ++m_generation;
    const auto ids = m_requests.keys();
    m_requests.clear();
    m_pages.clear();
    m_errors.clear();
    for (auto id : ids)
        m_api.cancel(id);
}
void ArtistDetailContext::cancelSongRequests()
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
void ArtistDetailContext::shutdown()
{
    m_songs.setMetadataActive(false);
    if (m_closed)
        return;
    m_closed = true;
    cancel();
    cancelAlbumRequests();
    disconnect(&m_api, nullptr, this, nullptr);
    emit changed();
}
void ArtistDetailContext::activate()
{
    m_songs.setMetadataActive(!m_closed && m_network && m_route.active());
    if (m_closed)
        return;
    if (!m_route.active()) {
        cancel();
        cancelAlbumRequests();
        emit changed();
    } else {
        // Each mounted route keeps the order of its loaded catalog. On return,
        // adopt the shared preference before requesting data, as EchoMusic does.
        if (m_detail.isEmpty()) {
            m_sort = m_settings.artistSongSort();
            reload();
        } else if (m_sort != m_settings.artistSongSort())
            setSort(m_settings.artistSongSort());
        else if (!m_complete)
            reload();
        if (m_albumSort != m_settings.artistAlbumSort())
            setAlbumSort(m_settings.artistAlbumSort());
        else if (m_route.query().value("tab").toString() == "albums")
            ensureAlbums();
    }
}
void ArtistDetailContext::setSort(const QString &sort)
{
    if (m_closed || (sort != "new" && sort != "hot"))
        return;
    m_settings.setArtistSongSort(sort);
    if (sort == m_sort)
        return;
    m_sort = sort;
    // Reset the song table when provider sort changes. Cached pages from
    // another sort cannot be merged into the new catalog.
    cancelSongRequests();
    m_songs.clear();
    m_complete = false;
    m_songError.clear();
    m_nextPage = 1;
    if (m_network && m_route.active())
        requestPage(1);
    emit changed();
}
void ArtistDetailContext::cancelAlbumRequests()
{
    const auto ids = m_albumRequests.keys();
    m_albumRequests.clear();
    m_albumPages.clear();
    m_albumErrors.clear();
    for (auto id : ids)
        m_api.cancel(id);
}
void ArtistDetailContext::ensureAlbums()
{
    if (m_closed || m_albumComplete || albumsLoading())
        return;
    m_albumsRequested = true;
    m_albumError.clear();
    if (m_network && m_route.active())
        requestAlbumPage(1);
    emit changed();
}
void ArtistDetailContext::setAlbumSort(const QString &sort)
{
    if (m_closed || (sort != "hot" && sort != "new"))
        return;
    m_settings.setArtistAlbumSort(sort);
    if (sort == m_albumSort)
        return;
    m_albumSort = sort;
    m_albumsRequested = true;
    cancelAlbumRequests();
    m_albumCards = {};
    m_albums.replaceCards({});
    m_albumComplete = false;
    m_albumNextPage = 1;
    m_albumError.clear();
    if (m_network && m_route.active() && m_route.query().value("tab").toString() == "albums")
        requestAlbumPage(1);
    emit changed();
}
void ArtistDetailContext::requestAlbumPage(quint32 page)
{
    if (m_closed || !m_network || !m_route.active())
        return;
    const auto id =
        m_api.artistAlbums(m_route.resourceId().toULongLong(), page, m_albumPageSize, m_albumSort);
    if (id)
        m_albumRequests.insert(id, {page, m_api.sessionRevision(), m_generation});
    else
        m_albumErrors.insert(page, tr("歌手专辑请求未能启动"));
}
void ArtistDetailContext::requestAlbumBatch()
{
    if (m_albumNextPage > 1000) {
        m_albumError = tr("专辑分页超过最大页数限制（1000），请重新加载");
        return;
    }
    for (quint32 page = m_albumNextPage; page < m_albumNextPage + 3 && page <= 1000; ++page)
        requestAlbumPage(page);
    if (!albumsLoading() &&
        (m_albumPages.contains(m_albumNextPage) || m_albumErrors.contains(m_albumNextPage)))
        consumeAlbumBatch();
}
void ArtistDetailContext::finishAlbumPage(quint32 page, const QJsonObject &value,
                                          const QString &error)
{
    if (error.isEmpty())
        m_albumPages.insert(page, value);
    else
        m_albumErrors.insert(page, error);
    if (!albumsLoading())
        consumeAlbumBatch();
}
void ArtistDetailContext::consumeAlbumBatch()
{
    while (m_albumPages.contains(m_albumNextPage) || m_albumErrors.contains(m_albumNextPage)) {
        if (m_albumErrors.contains(m_albumNextPage)) {
            m_albumError = m_albumErrors.take(m_albumNextPage);
            m_albumPages.clear();
            m_albumErrors.clear();
            m_albums.replaceCards(m_albumCards);
            return;
        }
        const auto page = m_albumPages.take(m_albumNextPage);
        const auto items = page["items"].toArray();
        for (const auto &entry : items) {
            const auto album = entry.toObject();
            QStringList names;
            const auto artists = album["artists"].toArray();
            for (const auto &artist : artists) {
                const auto name = artist.toObject()["name"].toString();
                if (!name.isEmpty())
                    names.append(name);
            }
            const auto creator = names.join(" / ");
            const auto published = album["published_at"].toString();
            QJsonObject card;
            card["id"] = album["id"];
            card["title"] = album["title"];
            card["creator"] = creator;
            card["cover_url"] = album["cover_url"];
            card["extra_text"] =
                creator + (!creator.isEmpty() && !published.isEmpty() ? " • " : "") + published;
            card["metadata"] = QJsonObject{{"published_at", published}, {"artists", artists}};
            m_albumCards.append(card);
        }
        ++m_albumNextPage;
        if (items.size() < int(m_albumPageSize)) {
            m_albumComplete = true;
            cancelAlbumRequests();
            m_albums.replaceCards(m_albumCards);
            return;
        }
    }
    m_albums.replaceCards(m_albumCards);
    requestAlbumBatch();
}
void ArtistDetailContext::reload()
{
    if (m_closed)
        return;
    cancel();
    m_songs.clear();
    m_complete = false;
    m_nextPage = 1;
    m_detailError.clear();
    m_songError.clear();
    if (m_network && m_route.active()) {
        bool valid = false;
        const auto artistId = m_route.resourceId().toULongLong(&valid);
        if (!valid || !artistId) {
            m_detailError = tr("歌手标识无效");
        } else {
            const auto id = m_api.artistDetail(artistId);
            if (id)
                m_requests.insert(id, {0, m_api.sessionRevision(), m_generation});
            else
                m_detailError = tr("歌手详情请求未能启动");
            requestPage(1);
        }
    }
    emit changed();
}
void ArtistDetailContext::requestPage(quint32 page)
{
    if (m_closed || !m_network || !m_route.active() || !m_songError.isEmpty())
        return;
    const auto id = m_api.artistSongs(m_route.resourceId().toULongLong(), page, m_pageSize, m_sort);
    if (id)
        m_requests.insert(id, {page, m_api.sessionRevision(), m_generation});
    else
        m_errors.insert(page, tr("歌手歌曲请求未能启动"));
}
void ArtistDetailContext::requestBatch()
{
    if (m_nextPage > 1000) {
        m_songError = tr("分页超过最大页数限制（1000），请重新加载");
        return;
    }
    for (quint32 page = m_nextPage; page < m_nextPage + 3 && page <= 1000; ++page)
        requestPage(page);
    if (!songsLoading() && (m_pages.contains(m_nextPage) || m_errors.contains(m_nextPage)))
        consumeBatch();
}
void ArtistDetailContext::accept(quint64 id, const QByteArray &bytes)
{
    if (m_albumRequests.contains(id)) {
        const auto pending = m_albumRequests.take(id);
        if (m_closed || pending.session != m_api.sessionRevision() ||
            pending.generation != m_generation || !m_route.active())
            return;
        if (pending.page && m_albumComplete)
            return;
        QJsonParseError parse;
        const auto doc = QJsonDocument::fromJson(bytes, &parse);
        if (parse.error != QJsonParseError::NoError || !doc.isObject())
            finishAlbumPage(pending.page, {}, tr("歌手专辑数据无法解码"));
        else
            finishAlbumPage(pending.page, doc.object());
        emit changed();
        return;
    }
    if (!m_requests.contains(id))
        return;
    const auto pending = m_requests.take(id);
    if (m_closed || pending.session != m_api.sessionRevision() ||
        pending.generation != m_generation || !m_route.active())
        return;
    // A short page established EOF; discard sibling page requests already in
    // flight instead of appending pages beyond the discovered end.
    if (pending.page && m_complete)
        return;
    QJsonParseError parse;
    const auto doc = QJsonDocument::fromJson(bytes, &parse);
    if (parse.error != QJsonParseError::NoError || (!doc.isObject() && !doc.isNull())) {
        if (pending.page)
            finishPage(pending.page, {}, tr("歌手歌曲数据无法解码"));
        else
            m_detailError = tr("歌手详情数据无法解码");
    } else if (!pending.page) {
        if (doc.object().isEmpty()) {
            m_detailError = tr("未找到此歌手");
        } else {
            m_detail = doc.object();
            if (interactionAvailable())
                m_profile->blacklist()->ensureSingerIndex();
            m_detailError.clear();
            m_actions.setQueueContext(m_detail["name"].toString(),
                                      "queue:artist:" + m_route.resourceId());
        }
    } else {
        finishPage(pending.page, doc.object());
    }
    emit changed();
}
void ArtistDetailContext::finishPage(quint32 page, const QJsonObject &value, const QString &error)
{
    if (error.isEmpty())
        m_pages.insert(page, value);
    else
        m_errors.insert(page, error);
    if (!songsLoading())
        consumeBatch();
}
void ArtistDetailContext::consumeBatch()
{
    while (m_pages.contains(m_nextPage) || m_errors.contains(m_nextPage)) {
        if (m_errors.contains(m_nextPage)) {
            m_songError = m_errors.take(m_nextPage);
            m_pages.clear();
            m_errors.clear();
            return;
        }
        const auto page = m_pages.take(m_nextPage);
        const auto items = page["items"].toArray();
        m_songs.appendSongs(items);
        ++m_nextPage;
        // Use returned length for pagination, independent of the provider total.
        // A repeated full page is not EOF; duplicate rows retain distinct view keys.
        if (items.size() < int(m_pageSize)) {
            m_complete = true;
            cancelSongRequests();
            return;
        }
    }
    requestBatch();
}
void ArtistDetailContext::fail(quint64 id, const QByteArray &bytes)
{
    if (m_albumRequests.contains(id)) {
        const auto pending = m_albumRequests.take(id);
        if (m_closed || pending.session != m_api.sessionRevision() ||
            pending.generation != m_generation || !m_route.active())
            return;
        const auto message =
            QJsonDocument::fromJson(bytes).object()["message"].toString(tr("歌手专辑加载失败"));
        finishAlbumPage(pending.page, {}, message);
        emit changed();
        emit notice(message, true);
        return;
    }
    if (!m_requests.contains(id))
        return;
    const auto pending = m_requests.take(id);
    if (m_closed || pending.session != m_api.sessionRevision() ||
        pending.generation != m_generation || !m_route.active())
        return;
    const auto message =
        QJsonDocument::fromJson(bytes).object()["message"].toString(tr("歌手加载失败"));
    if (pending.page) {
        finishPage(pending.page, {}, message);
    } else {
        m_detailError = message;
    }
    emit changed();
    emit notice(message, true);
}
} // namespace EchoMusic
