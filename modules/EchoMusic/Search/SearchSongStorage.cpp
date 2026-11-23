#include "SearchModel.h"
#include "SearchSongStorage_p.h"
#include "SongMetadataService.h"
#include "SongPresentation.h"
#include <QScopedValueRollback>
namespace EchoMusic
{
bool SearchModel::cacheRecovering() const
{
    if (!m_songStorage)
        return false;
    const auto &source = m_songStorage->buckets[size_t(m_kind)];
    auto *model = source.model;
    return model && (model->cacheRecovering() || (source.recoveryPage && model->querying()));
}
int SearchModel::count() const
{
    if (auto *model = catalog())
        return model->totalCount();
    return int(active().rows.size());
}
SongCatalogModel *SearchModel::catalog() const
{
    return m_songStorage && active().cached ? m_songStorage->buckets[size_t(m_kind)].model
                                            : nullptr;
}
int SearchModel::catalogRole(int role)
{
    switch (role) {
    case Title:
        return SongCatalogModel::Title;
    case ArtistText:
        return SongCatalogModel::ArtistText;
    case AlbumText:
        return SongCatalogModel::AlbumText;
    case CoverUrl:
        return SongCatalogModel::CoverUrl;
    case DurationText:
        return SongCatalogModel::DurationText;
    case ExtraText:
        return SongCatalogModel::ExtraText;
    case ResourceId:
        return SongCatalogModel::ResourceId;
    case DurationMs:
        return SongCatalogModel::DurationMs;
    case Original:
        return SongCatalogModel::Original;
    case ArtistLinks:
        return SongCatalogModel::ArtistLinks;
    case AlbumId:
        return SongCatalogModel::AlbumId;
    case PrivilegeTags:
        return SongCatalogModel::PrivilegeTags;
    case QualityLabel:
        return SongCatalogModel::QualityLabel;
    case PresentationReady:
        return SongCatalogModel::PresentationReady;
    default:
        return -1;
    }
}
void SearchModel::initializeSongStorage()
{
    if (!m_metadata || !m_metadata->sessionDatabase())
        return;
    if (!m_songStorage)
        m_songStorage = std::make_unique<SearchSongStorage>();
    for (auto kind : {Kind::Song, Kind::Lyric}) {
        auto &source = m_songStorage->buckets[size_t(kind)];
        if (source.model)
            continue;
        source.cache = std::make_unique<SongSourceCache>(*m_metadata->sessionDatabase());
        source.model = new SongCatalogModel(this);
        source.model->setQueryStyle(kind == Kind::Song ? SongDatasetStore::QueryStyle::SearchSong
                                                       : SongDatasetStore::QueryStyle::SearchLyric);
        source.model->attachMetadata(m_metadata);
        source.model->enableSessionStorage(*m_metadata->sessionDatabase());
        source.model->setMetadataActive(m_metadataNetwork && m_pageActive);
        connect(source.model, &SongCatalogModel::cacheRecoveryChanged, this, &SearchModel::changed);
        connect(source.model, &SongCatalogModel::changed, this, [this, kind] {
            auto &source = m_songStorage->buckets[size_t(kind)];
            if (source.recoveryPage && !source.model->cacheRecovering() &&
                !source.model->querying()) {
                source.recoveryPage = 0;
                if (m_kind == kind)
                    emit changed();
            }
        });
        connect(source.model, &SongCatalogModel::cacheInvalidated, this, [this, kind] {
            auto &source = m_songStorage->buckets[size_t(kind)];
            auto &bucket = m_buckets[size_t(kind)];
            source.recoveryPage = qMax(source.page, quint32(1));
            if (bucket.request)
                m_api.cancel(bucket.request);
            delete source.write.data();
            source.write = nullptr;
            source.pending = {};
            source.presentations.clear();
            source.records.clear();
            bucket = {};
            source.model->replaceCachedRows({});
            if (!m_closing && m_pageActive && m_kind == kind && !m_keyword.isEmpty())
                requestPage(1);
        });
        connect(source.model, &SongCatalogModel::readFailed, this,
                [this, kind](const QString &error) {
                    auto &bucket = m_buckets[size_t(kind)];
                    bucket.error = error;
                    bucket.state = State::Error;
                    m_songStorage->buckets[size_t(kind)].model->finishCacheRecovery();
                    if (m_kind == kind)
                        emit changed();
                });
        connect(source.model, &QAbstractItemModel::dataChanged, this,
                [this, kind](const QModelIndex &first, const QModelIndex &last) {
                    if (m_kind != kind || !active().cached || m_changingRows)
                        return;
                    auto *model = catalog();
                    for (int i = first.row(); i <= last.row(); ++i) {
                        const auto id =
                            model->data(model->index(i), SongCatalogModel::ResourceId).toString();
                        const auto row = model->sourceIndexOf(id);
                        if (row >= 0)
                            emit dataChanged(index(row), index(row));
                    }
                });
    }
}
bool SearchModel::cacheSongPage(Kind kind, const QJsonArray &items, quint64 total)
{
    if (!m_songStorage || (kind != Kind::Song && kind != Kind::Lyric))
        return false;
    auto &source = m_songStorage->buckets[size_t(kind)];
    if (!source.cache)
        return false;
    auto &bucket = m_buckets[size_t(kind)];
    source.pending = {};
    source.presentations.clear();
    // Late attachment may encounter a previously dense first page. Transfer it
    // in the same transaction-bound ingestion path before accepting new hits.
    for (const auto &row : bucket.rows) {
        source.pending.append(row.track.toDomain());
        auto view = SongPresentation::value(row.track.toDomain());
        view.insert("extra", row.extra);
        view.insert("original", row.original);
        source.presentations.append(view);
    }
    for (const auto &value : items) {
        const auto item = value.toObject();
        auto song = kind == Kind::Lyric ? item.value("song").toObject() : item;
        if (kind == Kind::Lyric && item.contains("metadata"))
            song.insert("metadata", item.value("metadata"));
        auto view = SongPresentation::value(m_metadata ? m_metadata->complete(song) : song);
        view.insert("extra", item.value("excerpt").toString());
        view.insert("original", song.value("original").toBool());
        source.pending.append(song);
        source.presentations.append(view);
    }
    source.total = total;
    source.page = bucket.pendingPage;
    bucket.request = 0;
    source.received = items.size();
    flushSongPage(kind);
    return true;
}
void SearchModel::flushSongPage(Kind kind)
{
    auto &source = m_songStorage->buckets[size_t(kind)];
    auto &bucket = m_buckets[size_t(kind)];
    // Start the accepted bucket even if another tab is currently visible.
    auto *reader = new QObject(this);
    source.write = reader;
    bucket.caching = true;
    const auto revision = bucket.revision;
    const auto keyword = m_keyword;
    source.cache->cache(
        source.pending, reader,
        [this, kind, reader, revision, keyword, count = source.received](auto rows, auto error) {
            reader->deleteLater();
            auto &source = m_songStorage->buckets[size_t(kind)];
            auto &bucket = m_buckets[size_t(kind)];
            if (source.write != reader || revision != m_api.sessionRevision() ||
                keyword != m_keyword || m_closing)
                return;
            source.write = nullptr;
            bucket.caching = false;
            if (!error.isEmpty()) {
                bucket.error = error;
                bucket.state = State::Error;
                if (kind == m_kind)
                    emit changed();
                return;
            }
            const bool visible = kind == m_kind;
            QScopedValueRollback<bool> changing(m_changingRows, true);
            if (visible)
                beginResetModel();
            source.records.append(rows);
            source.model->replaceCachedRows(source.records, true);
            bucket.rows.clear();
            bucket.cached = true;
            bucket.loaded = true;
            bucket.error.clear();
            bucket.state = State::Ready;
            bucket.total = source.total;
            bucket.nextPage = count ? source.page + 1 : 0;
            source.pending = {};
            source.presentations.clear();
            if (visible)
                endResetModel();
            if (visible)
                emit changed();
            if (source.recoveryPage) {
                if (source.page >= source.recoveryPage || !bucket.nextPage ||
                    quint64(source.records.size()) >= bucket.total) {
                    source.model->finishCacheRecovery();
                    if (!source.model->querying())
                        source.recoveryPage = 0;
                } else if (visible && m_pageActive)
                    requestPage(bucket.nextPage);
            }
        },
        {}, source.presentations);
}
bool SearchModel::retrySongPage()
{
    if (!m_songStorage || m_closing || !m_pageActive)
        return false;
    auto &source = m_songStorage->buckets[size_t(m_kind)];
    if (!source.model)
        return false;
    if (source.model->retryStorage()) {
        active().error.clear();
        active().state = State::Ready;
        emit changed();
        return true;
    }
    if (source.pending.isEmpty() || active().caching)
        return active().caching;
    // Reuse the admitted network page; this retry performs only local SQL.
    active().error.clear();
    active().state = active().loaded ? State::LoadingMore : State::Loading;
    flushSongPage(m_kind);
    emit changed();
    return true;
}
void SearchModel::clearSongStorage()
{
    if (!m_songStorage)
        return;
    if (m_songStorage->playRead)
        delete m_songStorage->playRead;
    m_songStorage->menus.clear();
    for (auto &source : m_songStorage->buckets) {
        if (source.write)
            delete source.write;
        source.records.clear();
        source.pending = {};
        source.presentations.clear();
        if (source.model) {
            source.model->finishCacheRecovery();
            source.model->clear();
        }
    }
}
void SearchModel::requestRow(const QString &id, std::function<void(PlaybackTrack)> done)
{
    if (auto *model = catalog()) {
        const auto keyword = m_keyword;
        const auto kind = m_kind;
        const auto revision = m_api.sessionRevision();
        model->requestTrack(id, this,
                            [this, keyword, kind, revision, done = std::move(done)](auto track) {
                                if (!m_closing && m_pageActive && keyword == m_keyword &&
                                    kind == m_kind && revision == m_api.sessionRevision())
                                    done(std::move(track));
                            });
    } else
        done(rowTrack(id));
}
void SearchModel::requestResultTracks(QObject *consumer, std::function<void(PlaybackTracks)> done)
{
    if (auto *model = catalog()) {
        const auto keyword = m_keyword;
        const auto kind = m_kind;
        const auto revision = m_api.sessionRevision();
        model->requestTracks(false, consumer,
                             [this, keyword, kind, revision, done = std::move(done)](auto tracks) {
                                 if (!m_closing && m_pageActive && keyword == m_keyword &&
                                     kind == m_kind && revision == m_api.sessionRevision())
                                     done(std::move(tracks));
                             });
        return;
    }
    PlaybackTracks tracks;
    for (int row = 0; row < m_results->rowCount(); ++row)
        tracks.append(active().rows[m_results->mapToSource(m_results->index(row, 0)).row()].track);
    done(std::move(tracks));
}
QString SearchModel::requestPluginMenuContext(const QString &id, QObject *consumer)
{
    if (!consumer)
        return {};
    initializeSongStorage();
    if (auto *model = catalog(); model && m_playback) {
        auto &source = m_songStorage->buckets[size_t(m_kind)];
        if (!source.actions) {
            source.actions = new SongCatalogActions(*model, *m_playback, this);
            source.actions->setQueueContext(tr("搜索：%1").arg(m_keyword), "search");
            const auto kind = m_kind;
            connect(source.actions, &SongCatalogActions::pluginMenuContextReady, this,
                    [this, kind](const QString &ticket, QVariantMap context) {
                        if (m_kind != kind || !catalog() || m_closing)
                            return;
                        context.insert("revision", QString::number(active().revision));
                        emit pluginMenuContextReady(
                            QString("search:%1:%2").arg(int(kind)).arg(ticket), context);
                    });
        }
        const auto ticket = source.actions->requestPluginMenuContext(id, consumer);
        return ticket.isEmpty() ? QString() : QString("search:%1:%2").arg(int(m_kind)).arg(ticket);
    }
    if (!m_songStorage)
        m_songStorage = std::make_unique<SearchSongStorage>();
    const auto ticket = "search-dense:" + QString::number(++m_songStorage->ticket);
    const auto context = pluginMenuContext(id);
    m_songStorage->menus.insert(ticket, {consumer, context});
    connect(consumer, &QObject::destroyed, this,
            [this, ticket] { m_songStorage->menus.remove(ticket); });
    const QPointer<SearchModel> guard(this);
    QTimer::singleShot(0, consumer, [guard, ticket, context] {
        if (guard)
            emit guard->pluginMenuContextReady(ticket, context);
    });
    return ticket;
}
bool SearchModel::pluginMenuContextCurrent(const QString &ticket) const
{
    if (!m_songStorage)
        return false;
    if (const auto found = m_songStorage->menus.constFind(ticket);
        found != m_songStorage->menus.cend())
        return found->consumer &&
               found->context ==
                   pluginMenuContext(found->context.value("track").toMap().value("id").toString());
    const auto &source = m_songStorage->buckets[size_t(m_kind)];
    return catalog() && source.actions &&
           ticket.startsWith(QString("search:%1:").arg(int(m_kind))) &&
           source.actions->pluginMenuContextCurrent(ticket.section(':', 2));
}
} // namespace EchoMusic
