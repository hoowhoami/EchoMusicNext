#include "SongCatalogModel.h"
#include "SongMetadataService.h"
#include "SongPresentation.h"
#include <QJsonArray>
#include <limits>

namespace EchoMusic
{
void SongCatalogModel::enableSessionStorage()
{
    if (m_metadata)
        if (auto *database = m_metadata->sessionDatabase())
            enableSessionStorage(*database);
}
void SongCatalogModel::enableSessionStorage(SessionDatabase &database)
{
    Q_ASSERT(m_entries.isEmpty());
    if (m_store || !m_entries.isEmpty())
        return;
    m_sessionDatabase = &database;
    m_store = std::make_unique<SongDatasetStore>(database);
    resetStorage();
    connect(&database, &SessionDatabase::aboutToInvalidate, this, [this] {
        if (m_cards)
            return;
        m_cacheRecovering = !m_entries.isEmpty() || !m_query.isEmpty() || m_sortField;
        m_recoverySelection = m_selected;
        emit cacheRecoveryChanged(); // Freeze the viewport before any row notification.
        resetStorage();
        m_cacheStale = true;
        if (m_metadata)
            m_metadata->unwatch(this);
        m_metadataObservedCount = 0;
    });
    connect(&database, &SessionDatabase::invalidated, this, [this] {
        if (!m_cards)
            emit cacheInvalidated();
    });
    connect(&database, &SessionDatabase::capacityAvailable, this, [this] {
        flushPresentations();
        flushStorage();
        flushQuery();
        flushViewReads();
        flushMetadata();
    });
}
void SongCatalogModel::replaceCachedRows(const QList<SongDatasetStore::CachedRow> &rows,
                                         bool preserveSelection)
{
    Q_ASSERT(m_store);
    if (!m_store)
        return;
    QList<Entry> entries;
    entries.reserve(rows.size());
    QSet<QString> ids;
    for (const auto &row : rows) {
        const auto &id = row.identity;
        Entry entry;
        entry.id = row.reference.id;
        if (entry.id.isEmpty() || ids.contains(entry.id))
            entry.id = QStringLiteral("catalog-row:%1:%2").arg(entries.size()).arg(entry.id);
        ids.insert(entry.id);
        entry.domainId = id.id;
        entry.hash = id.hash;
        entry.path = id.path;
        entry.completionId = id.completionId;
        entry.songId = id.songId;
        entry.playable = id.playable;
        entry.trackPlayable = id.trackPlayable;
        entry.incomplete = id.incomplete;
        entry.record = row.reference.record;
        entry.source = row.reference.source ? row.reference.source : row.generation;
        if (const auto found = m_indices.constFind(entry.id); found != m_indices.cend()) {
            const auto &old = m_entries[*found];
            if (old.record == entry.record && old.source == entry.source) {
                entry.presentationRevision = old.presentationRevision;
                entry.incomplete = old.incomplete;
                entry.view = old.view;
                if (!entry.view)
                    if (const auto cached = m_hotViews.lookup(*found))
                        entry.view = *cached;
            }
        }
        entries.append(std::move(entry));
    }
    const auto previousSelection = m_selected;
    if (m_cacheRecovering)
        for (const auto &entry : entries)
            if (m_recoverySelection.contains(entry.id))
                m_selected.insert(entry.id);
    bool prefix = m_sharedSource && entries.size() >= m_entries.size();
    for (int i = 0; prefix && i < m_entries.size(); ++i)
        prefix = entries[i].id == m_entries[i].id && entries[i].record == m_entries[i].record &&
                 entries[i].source == m_entries[i].source;
    if (prefix) {
        const auto oldSelection = previousSelection;
        if (!preserveSelection)
            m_selected.clear();
        const int first = m_entries.size();
        const bool ordered = m_query.trimmed().isEmpty() && !m_sortField;
        if (ordered && entries.size() > first)
            beginInsertRows({}, first, entries.size() - 1);
        for (int i = first; i < entries.size(); ++i) {
            m_indices.insert(entries[i].id, i);
            m_domainIndices.insert(entries[i].domainId, i);
            m_entries.append(entries[i]);
            m_viewRows.append(ordered ? i : -1);
            if (ordered)
                m_order.append(i);
        }
        m_writeCursor = m_entries.size();
        if (ordered && entries.size() > first)
            endInsertRows();
        if (m_metadata && entries.size() > first)
            m_metadataRefresh.start();
        if (!ordered && entries.size() > first)
            rebuild();
        if (oldSelection != m_selected) {
            if (count())
                emit dataChanged(index(0), index(count() - 1), {Selected});
            emit selectionChanged();
        }
        emit changed();
        emit presentationProgress();
        return;
    }
    bool preserveCommands = m_sharedSource;
    QHash<QString, const Entry *> incoming;
    for (const auto &entry : entries)
        incoming.insert(entry.id, &entry);
    for (const auto &old : m_entries) {
        const auto next = incoming.value(old.id);
        if (!next || next->record != old.record || next->source != old.source) {
            preserveCommands = false;
            break;
        }
    }
    replaceEntries(std::move(entries), false, preserveSelection, true, preserveCommands);
    QSet<int> reused;
    for (int i = 0; i < m_entries.size(); ++i)
        if (m_entries[i].view) {
            m_viewDemand.insert(i, true, std::numeric_limits<qint64>::max());
            reused.insert(i);
        }
    retireViews(reused);
}
SongDatasetStore::Snapshot SongCatalogModel::snapshot(const QList<Entry> &entries) const
{
    SongDatasetStore::Snapshot result{m_generation, {}};
    result.rows.reserve(entries.size());
    for (qsizetype i = 0; i < entries.size(); ++i)
        result.rows.append({entries[i].record, i, entries[i].id, entries[i].source});
    return result;
}
void SongCatalogModel::resetStorage(bool preserveCommands)
{
    ++m_storageEpoch;
    if (!preserveCommands)
        ++m_commandEpoch;
    m_viewLoad.stop();
    m_metadataLoad.stop();
    m_hotViews.clear();
    m_viewDemand.clear();
    m_pendingMetadata.clear();
    m_pendingOverlays.clear();
    m_loadingViews = m_metadataLoading = m_metadataWatching = false;
    m_writing = m_storageFailed = false;
    m_writeCursor = 0;
    m_sharedSource = false;
    m_presentationFlush.stop();
    m_dirtyPresentations.clear();
    m_presentationWriting = m_presentationFailed = false;
    ++m_viewRevision;
    m_queryRefresh.stop();
    m_queryNeeded = m_querying = false;
    m_generation = m_store ? m_store->create() : SongDatasetStore::Generation{};
}
void SongCatalogModel::finishCacheRecovery()
{
    if (!m_cacheRecovering)
        return;
    m_cacheRecovering = false;
    m_recoverySelection.clear();
    emit cacheRecoveryChanged();
}
void SongCatalogModel::failStorage(const QString &message)
{
    finishCacheRecovery();
    m_storageFailed = m_presentationFailed = true;
    m_loadingViews = m_metadataLoading = m_metadataWatching = false;
    ++m_viewRevision;
    m_queryNeeded = m_querying = false;
    m_queryRefresh.stop();
    const QPointer<SongCatalogModel> guard(this);
    emit changed();
    if (guard)
        emit storageFailed(message);
    if (guard)
        emit readFailed(message);
}
bool SongCatalogModel::retryStorage()
{
    if (!m_store || !m_storageFailed)
        return false;
    m_storageFailed = m_presentationFailed = false;
    flushPresentations();
    flushStorage();
    m_metadataLoad.start();
    m_viewLoad.start();
    m_metadataRefresh.start();
    rebuild();
    return true;
}
QJsonObject SongCatalogModel::presentationValue(const Entry &entry)
{
    return {{"song", SongPresentation::projection(entry.view->domainSong)},
            {"badges", QJsonArray::fromVariantList(entry.view->badges)},
            {"quality", entry.view->quality},
            {"quality_tags", QJsonArray::fromVariantList(entry.view->qualityTags)},
            {"playable", entry.playable},
            {"track_playable", entry.trackPlayable},
            {"original", entry.view->original},
            {"extra", entry.view->extra}};
}
void SongCatalogModel::queuePresentations(const QSet<int> &sources)
{
    if (!m_store || m_cards)
        return;
    for (const auto source : sources)
        if (source >= 0 && source < m_entries.size() && m_entries[source].record)
            m_dirtyPresentations.insert(source);
    if (!m_dirtyPresentations.isEmpty() && !m_presentationFailed)
        m_presentationFlush.start();
}
void SongCatalogModel::flushPresentations()
{
    if (m_cacheStale || !m_store || m_cards || m_presentationWriting || m_presentationFailed ||
        m_dirtyPresentations.isEmpty())
        return;
    const auto epoch = m_storageEpoch;
    QList<SongDatasetStore::Presentation> values;
    QHash<int, ViewPtr> admitted;
    QSet<int> sources;
    SongDatasetStore::Generation generation;
    for (const auto source : m_dirtyPresentations) {
        const auto &entry = m_entries[source];
        const auto owner = entry.source ? entry.source : m_generation;
        if (generation && generation != owner)
            continue;
        generation = owner;
        admitted.insert(source, entry.view);
        values.append({entry.record, entry.view->title, entry.view->artist, entry.view->album,
                       entry.view->durationMs, presentationValue(entry)});
        sources.insert(source);
        if (values.size() == 128)
            break;
    }
    m_presentationWriting = true;
    if (m_store->updatePresentations(
            generation, std::move(values), this, [this, epoch, sources, admitted](auto result) {
                if (epoch != m_storageEpoch)
                    return;
                m_presentationWriting = false;
                if (!result.error.isEmpty()) {
                    for (const auto source : sources)
                        if (!m_entries[source].view)
                            m_entries[source].view = admitted.value(source);
                    m_dirtyPresentations.unite(sources);
                    failStorage(result.error);
                    return;
                }
                flushPresentations();
                flushMetadata();
                emit presentationProgress();
            })) {
        m_dirtyPresentations.subtract(sources);
        retireViews(sources);
    } else
        m_presentationWriting = false;
}
void SongCatalogModel::flushStorage()
{
    if (m_cacheStale || !m_store || m_cards || m_sharedSource || m_writing || m_storageFailed ||
        m_writeCursor >= m_entries.size())
        return;
    const auto epoch = m_storageEpoch;
    const auto first = m_writeCursor;
    const auto last = std::min(first + 128, m_entries.size());
    QList<SongDatasetStore::Row> rows;
    QList<QJsonObject> admittedPresentations;
    for (auto i = first; i < last; ++i) {
        const auto &entry = m_entries[i];
        const auto view = presentationValue(entry);
        admittedPresentations.append(view);
        rows.append({entry.id, entry.view->title, entry.view->artist, entry.view->album,
                     entry.view->durationMs, i, entry.view->domainSong, view});
    }
    m_writing = true;
    if (!m_store->append(m_generation, std::move(rows), this,
                         [this, epoch, first, last, admittedPresentations](auto result) {
                             if (epoch != m_storageEpoch)
                                 return;
                             m_writing = false;
                             if (!result.error.isEmpty()) {
                                 // Keep the original rows usable if the disposable cache fails.
                                 failStorage(result.error);
                                 return;
                             }
                             QSet<int> changed;
                             for (auto i = first; i < last; ++i) {
                                 auto &entry = m_entries[i];
                                 entry.record = result.rows[i - first].record;
                                 entry.view.detach();
                                 entry.view->domainSong =
                                     SongPresentation::projection(entry.view->domainSong);
                                 // Metadata can arrive while this source page waits for
                                 // SQL. Publish its latest view after the source commit.
                                 if (presentationValue(entry) != admittedPresentations[i - first])
                                     changed.insert(i);
                             }
                             m_writeCursor = last;
                             queuePresentations(changed);
                             QSet<int> stored;
                             for (auto i = first; i < last; ++i)
                                 stored.insert(int(i));
                             retireViews(stored);
                             notifyViewRows(stored);
                             flushStorage();
                             if (storageReady())
                                 emit storageReadyChanged();
                         }))
        m_writing = false;
}
void SongCatalogModel::requestTrack(const QString &id, QObject *consumer,
                                    std::function<void(PlaybackTrack)> done)
{
    const auto found = m_indices.constFind(id);
    if (found == m_indices.cend())
        return;
    readEntries({m_entries[*found]}, consumer, [done = std::move(done)](PlaybackTracks tracks) {
        if (!tracks.isEmpty())
            done(std::move(tracks.first()));
    });
}
void SongCatalogModel::requestTracks(bool selectedOnly, QObject *consumer,
                                     std::function<void(PlaybackTracks)> done)
{
    if (m_store && !selectedOnly && !m_cards) {
        requestQueryTracks(consumer, std::move(done));
        return;
    }
    QList<Entry> rows;
    if (selectedOnly) {
        for (const auto &entry : m_entries)
            if (entry.playable && entry.trackPlayable && m_selected.contains(entry.id))
                rows.append(entry);
    } else
        for (const auto source : m_order) {
            const auto &entry = m_entries[source];
            if (m_queryStyle != SongDatasetStore::QueryStyle::Catalog ||
                (entry.playable && entry.trackPlayable))
                rows.append(entry);
        }
    readEntries(std::move(rows), consumer, std::move(done));
}
void SongCatalogModel::requestRows(const QStringList &ids, QObject *consumer,
                                   std::function<void(PlaybackTracks)> done)
{
    QList<Entry> rows;
    for (const auto &id : ids)
        if (auto found = m_indices.constFind(id); found != m_indices.cend())
            rows.append(m_entries[*found]);
    readEntries(std::move(rows), consumer, std::move(done));
}
void SongCatalogModel::flushQuery()
{
    if (m_cacheStale || !m_store || !m_queryNeeded || m_querying || m_storageFailed ||
        m_presentationFailed || m_writing || m_writeCursor < m_entries.size() ||
        m_presentationWriting || !m_dirtyPresentations.isEmpty())
        return;
    if (m_metadataLoading || !m_pendingMetadata.isEmpty())
        return;
    const auto epoch = m_storageEpoch, revision = m_viewRevision;
    m_querying = true;
    m_queryNeeded = false;
    const auto complete = [this, epoch, revision](auto result) {
        if (epoch != m_storageEpoch)
            return;
        m_querying = false;
        if (revision != m_viewRevision) {
            flushQuery();
            return;
        }
        if (!result.error.isEmpty()) {
            emit changed();
            emit readFailed(result.error);
            return;
        }
        QList<int> order;
        for (const auto &row : result.rows)
            order.append(int(row.ordinal));
        applyOrder(std::move(order));
    };
    const bool accepted = m_sharedSource
                              ? m_store->query(snapshot(m_entries), m_query, m_sortField,
                                               m_sortDirection, this, complete, m_queryStyle)
                              : m_store->query(m_generation, m_query, m_sortField, m_sortDirection,
                                               this, complete, m_entries.size() - 1, m_queryStyle);
    if (!accepted) {
        m_querying = false;
        m_queryNeeded = true;
    }
}
void SongCatalogModel::requestQueryTracks(QObject *consumer,
                                          std::function<void(PlaybackTracks)> done)
{
    if (!consumer)
        return;
    // Freeze inputs at the command boundary, even if a different query still
    // occupies the view. New pages cannot enter this accepted membership range.
    struct Request
    {
        QList<Entry> entries;
        QString text;
        SongDatasetStore::Generation generation;
        bool shared = false;
        SongDatasetStore::QueryStyle style = SongDatasetStore::QueryStyle::Catalog;
        int field = 0, direction = 0;
        bool waiting = false;
        std::function<void(PlaybackTracks)> done;
    };
    auto state = std::make_shared<Request>();
    state->entries = m_entries;
    state->generation = m_generation;
    state->shared = m_sharedSource;
    state->style = m_queryStyle;
    state->text = m_query;
    state->field = m_sortField;
    state->direction = m_sortDirection;
    state->done = std::move(done);
    auto *request = new QObject(this);
    connect(consumer, &QObject::destroyed, request, &QObject::deleteLater);
    if (m_sessionDatabase)
        connect(m_sessionDatabase, &SessionDatabase::aboutToInvalidate, request,
                &QObject::deleteLater);
    const QPointer<QObject> lifetime(request), guard(consumer);
    const auto epoch = m_commandEpoch;
    auto step = std::make_shared<std::function<void()>>();
    std::weak_ptr<std::function<void()>> weak = step;
    *step = [this, state, epoch, lifetime, guard, weak] {
        if (!lifetime)
            return;
        if (!guard || epoch != m_commandEpoch) {
            lifetime->deleteLater();
            return;
        }
        if (state->waiting)
            return;
        if (m_storageFailed || m_presentationFailed) {
            state->waiting = true;
            lifetime->deleteLater();
            emit readFailed(tr("读取歌曲缓存失败，请重试加载列表"));
            return;
        }
        if (m_writeCursor < state->entries.size() || m_presentationWriting || m_metadataLoading ||
            !m_pendingMetadata.isEmpty() || !m_dirtyPresentations.isEmpty())
            return;
        state->waiting = true;
        const auto complete = [this, state, epoch, lifetime, guard,
                               step = weak.lock()](auto result) {
            if (!lifetime)
                return;
            lifetime->deleteLater();
            if (!guard || epoch != m_commandEpoch)
                return;
            if (!result.error.isEmpty()) {
                emit readFailed(result.error);
                return;
            }
            QList<Entry> rows;
            for (const auto &row : result.rows) {
                auto entry = state->entries[row.ordinal];
                if (state->style != SongDatasetStore::QueryStyle::Catalog ||
                    (entry.playable && entry.trackPlayable)) {
                    entry.record = row.record;
                    rows.append(std::move(entry));
                }
            }
            readEntries(std::move(rows), guard, std::move(state->done));
        };
        const bool accepted =
            state->shared
                ? m_store->query(
                      SongDatasetStore::Snapshot{state->generation, snapshot(state->entries).rows},
                      state->text, state->field, state->direction, lifetime, complete, state->style)
                : m_store->query(state->generation, state->text, state->field, state->direction,
                                 lifetime, complete, state->entries.size() - 1, state->style);
        if (!accepted)
            state->waiting = false;
    };
    connect(m_sessionDatabase, &SessionDatabase::capacityAvailable, request, [step] { (*step)(); });
    connect(this, &SongCatalogModel::presentationProgress, request, [step] { (*step)(); });
    QTimer::singleShot(0, request, [step] { (*step)(); });
}
void SongCatalogModel::readEntries(QList<Entry> rows, QObject *consumer,
                                   std::function<void(PlaybackTracks)> done)
{
    if (!consumer)
        return;
    if (!m_store) {
        PlaybackTracks tracks;
        for (const auto &entry : rows)
            tracks.append(PlaybackTrack::fromDomain(entry.view->domainSong));
        done(std::move(tracks));
        return;
    }
    // A command owns one immutable selection/order snapshot. Subsequent sort,
    // selection and append changes cannot change its meaning; replacement or
    // account retirement cancels delivery. Full tracks exist only for the action.
    struct Request
    {
        QList<Entry> rows;
        PlaybackTracks tracks;
        SongDatasetStore::Generation generation;
        qsizetype cursor = 0;
        bool waiting = false;
        std::function<void(PlaybackTracks)> done;
    };
    auto state = std::make_shared<Request>();
    state->rows = std::move(rows);
    state->generation = m_generation;
    state->done = std::move(done);
    auto *request = new QObject(this);
    connect(consumer, &QObject::destroyed, request, &QObject::deleteLater);
    if (m_sessionDatabase)
        connect(m_sessionDatabase, &SessionDatabase::aboutToInvalidate, request,
                &QObject::deleteLater);
    const QPointer<QObject> guard(consumer), lifetime(request);
    const auto epoch = m_commandEpoch;
    auto step = std::make_shared<std::function<void()>>();
    std::weak_ptr<std::function<void()>> weak = step;
    *step = [this, state, epoch, guard, lifetime, weak] {
        if (!lifetime)
            return;
        if (!guard || epoch != m_commandEpoch) {
            lifetime->deleteLater();
            return;
        }
        if (state->waiting)
            return;
        while (state->cursor < state->rows.size() && !state->rows[state->cursor].record) {
            state->tracks.append(
                PlaybackTrack::fromDomain(state->rows[state->cursor].view->domainSong));
            ++state->cursor;
        }
        if (state->cursor == state->rows.size()) {
            state->waiting = true;
            lifetime->deleteLater();
            state->done(std::move(state->tracks));
            return;
        }
        SongDatasetStore::Snapshot snapshot{state->generation, {}};
        const auto first = state->cursor;
        for (auto i = first; i < state->rows.size() && i < first + 128; ++i) {
            const auto &entry = state->rows[i];
            if (!entry.record)
                break;
            snapshot.rows.append({entry.record, i, entry.id, entry.source});
        }
        state->waiting = true;
        if (!m_store->read(
                std::move(snapshot), 0, lifetime,
                [this, state, epoch, first, guard, lifetime, step = weak.lock()](auto result) {
                    if (!lifetime)
                        return;
                    state->waiting = false;
                    if (!guard || epoch != m_commandEpoch) {
                        lifetime->deleteLater();
                        return;
                    }
                    if (!result.error.isEmpty()) {
                        state->waiting = true;
                        lifetime->deleteLater();
                        emit readFailed(result.error);
                        return;
                    }
                    for (qsizetype i = 0; i < result.values.size(); ++i) {
                        auto track = PlaybackTrack::fromDomain(result.values[i]);
                        const auto &entry = state->rows[first + i];
                        auto presentation = entry.view
                                                ? entry.view->domainSong
                                                : result.presentations[i].value("song").toObject();
                        if (m_metadata)
                            presentation = m_metadata->replay(presentation);
                        track.applyPresentation(presentation);
                        state->tracks.append(std::move(track));
                    }
                    state->cursor += result.next;
                    (*step)();
                },
                true))
            state->waiting = false;
    };
    connect(m_sessionDatabase, &SessionDatabase::capacityAvailable, request, [step] { (*step)(); });
    QTimer::singleShot(0, request, [step] { (*step)(); });
}
} // namespace EchoMusic
