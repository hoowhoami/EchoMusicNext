#include "SearchModel.h"
#include "PlaybackLibraryActions.h"
#include "PlaybackService.h"
#include "SearchResultModel.h"
#include "SongCommands.h"
#include "SongMetadataService.h"
#include "SongRowFacts.h"
#include "StorageService.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QScopedValueRollback>
#include <QThread>
#include <utility>
#include <algorithm>

namespace EchoMusic
{
QVariantMap SearchModel::pluginMenuContext(const QString &id) const
{
    if (m_kind != Kind::Song && m_kind != Kind::Lyric)
        return {};
    for (const auto &row : active().rows)
        if (row.id == id)
            return {{"track", row.track.publicValue()},
                    {"selectedTracks", QVariantList{}},
                    {"selectionCount", 0},
                    {"truncated", false},
                    {"sourceId", "search"},
                    {"revision", QString::number(active().revision)}};
    return {};
}

namespace
{
constexpr quint32 PageSize = 30;
QString artistNames(const QJsonObject &object)
{
    QStringList names;
    for (const auto &value : object.value("artists").toArray())
        names.append(value.toObject().value("name").toString());
    return names.join(QStringLiteral(" / "));
}
QString durationText(const QJsonObject &object)
{
    const auto seconds = quint64(object.value("duration_ms").toDouble()) / 1000;
    return QStringLiteral("%1:%2").arg(seconds / 60).arg(seconds % 60, 2, 10, QLatin1Char('0'));
}
QString identifier(const QJsonValue &value)
{
    return value.isString() ? value.toString() : QString::number(quint64(value.toDouble()));
}
QString domainKind(SearchModel::Kind kind)
{
    switch (kind) {
    case SearchModel::Kind::Song:
        return QStringLiteral("songs");
    case SearchModel::Kind::Album:
        return QStringLiteral("albums");
    case SearchModel::Kind::Artist:
        return QStringLiteral("artists");
    case SearchModel::Kind::Playlist:
        return QStringLiteral("playlists");
    case SearchModel::Kind::Lyric:
        return QStringLiteral("lyrics");
    case SearchModel::Kind::Mv:
        return QStringLiteral("mvs");
    }
    return {};
}
ApiClient::SearchKind apiKind(SearchModel::Kind kind)
{
    switch (kind) {
    case SearchModel::Kind::Song:
        return ApiClient::SearchKind::Song;
    case SearchModel::Kind::Album:
        return ApiClient::SearchKind::Album;
    case SearchModel::Kind::Artist:
        return ApiClient::SearchKind::Artist;
    case SearchModel::Kind::Playlist:
        return ApiClient::SearchKind::Playlist;
    case SearchModel::Kind::Lyric:
        return ApiClient::SearchKind::Lyric;
    case SearchModel::Kind::Mv:
        return ApiClient::SearchKind::Mv;
    }
    return ApiClient::SearchKind::Song;
}
} // namespace
int HotSearchModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_groups.size());
}
QVariant HotSearchModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_groups.size())
        return {};
    const auto &group = m_groups.at(index.row());
    switch (role) {
    case Qt::UserRole + 1:
        return group.title;
    case Qt::UserRole + 2:
        return group.keywords;
    case Qt::UserRole + 3:
        return group.reasons;
    default:
        return {};
    }
}
QHash<int, QByteArray> HotSearchModel::roleNames() const
{
    return {{Qt::UserRole + 1, "groupTitle"},
            {Qt::UserRole + 2, "keywords"},
            {Qt::UserRole + 3, "reasons"}};
}
void HotSearchModel::replace(QList<Group> groups)
{
    beginResetModel();
    m_groups = std::move(groups);
    endResetModel();
}

SearchModel::SearchModel(ApiClient &api, QObject *parent)
    : QAbstractListModel(parent), m_api(api), m_hot(this)
{
    m_metadataRefresh.setSingleShot(true);
    connect(&m_metadataRefresh, &QTimer::timeout, this, &SearchModel::watchMetadata);
    connect(this, &QAbstractItemModel::modelReset, this, [this] { m_metadataRefresh.start(); });
    connect(this, &QAbstractItemModel::rowsInserted, this, [this] { m_metadataRefresh.start(); });
    m_results = new SearchResultModel(*this);
    m_suggestions = new SearchSuggestionModel(api, this);
    connect(&api, &ApiClient::succeeded, this, &SearchModel::succeeded);
    connect(&api, &ApiClient::failed, this, &SearchModel::failed);
    connect(&api, &ApiClient::sessionChanged, this, &SearchModel::resetSession);
}
void SearchModel::attachMetadata(SongMetadataService &service, bool network)
{
    m_metadata = &service;
    m_metadataNetwork = network;
    watchMetadata();
}
void SearchModel::watchMetadata()
{
    if (!m_metadata)
        return;
    QList<SongMetadataService::Observation> songs;
    QStringList rows;
    if (!m_closing)
        for (auto kind : {Kind::Song, Kind::Lyric}) {
            const auto &bucket = m_buckets[size_t(kind)];
            for (int i = 0; i < bucket.rows.size(); ++i) {
                const auto row = QString::number(int(kind)) + ":" + QString::number(i);
                songs.append({row, bucket.rows[i].track.toCatalog()});
                rows.append(row);
            }
        }
    refreshMetadata(rows);
    m_metadata->watch(
        this, songs, [this](const QStringList &changed) { refreshMetadata(changed); },
        m_metadataNetwork && m_pageActive);
}
void SearchModel::refreshMetadata(const QStringList &rows)
{
    if (!m_metadata || m_closing)
        return;
    bool changed = false;
    for (const auto &key : rows) {
        const auto pieces = key.split(':');
        if (pieces.size() != 2)
            continue;
        const auto kind = Kind(pieces[0].toInt());
        auto &bucket = m_buckets[size_t(kind)];
        const auto i = pieces[1].toInt();
        if (i < 0 || i >= bucket.rows.size())
            continue;
        auto &row = bucket.rows[i];
        if (!row.track.applyPresentation(m_metadata->complete(row.track.toCatalog())))
            continue;
        row.title = row.track.title;
        row.artists = row.track.artist;
        row.album = row.track.album;
        row.cover = row.track.cover;
        row.durationMs = row.track.durationMs;
        row.duration = durationText(row.track.toCatalog());
        if (kind == m_kind) {
            emit dataChanged(index(i), index(i),
                             {Title, ArtistText, AlbumText, CoverUrl, DurationMs, DurationText,
                              ArtistLinks, AlbumId});
            changed = true;
        }
    }
    if (changed)
        emit this->changed();
}
void SearchModel::resetSession()
{
    if (m_changingRows) {
        QMetaObject::invokeMethod(this, &SearchModel::resetSession, Qt::QueuedConnection);
        return;
    }
    if (m_closing)
        return;
    const auto keyword = m_keyword;
    const auto kind = m_kind;
    clearBuckets();
    const auto hot = std::exchange(m_hotRequest, 0);
    if (hot)
        m_api.cancel(hot);
    m_hot.replace({});
    emit changed();
    emit discoveryChanged();
    QMetaObject::invokeMethod(
        this,
        [this, keyword, kind] {
            if (!m_started || m_closing)
                return;
            if (m_pageActive || m_discoveryActive)
                reloadDiscovery();
            if (m_pageActive && !keyword.isEmpty() && m_keyword == keyword && m_kind == kind)
                search(keyword, kind);
        },
        Qt::QueuedConnection);
}

void SearchModel::attachPlayback(PlaybackService &playback)
{
    Q_ASSERT(!m_playback);
    m_playback = &playback;
    connect(&playback, &PlaybackService::changed, this, &SearchModel::playbackChanged);
    connect(&playback, &QObject::destroyed, this, &SearchModel::playbackChanged);
    connect(playback.songCommands(), &SongCommands::changed, this, &SearchModel::blacklistChanged);
    // favoriteRevision 依赖库动作会话；attachLibrary 可能晚于本调用装配。
    const auto bindLibrary = [this] {
        auto *actions = m_playback ? m_playback->libraryActions() : nullptr;
        if (!actions || m_libraryActions == actions)
            return;
        if (m_libraryActions)
            disconnect(m_libraryActions, nullptr, this, nullptr);
        m_libraryActions = actions;
        connect(actions, &PlaybackLibraryActions::changed, this, [this] {
            ++m_favoriteRevision;
            emit favoritesChanged();
        });
    };
    bindLibrary();
    connect(&playback, &PlaybackService::changed, this, bindLibrary);
    emit playbackChanged();
}
QString SearchModel::currentTrackId() const
{
    return m_playback ? m_playback->trackId() : QString();
}
bool SearchModel::playing() const
{
    return m_playback && m_playback->playing();
}
SearchModel::~SearchModel()
{
    shutdown();
}
int SearchModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(active().rows.size());
}
QVariant SearchModel::data(const QModelIndex &index, int role) const
{
    const auto &rows = active().rows;
    if (!index.isValid() || index.row() < 0 || index.row() >= rows.size())
        return {};
    const auto &row = rows.at(index.row());
    switch (role) {
    case Title:
        return row.title;
    case ArtistText:
        return row.artists;
    case ArtistLinks: {
        auto track = row.track;
        if (track.artist.isEmpty())
            track.artist = row.artists;
        return track.artistLinks();
    }
    case AlbumId:
        return row.track.albumId ? QString::number(row.track.albumId) : QString();
    case AlbumText:
        return row.album;
    case CoverUrl:
        return row.cover;
    case DurationText:
        return row.duration;
    case ExtraText:
        return row.extra;
    case ResourceId:
        return row.id;
    case DurationMs:
        return QVariant::fromValue(row.durationMs);
    case Original:
        return row.original;
    case PrivilegeTags:
        return row.badges;
    case QualityLabel:
        return row.quality;
    default:
        return {};
    }
}
QHash<int, QByteArray> SearchModel::roleNames() const
{
    return {
        {ArtistLinks, "artistLinks"},   {AlbumId, "albumId"},     {Title, "title"},
        {ArtistText, "artistText"},     {AlbumText, "albumText"}, {CoverUrl, "coverUrl"},
        {DurationText, "durationText"}, {ExtraText, "extraText"}, {ResourceId, "resourceId"},
        {DurationMs, "durationMs"},     {Original, "original"},   {PrivilegeTags, "privilegeTags"},
        {QualityLabel, "qualityLabel"}};
}
QStringList SearchModel::searchHistory() const
{
    return m_historyStore ? m_historyStore->searchHistory() : QStringList{};
}
void SearchModel::setHistoryStore(StorageService &storage)
{
    Q_ASSERT(!m_historyStore);
    m_historyStore = &storage;
    connect(&storage, &StorageService::searchHistoryChanged, this, &SearchModel::historyChanged);
    emit historyChanged();
}
void SearchModel::removeHistory(const QString &keyword)
{
    if (m_historyStore)
        m_historyStore->removeSearch(keyword);
}
void SearchModel::clearHistory()
{
    if (m_historyStore)
        m_historyStore->clearSearches();
}
void SearchModel::initialize()
{
    if (m_started || m_closing)
        return;
    m_started = true;
    m_suggestions->initialize();
    reloadDiscovery();
}
void SearchModel::setDiscoveryActive(bool active)
{
    if (m_discoveryActive == active || m_closing)
        return;
    m_discoveryActive = active;
    if (active && m_started && !m_hotRequest && m_hot.rowCount() == 0)
        reloadDiscovery();
    if (!active && !m_pageActive) {
        const auto hot = std::exchange(m_hotRequest, 0);
        if (hot) {
            m_api.cancel(hot);
            emit discoveryChanged();
        }
    }
}
void SearchModel::setPageActive(bool activePage)
{
    if (m_pageActive == activePage || m_closing)
        return;
    m_pageActive = activePage;
    watchMetadata();
    if (!activePage) {
        cancelRequests();
        const auto hot = m_discoveryActive ? 0 : std::exchange(m_hotRequest, 0);
        if (hot)
            m_api.cancel(hot);
        active().state = State::Idle;
        emit changed();
        emit discoveryChanged();
    }
}
void SearchModel::shutdown()
{
    if (std::exchange(m_closing, true))
        return;
    m_metadataRefresh.stop();
    if (m_metadata)
        m_metadata->unwatch(this);
    m_suggestions->shutdown();
    cancelRequests();
    const auto hot = std::exchange(m_hotRequest, 0);
    if (hot)
        m_api.cancel(hot);
}
void SearchModel::cancelRequests()
{
    for (auto &bucket : m_buckets) {
        const auto id = std::exchange(bucket.request, 0);
        if (id)
            m_api.cancel(id);
        if (bucket.state == State::Loading || bucket.state == State::LoadingMore)
            bucket.state = bucket.loaded ? State::Ready : State::Idle;
    }
}
void SearchModel::clearBuckets()
{
    cancelRequests();
    if (m_metadata)
        m_metadata->unwatch(this);
    QScopedValueRollback<bool> changing(m_changingRows, true);
    beginResetModel();
    m_buckets = {};
    endResetModel();
    m_results->resetPresentation();
}
void SearchModel::search(const QString &keyword, Kind kind)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (m_closing || !m_pageActive || domainKind(kind).isEmpty())
        return;
    if (m_changingRows) {
        QMetaObject::invokeMethod(
            this, [this, keyword, kind] { search(keyword, kind); }, Qt::QueuedConnection);
        return;
    }
    const auto query = keyword.trimmed();
    if (query.isEmpty()) {
        showDiscovery();
        return;
    }
    clearBuckets();
    m_keyword = query;
    m_kind = kind;
    if (m_historyStore)
        m_historyStore->recordSearch(query);
    requestPage(1);
}
void SearchModel::setKind(Kind kind)
{
    if (kind == m_kind || m_closing || !m_pageActive || domainKind(kind).isEmpty())
        return;
    if (m_changingRows) {
        QMetaObject::invokeMethod(this, [this, kind] { setKind(kind); }, Qt::QueuedConnection);
        return;
    }
    // One query owns six buckets, including in-flight requests. A late result
    // fills its original bucket without resetting or repainting the active tab.
    {
        QScopedValueRollback<bool> changing(m_changingRows, true);
        beginResetModel();
        m_kind = kind;
        endResetModel();
    }
    if (!m_keyword.isEmpty() && !active().loaded && !active().request)
        requestPage(1);
    else
        emit changed();
}
void SearchModel::requestPage(quint32 page)
{
    auto &bucket = active();
    if (bucket.request)
        return;
    bucket.revision = m_api.sessionRevision();
    bucket.pendingPage = page;
    bucket.error.clear();
    bucket.request = m_api.search(apiKind(m_kind), m_keyword, page, PageSize,
                                  {AuthenticationMode::CurrentAccount, false});
    bucket.state =
        bucket.request ? (page == 1 ? State::Loading : State::LoadingMore) : State::Error;
    if (!bucket.request)
        bucket.error = tr("无法开始搜索，请重试");
    emit changed();
}
void SearchModel::loadMore()
{
    if (!m_closing && m_pageActive && !active().request && hasMore() && active().error.isEmpty())
        requestPage(active().nextPage);
}
void SearchModel::retry()
{
    if (!m_closing && m_pageActive && !active().request && !m_keyword.isEmpty())
        requestPage(active().nextPage ? active().nextPage : 1);
}
void SearchModel::showDiscovery()
{
    if (m_closing)
        return;
    if (m_changingRows) {
        QMetaObject::invokeMethod(this, &SearchModel::showDiscovery, Qt::QueuedConnection);
        return;
    }
    clearBuckets();
    m_keyword.clear();
    emit changed();
}
void SearchModel::reloadDiscovery()
{
    if (m_closing || (!m_pageActive && !m_discoveryActive) || m_hotRequest)
        return;
    m_hotError.clear();
    m_hotRevision = m_api.sessionRevision();
    m_hotRequest = m_api.searchHot({AuthenticationMode::DeviceOnly, false});
    if (!m_hotRequest)
        m_hotError = tr("热门搜索加载失败");
    emit discoveryChanged();
}
void SearchModel::succeeded(quint64 id, const QByteArray &payload)
{
    if (m_closing)
        return;
    const bool hotResult =
        id == m_hotRequest && m_hotRequest && m_hotRevision == m_api.sessionRevision();
    auto found = std::find_if(m_buckets.begin(), m_buckets.end(), [id](const Bucket &bucket) {
        return bucket.request && bucket.request == id;
    });
    // The shared client also delivers other services' replies. Reject their
    // identities before parsing/copying a potentially large JSON document.
    if (!hotResult && (found == m_buckets.end() || found->revision != m_api.sessionRevision()))
        return;
    const auto document = QJsonDocument::fromJson(payload);
    if (hotResult) {
        if (!document.isArray()) {
            failed(id, 1, {});
            return;
        }
        QList<HotSearchModel::Group> groups;
        for (const auto &value : document.array()) {
            const auto group = value.toObject();
            HotSearchModel::Group row;
            row.title = group.value("title").toString();
            for (const auto &item : group.value("keywords").toArray()) {
                row.keywords.append(item.toObject().value("keyword").toString());
                row.reasons.append(item.toObject().value("reason").toString());
            }
            groups.append(std::move(row));
        }
        m_hotRequest = 0;
        m_hotError.clear();
        m_hot.replace(std::move(groups));
        emit discoveryChanged();
        return;
    }
    auto &bucket = *found;
    const auto requestKind = Kind(std::distance(m_buckets.begin(), found));
    const auto object = document.object();
    const auto page = object.value("page").toObject();
    const auto items = page.value("items").toArray();
    const auto total = qMax(0.0, page.value("total").toDouble());
    // The tag is produced by our Rust enum, not a provider field. A mismatched
    // typed result cannot be interpreted as another category's records.
    if (object.value("kind").toString() != domainKind(requestKind) ||
        !page.value("items").isArray()) {
        failed(id, 1, {});
        return;
    }
    // Request page is owned by the pending request. Provider count/echo
    // variations must not become a second schema gate after Rust mapping.
    QList<Row> rows;
    for (const auto &value : items) {
        const auto item = value.toObject();
        Row row;
        switch (requestKind) {
        case Kind::Song:
        case Kind::Lyric: {
            auto song = requestKind == Kind::Lyric ? item.value("song").toObject() : item;
            // Lyric hits keep the song summary separate from catalog facts.
            if (requestKind == Kind::Lyric && item.contains("metadata"))
                song.insert("metadata", item.value("metadata"));
            if (m_metadata)
                song = m_metadata->complete(song);
            row = {song.value("title").toString(),
                   artistNames(song),
                   song.value("album_title").toString(),
                   song.value("cover_url").toString(),
                   durationText(song),
                   item.value("excerpt").toString(),
                   song.value("id").toString(),
                   quint64(song.value("duration_ms").toDouble())};
            row.track = PlaybackTrack::fromDomain(song);
            row.original = song.value("original").toBool();
            row.badges = songPrivilegeTags(row.track);
            row.quality = songQualityLabel(row.track);
            break;
        }
        case Kind::Album:
            row = {item.value("title").toString(),
                   artistNames(item),
                   item.value("published_at").toString(),
                   item.value("cover_url").toString(),
                   {},
                   QStringList{artistNames(item), item.value("published_at").toString()}
                       .filter(QRegularExpression(".+"))
                       .join(" • "),
                   identifier(item.value("id"))};
            break;
        case Kind::Artist: {
            QStringList counts;
            const auto songs = quint64(item.value("song_count").toDouble());
            const auto albums = quint64(item.value("album_count").toDouble());
            const auto fans = quint64(item.value("fans").toDouble());
            if (songs)
                counts.append(tr("%1 歌曲").arg(songs));
            if (albums)
                counts.append(tr("%1 专辑").arg(albums));
            if (counts.isEmpty() && fans)
                counts.append(fans >= 10000 ? tr("%1万 粉丝").arg(double(fans) / 10000, 0, 'f', 1)
                                            : tr("%1 粉丝").arg(fans));
            row = {item.value("name").toString(),       {}, {},
                   item.value("avatar_url").toString(), {}, counts.join(" • "),
                   identifier(item.value("id"))};
            break;
        }
        case Kind::Playlist: {
            QStringList metadata;
            const auto creator = item.value("creator_name").toString();
            const auto count = item.value("song_count").toInt();
            if (!creator.isEmpty())
                metadata.append(creator);
            if (count)
                metadata.append(tr("%1 首歌曲").arg(count));
            row = {item.value("title").toString(),     creator, {},
                   item.value("cover_url").toString(), {},      metadata.join(" • "),
                   item.value("global_id").toString()};
            break;
        }
        case Kind::Mv:
            // The domain retains thumbnail_file as a filename. Do not guess a CDN.
            row = {item.value("title").toString(),
                   artistNames(item),
                   {},
                   {},
                   durationText(item),
                   {},
                   identifier(item.value("id")),
                   quint64(item.value("duration_ms").toDouble())};
            break;
        }
        rows.append(std::move(row));
    }
    const bool visibleBucket = requestKind == m_kind;
    bucket.request = 0;
    bucket.total = quint64(total);
    bucket.error.clear();
    bucket.state = State::Ready;
    bucket.loaded = true;
    bucket.nextPage = rows.isEmpty() ? 0 : bucket.pendingPage + 1;
    QPointer<SearchModel> guard(this);
    if (!rows.isEmpty()) {
        QScopedValueRollback<bool> changing(m_changingRows, true);
        if (visibleBucket)
            beginInsertRows({}, int(bucket.rows.size()), int(bucket.rows.size() + rows.size() - 1));
        bucket.rows.append(std::move(rows));
        if (visibleBucket)
            endInsertRows();
    }
    if (guard && visibleBucket)
        emit changed();
}
void SearchModel::failed(quint64 id, int, const QByteArray &)
{
    if (m_closing)
        return;
    if (id == m_hotRequest && m_hotRequest && m_hotRevision == m_api.sessionRevision()) {
        m_hotRequest = 0;
        m_hotError = tr("热门搜索加载失败，请重试");
        emit discoveryChanged();
        return;
    }
    for (auto &bucket : m_buckets) {
        if (!bucket.request || bucket.request != id || bucket.revision != m_api.sessionRevision())
            continue;
        bucket.request = 0;
        bucket.state = State::Error;
        bucket.error = bucket.loaded ? tr("加载更多失败，请重试") : tr("搜索请求失败，请重试");
        if (&bucket == &active())
            emit changed();
        return;
    }
}
void SearchModel::playSong(const QString &id)
{
    requestPlayback(id, true);
}
void SearchModel::playNow(const QString &id)
{
    const auto track = rowTrack(id);
    if (track.playable())
        emit trackPlayNowRequested(track, tr("搜索：%1").arg(m_keyword), "search/" + m_keyword);
}
void SearchModel::requestPlayback(const QString &id, bool selectedSong)
{
    if (m_kind != Kind::Song && m_kind != Kind::Lyric)
        return;
    PlaybackTracks tracks;
    int selected = -1;
    for (int row = 0; row < m_results->rowCount(); ++row) {
        const int source = m_results->mapToSource(m_results->index(row, 0)).row();
        const auto &item = active().rows[source];
        if (item.id == id)
            selected = int(tracks.size());
        tracks.append(item.track);
    }
    if (!selectedSong && !tracks.isEmpty())
        selected = m_playback && m_playback->playMode() == PlaybackService::PlayMode::Shuffle
                       ? QRandomGenerator::global()->bounded(int(tracks.size()))
                       : 0;
    if (selectedSong && selected >= 0 && m_playback && !id.isEmpty() &&
        id == m_playback->trackId()) {
        m_playback->toggle();
        return;
    }
    if (selected >= 0)
        emit tracksPlayRequested(tracks, selected, tr("搜索：%1").arg(m_keyword),
                                 "search/" + m_keyword, selectedSong);
}
void SearchModel::playAll()
{
    if (m_kind != Kind::Song && m_kind != Kind::Lyric)
        return;
    if (m_results->rowCount())
        requestPlayback({}, false);
}
void SearchModel::appendAll(bool next)
{
    if (m_kind != Kind::Song && m_kind != Kind::Lyric)
        return;
    PlaybackTracks tracks;
    for (int row = 0; row < m_results->rowCount(); ++row)
        tracks.append(active().rows[m_results->mapToSource(m_results->index(row, 0)).row()].track);
    emit tracksAppendRequested(tracks, next);
}
PlaybackTrack SearchModel::rowTrack(const QString &id) const
{
    for (const auto &row : active().rows)
        if (row.id == id)
            return row.track;
    return {};
}
void SearchModel::appendSong(const QString &id, bool next)
{
    const auto track = rowTrack(id);
    if (track.playable())
        emit trackQueueRequested(track, next);
}
void SearchModel::addSong(const QString &id)
{
    const auto track = rowTrack(id);
    auto *actions = m_playback ? m_playback->libraryActions() : nullptr;
    if (actions && track.playable())
        actions->prepareTracks({track});
}
bool SearchModel::isFavorite(const QString &id) const
{
    auto *actions = m_playback ? m_playback->libraryActions() : nullptr;
    return actions && actions->favoriteTrack(rowTrack(id));
}
void SearchModel::toggleFavorite(const QString &id)
{
    const auto track = rowTrack(id);
    auto *actions = m_playback ? m_playback->libraryActions() : nullptr;
    if (actions && track.playable())
        actions->toggleFavoriteTrack(track);
}
void SearchModel::shareSong(const QString &id)
{
    if (!m_playback)
        return;
    const auto track = rowTrack(id);
    if (!track.id.isEmpty() || !track.hash.isEmpty())
        m_playback->songCommands()->share(track);
}
void SearchModel::copySong(const QString &id)
{
    if (m_playback)
        m_playback->songCommands()->copyInfo(rowTrack(id));
}
void SearchModel::openDetails(const QString &id)
{
    if (m_playback)
        m_playback->songCommands()->openDetails(rowTrack(id));
}
void SearchModel::openArtist(const QString &id)
{
    if (m_playback)
        m_playback->songCommands()->openArtist(id);
}
void SearchModel::openAlbum(const QString &id)
{
    if (m_playback)
        m_playback->songCommands()->openAlbum(id);
}
void SearchModel::openMv(const QString &id)
{
    if (m_playback)
        m_playback->songCommands()->openMv(rowTrack(id));
}
bool SearchModel::blacklistAvailable(const QString &id) const
{
    const auto track = rowTrack(id);
    return m_playback && m_playback->songCommands()->blacklistAvailable(track);
}
bool SearchModel::blacklisted(const QString &id) const
{
    const auto track = rowTrack(id);
    return m_playback && m_playback->songCommands()->blacklisted(track);
}
void SearchModel::addToBlacklist(const QString &id)
{
    if (!m_playback)
        return;
    m_playback->songCommands()->addToBlacklist(rowTrack(id));
}
void SearchModel::removeFromBlacklist(const QString &id)
{
    if (!m_playback)
        return;
    m_playback->songCommands()->removeFromBlacklist(rowTrack(id));
}
bool SearchModel::blacklistBusy() const
{
    return m_playback && m_playback->songCommands()->busy();
}
quint64 SearchModel::blacklistRevision() const
{
    return m_playback ? m_playback->songCommands()->revision() : 0;
}
} // namespace EchoMusic
