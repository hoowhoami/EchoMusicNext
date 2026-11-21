#include "SongCatalogModel.h"
#include "SongMetadataService.h"
#include <algorithm>
#include <limits>
#include <QUrl>

namespace EchoMusic
{
namespace
{
constexpr auto cacheLifetime = std::numeric_limits<qint64>::max();
constexpr quint64 viewBudget = 4 * 1024 * 1024;
} // namespace
SongCatalogModel::ViewPtr SongCatalogModel::viewFor(int source) const
{
    const auto &entry = m_entries[source];
    if (entry.view) {
        m_viewDemand.insert(source, true, cacheLifetime);
        return entry.view;
    }
    if (auto cached = m_hotViews.lookup(source))
        return *cached;
    auto *model = const_cast<SongCatalogModel *>(this);
    // Demand is bounded and coalesced. A fast jump retires old, unadmitted
    // windows rather than queuing a read for every row passed along the way.
    const int visible = source < m_viewRows.size() ? m_viewRows[source] : -1;
    if (visible >= 0)
        for (int row = std::max(0, visible - 24); row < std::min(count(), visible + 49); ++row)
            m_viewDemand.insert(m_order[row], true, cacheLifetime);
    else
        m_viewDemand.insert(source, true, cacheLifetime);
    model->m_viewLoad.start();
    return {};
}
SongCatalogModel::Entry SongCatalogModel::storedEntry(const QJsonObject &value, const Entry &base)
{
    auto result = base;
    result.view = songEntry(value.value("song").toObject()).view;
    result.view->badges = value.value("badges").toArray().toVariantList();
    result.view->quality = value.value("quality").toString();
    result.view->qualityTags = value.value("quality_tags").toArray().toVariantList();
    result.view->original = value.value("original").toBool();
    result.view->extra = value.value("extra").toString();
    return result;
}
void SongCatalogModel::retireViews(const QSet<int> &sources)
{
    if (!m_store || m_cards)
        return;
    for (const auto source : sources) {
        auto &entry = m_entries[source];
        if (!entry.record || !entry.view || m_dirtyPresentations.contains(source))
            continue;
        if (m_viewDemand.lookup(source) || m_hotViews.lookup(source)) {
            auto view = entry.view;
            view.detach();
            // Public plugin data is read for the menu/action, never retained
            // as part of a scrolling row's presentation window.
            const auto original = view->domainSong;
            view->domainSong = SongMetadataService::observationInput(original);
            if (original.contains("privilege"))
                view->domainSong.insert("privilege", original.value("privilege"));
            auto metadata = view->domainSong.value("metadata").toObject();
            const auto historyKey = original.value("metadata").toObject().value("history_key");
            if (!historyKey.isUndefined())
                metadata.insert("history_key", historyKey);
            view->domainSong.insert("metadata", metadata);
            const auto cost = SongDatasetStore::estimateCost(view->domainSong) + 1024;
            // One oversized presentation can still make progress. It occupies
            // the entire declared window budget instead of causing read loops.
            m_hotViews.insert(source, std::move(view), cacheLifetime, std::min(cost, viewBudget));
        }
        m_viewDemand.remove(source);
        entry.view.reset();
    }
}
void SongCatalogModel::loadViews(QList<int> sources, QObject *consumer,
                                 std::function<void(QList<Entry>)> done)
{
    struct Read
    {
        QList<Entry> rows;
        qsizetype cursor = 0;
        bool waiting = false;
        std::function<void(QList<Entry>)> done;
    };
    auto state = std::make_shared<Read>();
    for (const auto source : sources) {
        auto entry = m_entries[source];
        if (!entry.view)
            if (auto cached = m_hotViews.lookup(source))
                entry.view = *cached;
        state->rows.append(std::move(entry));
    }
    state->done = std::move(done);
    if (!m_store) {
        state->done(std::move(state->rows));
        return;
    }
    auto *request = new QObject(this);
    connect(consumer, &QObject::destroyed, request, &QObject::deleteLater);
    const QPointer<QObject> guard(consumer), lifetime(request);
    const auto epoch = m_storageEpoch;
    auto step = std::make_shared<std::function<void()>>();
    std::weak_ptr<std::function<void()>> weak = step;
    *step = [this, state, guard, lifetime, epoch, weak] {
        if (!lifetime)
            return;
        if (!guard || epoch != m_storageEpoch) {
            lifetime->deleteLater();
            return;
        }
        if (state->waiting)
            return;
        while (state->cursor < state->rows.size() && state->rows[state->cursor].view)
            ++state->cursor;
        if (state->cursor == state->rows.size()) {
            state->waiting = true;
            lifetime->deleteLater();
            state->done(std::move(state->rows));
            return;
        }
        const auto first = state->cursor;
        SongDatasetStore::Snapshot snapshot{m_generation, {}};
        for (auto i = first; i < state->rows.size() && i < first + 128; ++i) {
            const auto &entry = state->rows[i];
            if (entry.view)
                break;
            snapshot.rows.append({entry.record, i, entry.id, entry.source});
        }
        state->waiting = true;
        if (!m_store->readPresentations(
                std::move(snapshot), 0, lifetime,
                [this, state, first, guard, lifetime, epoch, step = weak.lock()](auto result) {
                    if (!lifetime)
                        return;
                    if (!guard || epoch != m_storageEpoch) {
                        lifetime->deleteLater();
                        return;
                    }
                    if (!result.error.isEmpty()) {
                        lifetime->deleteLater();
                        failStorage(result.error);
                        return;
                    }
                    for (qsizetype i = 0; i < result.values.size(); ++i)
                        state->rows[first + i] =
                            storedEntry(result.values[i], state->rows[first + i]);
                    state->cursor += result.next;
                    state->waiting = false;
                    (*step)();
                }))
            state->waiting = false;
    };
    connect(m_sessionDatabase, &SessionDatabase::capacityAvailable, request, [step] { (*step)(); });
    QTimer::singleShot(0, request, [step] { (*step)(); });
}
void SongCatalogModel::requestPresentations(const QStringList &ids, QObject *consumer,
                                            std::function<void(QJsonArray)> done)
{
    QList<int> sources;
    for (const auto &id : ids)
        if (const auto found = m_indices.constFind(id); found != m_indices.cend())
            sources.append(*found);
    loadViews(std::move(sources), consumer, [done = std::move(done)](auto rows) {
        QJsonArray values;
        for (const auto &row : rows)
            values.append(row.view->domainSong);
        done(std::move(values));
    });
}
void SongCatalogModel::requestCover(QObject *consumer, std::function<void(QString)> done)
{
    struct Scan
    {
        qsizetype cursor = 0;
        std::function<void(QString)> done;
    };
    auto state = std::make_shared<Scan>();
    state->done = std::move(done);
    auto *request = new QObject(this);
    connect(consumer, &QObject::destroyed, request, &QObject::deleteLater);
    const QPointer<QObject> guard(consumer), lifetime(request);
    const auto epoch = m_storageEpoch;
    auto step = std::make_shared<std::function<void()>>();
    std::weak_ptr<std::function<void()>> weak = step;
    *step = [this, state, guard, lifetime, epoch, weak] {
        if (!lifetime)
            return;
        if (!guard || epoch != m_storageEpoch) {
            lifetime->deleteLater();
            return;
        }
        if (state->cursor == m_entries.size()) {
            lifetime->deleteLater();
            state->done({});
            return;
        }
        QList<int> sources;
        for (auto i = state->cursor; i < m_entries.size() && i < state->cursor + 128; ++i)
            sources.append(int(i));
        loadViews(sources, lifetime, [state, guard, lifetime, step = weak.lock()](auto rows) {
            if (!guard || !lifetime)
                return;
            for (const auto &row : rows) {
                const auto &cover = row.view->cover;
                const QUrl url(cover);
                if (url.isValid() && (url.scheme() == "https" || url.scheme() == "http")) {
                    lifetime->deleteLater();
                    state->done(cover);
                    return;
                }
            }
            state->cursor += rows.size();
            (*step)();
        });
    };
    (*step)();
}
void SongCatalogModel::flushViewReads()
{
    if (!m_store || m_loadingViews || m_storageFailed)
        return;
    QList<int> sources;
    for (const auto source : m_viewDemand.keys()) {
        if (m_entries[source].view || m_hotViews.lookup(source))
            m_viewDemand.remove(source);
        else {
            sources.append(source);
            if (sources.size() == 128)
                break;
        }
    }
    if (sources.isEmpty())
        return;
    const auto epoch = m_storageEpoch;
    m_loadingViews = true;
    loadViews(sources, this, [this, sources, epoch](auto entries) {
        if (epoch != m_storageEpoch)
            return;
        m_loadingViews = false;
        QSet<int> ready;
        for (int i = 0; i < sources.size(); ++i) {
            const auto source = sources[i];
            if (!m_viewDemand.lookup(source))
                continue;
            if (!m_entries[source].view)
                m_entries[source].view = entries[i].view;
            ready.insert(source);
        }
        retireViews(ready);
        notifyViewRows(ready);
        flushViewReads();
    });
}
void SongCatalogModel::flushMetadata()
{
    if (!m_store || m_metadataLoading || m_pendingMetadata.isEmpty() || m_storageFailed ||
        m_presentationWriting || !m_dirtyPresentations.isEmpty())
        return;
    QList<int> sources;
    for (const auto source : m_pendingMetadata) {
        sources.append(source);
        if (sources.size() == 128)
            break;
    }
    const auto epoch = m_storageEpoch;
    m_metadataLoading = true;
    loadViews(sources, this, [this, sources, epoch](auto entries) {
        if (epoch != m_storageEpoch)
            return;
        m_metadataLoading = false;
        QSet<int> changed, loaded;
        bool orderMayChange = false;
        for (int i = 0; i < sources.size(); ++i) {
            const auto source = sources[i];
            m_pendingMetadata.remove(source);
            const auto overlays = m_pendingOverlays.take(source);
            auto &entry = m_entries[source];
            if (!entry.view)
                entry.view = entries[i].view;
            loaded.insert(source);
            auto song =
                m_metadata ? m_metadata->replay(entry.view->domainSong) : entry.view->domainSong;
            for (const auto &overlay : overlays)
                song = SongMetadataService::completeSong(song, overlay.toObject());
            if (song == entry.view->domainSong)
                continue;
            const auto before = entry.view;
            const auto id = entry.id;
            applyEntryPresentation(entry, song);
            entry.id = id;
            orderMayChange |=
                before->title != entry.view->title || before->artist != entry.view->artist ||
                before->album != entry.view->album || before->durationMs != entry.view->durationMs;
            changed.insert(source);
        }
        notifyPresentations(changed, orderMayChange);
        retireViews(loaded);
        flushMetadata();
        flushQuery();
        emit presentationProgress();
    });
}
void SongCatalogModel::notifyViewRows(const QSet<int> &sources)
{
    for (const auto source : sources)
        if (source < m_viewRows.size() && m_viewRows[source] >= 0)
            emit dataChanged(index(m_viewRows[source]), index(m_viewRows[source]));
    if (!sources.isEmpty())
        emit changed();
}
} // namespace EchoMusic
