#include <QQmlEngine>
#include "SongCatalogModel.h"
#include "SongMetadataService.h"
#include "SongRowFacts.h"
#include <QJsonObject>
#include <QRegularExpression>
#include <algorithm>
#include <numeric>
namespace EchoMusic
{
namespace
{
QString baseSortText(const QString &text)
{
    // Qt collation retains accent differences. Normalize sort keys once for
    // case- and accent-insensitive comparison rather than during every comparison.
    static const QRegularExpression marks(QStringLiteral(R"(\p{M})"));
    return text.normalized(QString::NormalizationForm_D).remove(marks);
}
} // namespace
SongCatalogModel::SongCatalogModel(QObject *parent) : QAbstractListModel(parent)
{
    m_viewLoad.setSingleShot(true);
    connect(&m_viewLoad, &QTimer::timeout, this, &SongCatalogModel::flushViewReads);
    m_metadataLoad.setSingleShot(true);
    connect(&m_metadataLoad, &QTimer::timeout, this, &SongCatalogModel::flushMetadata);
    m_metadataRefresh.setSingleShot(true);
    connect(&m_metadataRefresh, &QTimer::timeout, this, &SongCatalogModel::watchMetadata);
    m_presentationFlush.setSingleShot(true);
    connect(&m_presentationFlush, &QTimer::timeout, this, &SongCatalogModel::flushPresentations);
    m_queryRefresh.setSingleShot(true);
    connect(&m_queryRefresh, &QTimer::timeout, this, &SongCatalogModel::flushQuery);
    // QML borrows this application-owned object, including explicit invokable
    // returns. GC and engine teardown must never delete a C++ member/context.
    QQmlEngine::setObjectOwnership(this, QQmlEngine::CppOwnership);

    // Use zh-Hans-CN base collation, independently of OS locale or natural-number sorting.
    m_collator.setLocale(QLocale(QLocale::Chinese, QLocale::SimplifiedHanScript, QLocale::China));
    m_collator.setNumericMode(false);
    m_collator.setCaseSensitivity(Qt::CaseInsensitive);
}
SongCatalogModel::~SongCatalogModel() = default;
void SongCatalogModel::attachMetadata(SongMetadataService *service)
{
    if (m_metadata == service)
        return;
    if (m_metadata)
        m_metadata->unwatch(this);
    disconnect(m_metadataReset);
    m_metadata = service;
    m_metadataObservedCount = 0;
    m_metadataResume = false;
    m_metadataRefresh.stop();
    if (service) {
        m_metadataReset = connect(service, &SongMetadataService::cacheCleared, this, [this] {
            if (m_store && m_metadataObservedCount > 0) {
                QStringList rows, wanted;
                for (const auto &entry : m_entries) {
                    rows.append(entry.id);
                    if (entry.incomplete && !entry.completionId.isEmpty())
                        wanted.append(entry.completionId);
                }
                refreshMetadata(rows);
                if (!m_metadataActive || m_metadata->resumeDemand(this, wanted)) {
                    m_metadataRefresh.start();
                    return;
                }
            }
            m_metadataObservedCount = 0;
            m_metadataRefresh.start();
        });
        QStringList rows;
        for (const auto &entry : m_entries)
            rows.append(entry.id);
        refreshMetadata(rows);
        m_metadataRefresh.start();
    }
}
void SongCatalogModel::setMetadataActive(bool active)
{
    if (m_metadataActive == active)
        return;
    m_metadataActive = active;
    if (!active) {
        if (m_metadata)
            m_metadata->pause(this);
        return;
    }
    if (m_metadata && m_store && m_metadataObservedCount > 0 &&
        m_metadataObservedCount == m_entries.size()) {
        QStringList wanted;
        for (const auto &entry : m_entries)
            if (entry.incomplete && !entry.completionId.isEmpty())
                wanted.append(entry.completionId);
        if (m_metadata->resumeDemand(this, wanted))
            return;
    }
    m_metadataResume = true;
    m_metadataObservedCount = 0;
    watchMetadata();
}
void SongCatalogModel::watchMetadata()
{
    if (!m_metadata || m_cacheStale)
        return;
    if (m_metadataObservedCount && m_metadataObservedCount == m_entries.size())
        return;
    if (m_store) {
        if (m_metadataWatching)
            return;
        const auto first = m_metadataObservedCount;
        const auto last = std::min(first + 128, int(m_entries.size()));
        QList<int> sources;
        for (int source = first; source < last; ++source)
            sources.append(source);
        if (sources.isEmpty())
            return;
        const auto epoch = m_storageEpoch;
        const auto metadata = m_metadata;
        m_metadataWatching = true;
        loadViews(sources, this, [this, first, last, epoch, metadata](auto entries) {
            if (epoch != m_storageEpoch || metadata != m_metadata)
                return;
            m_metadataWatching = false;
            QList<SongMetadataService::Observation> songs;
            QStringList rows;
            for (const auto &entry : entries) {
                songs.append({entry.id, entry.view->domainSong});
                rows.append(entry.id);
            }
            m_metadataObservedCount = last;
            const auto apply = [this](const QStringList &ids) { refreshMetadata(ids); };
            if (first == 0 && std::exchange(m_metadataResume, false))
                metadata->resume(this, songs, apply);
            else
                metadata->watch(this, songs, apply, m_metadataActive, first > 0);
            refreshMetadata(rows);
            if (m_metadataObservedCount < m_entries.size())
                m_metadataRefresh.start();
        });
        return;
    }
    const bool append = m_metadataObservedCount > 0;
    QList<SongMetadataService::Observation> songs;
    if (!m_cards)
        for (int i = m_metadataObservedCount; i < m_entries.size(); ++i) {
            const auto &entry = m_entries[i];
            songs.append({entry.id, entry.view->domainSong});
        }
    m_metadataObservedCount = m_entries.size();
    QStringList rows;
    for (const auto &entry : songs)
        rows.append(entry.row);
    refreshMetadata(rows);
    const auto apply = [this](const QStringList &rows) { refreshMetadata(rows); };
    if (std::exchange(m_metadataResume, false))
        m_metadata->resume(this, songs, apply);
    else
        m_metadata->watch(this, songs, apply, m_metadataActive, append);
}
void SongCatalogModel::refreshMetadata(const QStringList &rows)
{
    if (!m_metadata || m_cards)
        return;
    if (m_store) {
        for (const auto &id : rows)
            if (const auto found = m_indices.constFind(id); found != m_indices.cend())
                m_pendingMetadata.insert(*found);
        m_metadataLoad.start();
        return;
    }
    QSet<int> changed;
    bool orderMayChange = false;
    for (const auto &id : rows) {
        const auto found = m_indices.constFind(id);
        if (found == m_indices.cend())
            continue;
        auto &entry = m_entries[*found];
        const auto song = m_metadata->complete(entry.view->domainSong);
        if (song == entry.view->domainSong)
            continue;
        const auto title = entry.view->title, artist = entry.view->artist,
                   album = entry.view->album;
        const auto duration = entry.view->durationMs;
        applyEntryPresentation(entry, song);
        orderMayChange |= title != entry.view->title || artist != entry.view->artist ||
                          album != entry.view->album || duration != entry.view->durationMs;
        entry.id = id;
        changed.insert(*found);
    }
    notifyPresentations(changed, orderMayChange);
}
void SongCatalogModel::notifyPresentations(const QSet<int> &changed, bool orderMayChange)
{
    if (changed.isEmpty())
        return;
    queuePresentations(changed);
    QStringList identities;
    for (const auto source : changed)
        identities.append(m_entries[source].id);
    emit presentationsApplied(identities);
    if (orderMayChange && (!m_query.isEmpty() || m_sortField >= 2)) {
        rebuild();
        return;
    }
    QList<int> rows;
    for (const auto source : changed)
        if (source < m_viewRows.size() && m_viewRows[source] >= 0)
            rows.append(m_viewRows[source]);
    std::sort(rows.begin(), rows.end());
    for (int i = 0; i < rows.size();) {
        const auto first = rows[i];
        auto last = first;
        while (++i < rows.size() && rows[i] == last + 1)
            last = rows[i];
        emit dataChanged(index(first), index(last));
    }
    emit this->changed();
}
int SongCatalogModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : count();
}
QHash<int, QByteArray> SongCatalogModel::roleNames() const
{
    return {{PresentationReady, "presentationReady"},
            {ArtistLinks, "artistLinks"},
            {AlbumId, "albumId"},
            {Title, "title"},
            {ArtistText, "artistText"},
            {AlbumText, "albumText"},
            {CoverUrl, "coverUrl"},
            {DurationText, "durationText"},
            {ExtraText, "extraText"},
            {ResourceId, "resourceId"},
            {DurationMs, "durationMs"},
            {Selected, "selected"},
            {PrivilegeTags, "privilegeTags"},
            {QualityLabel, "qualityLabel"},
            {QualityTags, "qualityTags"},
            {CatalogPlayable, "catalogPlayable"},
            {Original, "original"}};
}
QVariant SongCatalogModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= count())
        return {};
    return sourceData(m_order.at(index.row()), role);
}
QVariant SongCatalogModel::sourceData(int source, int role) const
{
    if (source < 0 || source >= m_entries.size())
        return {};
    if (role < Title || role > PresentationReady)
        return {};
    const auto &entry = m_entries.at(source);
    if (role == ResourceId)
        return entry.id;
    if (role == Selected)
        return m_selected.contains(entry.id);
    if (role == CatalogPlayable)
        return entry.playable;
    const auto view = viewFor(source);
    if (role == PresentationReady)
        return bool(view);
    if (!view) {
        switch (role) {
        case ResourceId:
            return entry.id;
        case Selected:
            return m_selected.contains(entry.id);
        case CatalogPlayable:
            return entry.playable;
        case Original:
            return false;
        case DurationMs:
            return QVariant::fromValue(quint64(0));
        case PrivilegeTags:
        case QualityTags:
        case ArtistLinks:
            return QVariantList{};
        default:
            return role >= Title && role <= AlbumId ? QVariant(QString()) : QVariant();
        }
    }
    switch (role) {
    case Title:
        return view->title;
    case ArtistText:
        return view->artist;
    case ArtistLinks: {
        const auto refs =
            view->domainSong.contains("artists")
                ? view->domainSong.value("artists").toArray()
                : view->domainSong.value("metadata").toObject().value("artist_refs").toArray();
        return catalogArtistLinks(refs, view->domainSong.value("artists").isArray() ? view->artist
                                                                                    : QString());
    }
    case AlbumId:
        return view->albumId ? QString::number(view->albumId) : QString();
    case AlbumText:
        return view->album;
    case CoverUrl:
        return view->cover;
    case ExtraText:
        return view->extra;
    case ResourceId:
        return entry.id;
    case DurationMs:
        return QVariant::fromValue(view->durationMs);
    case DurationText: {
        if (m_queryStyle == SongDatasetStore::QueryStyle::Catalog &&
            !view->domainSong.value("duration_ms").isDouble())
            return QString();
        const auto seconds = view->durationMs / 1000;
        return QString("%1:%2").arg(seconds / 60).arg(seconds % 60, 2, 10, QLatin1Char('0'));
    }
    case Selected:
        return m_selected.contains(entry.id);
    case PrivilegeTags:
        return view->badges;
    case QualityLabel:
        return view->quality;
    case QualityTags:
        return view->qualityTags;
    case CatalogPlayable:
        return entry.playable;
    case Original:
        return view->original;
    default:
        return {};
    }
}
QVariantMap SongCatalogModel::item(int row) const
{
    QVariantMap result;
    const auto roles = roleNames();
    for (auto it = roles.cbegin(); it != roles.cend(); ++it)
        result.insert(QString::fromLatin1(it.value()), data(index(row), it.key()));
    return result;
}
QVariantMap SongCatalogModel::sourceItem(int row) const
{
    if (row < 0 || row >= m_entries.size())
        return {};
    const auto &e = m_entries[row];
    const auto view = viewFor(row);
    if (!view)
        return {{"presentationReady", false},
                {"resourceId", e.id},
                {"selected", m_selected.contains(e.id)},
                {"catalogPlayable", e.playable},
                {"title", QString()},
                {"artistText", QString()},
                {"coverUrl", QString()}};
    return {{"presentationReady", true},
            {"title", view->title},
            {"artistText", view->artist},
            {"albumText", view->album},
            {"coverUrl", view->cover},
            {"resourceId", e.id},
            {"selected", m_selected.contains(e.id)},
            {"privilegeTags", view->badges},
            {"qualityLabel", view->quality},
            {"qualityTags", view->qualityTags},
            {"catalogPlayable", e.playable},
            {"original", view->original}};
}
SongCatalogModel::Entry SongCatalogModel::songEntry(const QJsonObject &song)
{
    const auto track = PlaybackTrack::fromDomain(song);
    Entry entry;
    entry.view = new Presentation;
    entry.view->domainSong = song;
    entry.view->albumId = track.albumId;
    entry.view->durationMs = track.durationMs;
    entry.trackPlayable = track.playable();
    entry.view->original = track.catalogMetadata.value("originality_code").toInt() > 0;
    entry.id = track.id;
    entry.domainId = track.id;
    entry.hash = track.hash;
    entry.path = track.localPath;
    entry.songId = track.songId;
    entry.completionId = SongMetadataService::completionId(song);
    if (entry.completionId == entry.id)
        entry.completionId = entry.id;
    entry.incomplete = SongMetadataService::needsCompletion(song);
    entry.view->title = track.title;
    entry.view->artist = track.artist;
    entry.view->album = track.album;
    entry.view->cover = track.cover;
    entry.view->titleSort = baseSortText(entry.view->title.trimmed());
    entry.view->artistSort = baseSortText(entry.view->artist);
    entry.view->albumSort = baseSortText(entry.view->album);
    entry.view->badges = songPrivilegeTags(track);
    entry.view->quality = songQualityLabel(track);
    entry.view->qualityTags = songQualityTags(track);
    entry.playable = track.catalogPrivilege != 40 && !songCopyrightRestricted(track);
    return entry;
}
void SongCatalogModel::applyEntryPresentation(Entry &entry, const QJsonObject &song)
{
    auto next = songEntry(song);
    if (entry.record) {
        // Source/rights/quality facts belong to the immutable rich row. Only
        // presentation fields can change through shared metadata completion.
        next.record = entry.record;
        next.source = entry.source;
        next.view->badges = entry.view->badges;
        next.view->quality = entry.view->quality;
        next.view->qualityTags = entry.view->qualityTags;
        next.playable = entry.playable;
        next.trackPlayable = entry.trackPlayable;
        next.view->original = entry.view->original;
        next.view->extra = entry.view->extra;
    }
    next.presentationRevision = entry.presentationRevision + 1;
    entry = std::move(next);
}
void SongCatalogModel::applyPresentations(const QJsonArray &presentations)
{
    if (m_store) {
        for (const auto &value : presentations) {
            const auto id = value.toObject().value("id").toString();
            if (id.isEmpty())
                continue;
            for (const auto source : m_domainIndices.values(id)) {
                m_pendingOverlays[source].append(value);
                m_pendingMetadata.insert(source);
            }
        }
        m_metadataLoad.start();
        return;
    }
    QSet<int> changed;
    for (const auto &value : presentations) {
        const auto p = value.toObject();
        const auto id = p["id"].toString();
        if (id.isEmpty())
            continue;
        const auto matches = m_domainIndices.values(id);
        for (const int source : matches) {
            const auto song =
                SongMetadataService::completeSong(m_entries[source].view->domainSong, p);
            if (song != m_entries[source].view->domainSong) {
                const auto key = m_entries[source].id;
                applyEntryPresentation(m_entries[source], song);
                m_entries[source].id = key;
                changed.insert(source);
            }
        }
    }
    notifyPresentations(changed);
}
QStringList SongCatalogModel::incompletePresentationIds() const
{
    QStringList ids;
    for (const auto &entry : m_entries)
        if (entry.incomplete)
            ids.append(entry.domainId);
    return ids;
}
std::optional<PlaybackTrack> SongCatalogModel::trackProjection(const QString &id) const
{
    const auto it = m_indices.constFind(id);
    if (it == m_indices.cend())
        return std::nullopt;
    const auto &entry = m_entries[*it];
    if (entry.view)
        return PlaybackTrack::fromDomain(entry.view->domainSong);
    if (auto view = m_hotViews.lookup(*it))
        return PlaybackTrack::fromDomain((*view)->domainSong);
    PlaybackTrack key;
    key.id = entry.domainId;
    key.hash = entry.hash;
    key.localPath = entry.path;
    key.songId = entry.songId;
    return key;
}

bool SongCatalogModel::catalogPlayable(const QString &id) const
{
    const auto it = m_indices.constFind(id);
    return it != m_indices.cend() && m_entries[*it].playable;
}
QStringList SongCatalogModel::selectedIds() const
{
    return QStringList(m_selected.cbegin(), m_selected.cend());
}
void SongCatalogModel::appendSongs(const QJsonArray &songs)
{
    m_cards = false;
    QList<Entry> added;
    QStringList completedRows;
    QSet<QString> seen;
    for (const auto &value : songs) {
        const auto original = value.toObject();
        const auto song = m_metadata ? m_metadata->complete(original) : original;
        auto entry = songEntry(song);
        if (entry.id.isEmpty() || m_indices.contains(entry.id) || seen.contains(entry.id))
            entry.id = QStringLiteral("catalog-row:%1:%2")
                           .arg(m_entries.size() + added.size())
                           .arg(entry.id);
        seen.insert(entry.id);
        if (song != original)
            completedRows.append(entry.id);
        added.append(std::move(entry));
    }
    if (added.isEmpty())
        return;
    if (m_metadata)
        m_metadataRefresh.start();
    const bool plain = m_query.trimmed().isEmpty() && !m_sortField && !querying();
    if (plain)
        beginInsertRows({}, count(), count() + added.size() - 1);
    for (auto &entry : added) {
        const int source = m_entries.size();
        if (m_cacheRecovering && m_recoverySelection.contains(entry.id))
            m_selected.insert(entry.id);
        m_indices.insert(entry.id, source);
        m_domainIndices.insert(entry.domainId, source);
        m_entries.append(std::move(entry));
        if (plain) {
            m_viewRows.append(m_order.size());
            m_order.append(source);
        }
    }
    if (plain) {
        endInsertRows();
        emit changed();
    } else
        rebuild();
    if (!completedRows.isEmpty())
        emit presentationsApplied(completedRows);
    flushStorage();
}
void SongCatalogModel::replaceSongs(const QJsonArray &songs, bool preserveSelection)
{
    QList<Entry> entries;
    QSet<QString> ids;
    QStringList completedRows;
    for (const auto &value : songs) {
        const auto original = value.toObject();
        const auto song = m_metadata ? m_metadata->complete(original) : original;
        auto entry = songEntry(song);
        if (entry.id.isEmpty() || ids.contains(entry.id))
            entry.id = QStringLiteral("catalog-row:%1:%2").arg(entries.size()).arg(entry.id);
        ids.insert(entry.id);
        if (song != original)
            completedRows.append(entry.id);
        entries.append(std::move(entry));
    }
    replaceEntries(std::move(entries), false, preserveSelection);
    if (!completedRows.isEmpty())
        emit presentationsApplied(completedRows);
}
void SongCatalogModel::replaceCards(const QJsonArray &cards)
{
    QList<Entry> entries;
    for (const auto &value : cards) {
        const auto o = value.toObject();
        Entry entry;
        entry.view = new Presentation;
        const auto id = o["id"];
        // Card ids arrive as strings from discovery and as numbers from album
        // pages. Preserve either provider representation as an opaque view id.
        entry.id = id.isString()   ? id.toString()
                   : id.isDouble() ? QString::number(id.toInteger())
                                   : QString();
        entry.view->title = o["title"].toString();
        entry.view->artist = o["creator"].toString();
        const auto count = o["song_count"];
        entry.view->extra = entry.view->artist;
        if (count.isDouble() && count.toInteger() > 0) {
            const auto label = tr("%1 首歌曲").arg(count.toInteger());
            entry.view->extra =
                entry.view->extra.isEmpty() ? label : entry.view->extra + " • " + label;
        }
        entry.view->cover = o["cover_url"].toString();
        if (o["extra_text"].isString())
            entry.view->extra = o["extra_text"].toString();
        // Card rows may carry a duration; project it for the shared card badge.
        // Missing durations remain unknown.
        const auto duration = o["duration_ms"];
        if (duration.isDouble() && duration.toInteger() > 0) {
            entry.view->durationMs = quint64(duration.toInteger());
            entry.view->domainSong = QJsonObject{{"duration_ms", duration}};
        }
        entries.append(std::move(entry));
    }
    replaceEntries(std::move(entries), true, false);
}
void SongCatalogModel::replaceEntries(QList<Entry> entries, bool cards, bool preserveSelection,
                                      bool shared, bool preserveCommands)
{
    m_cacheStale = false;
    if (m_cacheRecovering) {
        preserveSelection = true;
        m_selected.unite(m_recoverySelection);
    }
    const auto oldSelection = m_selected;
    QHash<QString, int> incoming, existing;
    bool unique = true;
    for (int i = 0; i < entries.size(); ++i) {
        unique &= !incoming.contains(entries[i].id);
        incoming.insert(entries[i].id, i);
    }
    for (int i = 0; i < m_entries.size(); ++i) {
        unique &= !existing.contains(m_entries[i].id);
        existing.insert(m_entries[i].id, i);
    }
    if (!preserveSelection)
        m_selected.clear();
    else
        m_selected.removeIf([&](const QString &id) { return !incoming.contains(id); });
    if (m_cards == cards && m_entries == entries && oldSelection == m_selected)
        return;
    resetStorage(preserveCommands);
    m_sharedSource = shared;
    QList<int> order;
    if (!shared)
        order = orderedRows(entries);
    else if (m_query.trimmed().isEmpty() && !m_sortField)
        for (int i = 0; i < entries.size(); ++i)
            order.append(i);
    else
        for (const auto source : m_order) {
            const auto found = incoming.constFind(m_entries[source].id);
            if (found != incoming.cend())
                order.append(*found);
        }
    if (shared)
        m_writeCursor = entries.size();
    QSet<QString> changedIds;
    for (const auto &entry : entries) {
        const auto before = existing.constFind(entry.id);
        if (before != existing.cend() &&
            (m_entries[*before] != entry ||
             oldSelection.contains(entry.id) != m_selected.contains(entry.id)))
            changedIds.insert(entry.id);
    }
    if (!unique) {
        // Repeated opaque card IDs are displayable, but cannot address a
        // persistent row unambiguously. Preserve the old fallback for them.
        beginResetModel();
    } else {
        QList<int> target;
        QSet<int> wanted;
        for (int index : order) {
            const auto &entry = entries[index];
            auto found = existing.constFind(entry.id);
            int backing;
            if (found == existing.cend()) {
                backing = m_entries.size();
                existing.insert(entry.id, backing);
                m_indices.insert(entry.id, backing);
                m_entries.append(entry);
            } else
                backing = *found;
            target.append(backing);
            wanted.insert(backing);
        }
        for (int row = m_order.size() - 1; row >= 0;)
            if (!wanted.contains(m_order[row])) {
                const int last = row;
                while (row >= 0 && !wanted.contains(m_order[row]))
                    --row;
                const int first = row + 1;
                beginRemoveRows({}, first, last);
                m_order.remove(first, last - first + 1);
                endRemoveRows();
            } else
                --row;
        QSet<int> visible(m_order.cbegin(), m_order.cend());
        for (int row = 0; row < target.size(); ++row) {
            if (row < m_order.size() && m_order[row] == target[row])
                continue;
            const int from = m_order.indexOf(target[row], row);
            if (from < 0) {
                const int first = row;
                while (row + 1 < target.size() && !visible.contains(target[row + 1]))
                    ++row;
                beginInsertRows({}, first, row);
                for (int at = first; at <= row; ++at) {
                    m_order.insert(at, target[at]);
                    visible.insert(target[at]);
                }
                endInsertRows();
            } else {
                beginMoveRows({}, from, from, {}, row);
                m_order.move(from, row);
                endMoveRows();
            }
        }
    }
    m_cards = cards;
    m_entries = std::move(entries);
    m_order = order;
    m_indices.clear();
    m_domainIndices.clear();
    m_viewRows.fill(-1, m_entries.size());
    for (int i = 0; i < m_entries.size(); ++i) {
        m_indices.insert(m_entries[i].id, i);
        m_domainIndices.insert(m_entries[i].domainId, i);
    }
    for (int row = 0; row < m_order.size(); ++row)
        m_viewRows[m_order[row]] = row;
    if (!unique)
        endResetModel();
    else
        for (int row = 0; row < m_order.size(); ++row)
            if (changedIds.contains(m_entries[m_order[row]].id))
                emit dataChanged(index(row), index(row));
    m_metadataObservedCount = 0;
    m_metadataResume = false;
    if (m_metadata) {
        m_metadata->unwatch(this);
        if (cards) {
            m_metadataRefresh.stop();
            m_metadata->unwatch(this);
        } else
            m_metadataRefresh.start();
    }
    emit changed();
    if (oldSelection != m_selected)
        emit selectionChanged();
    flushStorage();
    if (shared && (!m_query.trimmed().isEmpty() || m_sortField))
        rebuild();
}
void SongCatalogModel::clear()
{
    const bool controlsChanged = !m_query.isEmpty() || m_sortField || m_sortDirection;
    const bool empty = m_entries.isEmpty();
    if (!m_cacheRecovering) {
        m_query.clear();
        m_sortField = m_sortDirection = 0;
    }
    replaceSongs({});
    if (empty && controlsChanged)
        emit changed();
}
void SongCatalogModel::setQuery(const QString &value)
{
    if (m_query == value)
        return;
    m_query = value;
    rebuild();
}
void SongCatalogModel::setSort(int field, int direction)
{
    if (m_sortField == field && m_sortDirection == direction)
        return;
    m_sortField = field;
    m_sortDirection = direction;
    rebuild();
}
void SongCatalogModel::toggleSort(int field)
{
    if (field < 1 || field > 5)
        return;
    if (m_sortField == field) {
        m_sortDirection = (m_sortDirection + 1) % 3;
        if (!m_sortDirection)
            m_sortField = 0;
    } else {
        m_sortField = field;
        m_sortDirection = 1;
    }
    rebuild();
}
void SongCatalogModel::clearSort()
{
    if (!m_sortField && !m_sortDirection)
        return;
    m_sortField = 0;
    m_sortDirection = 0;
    rebuild();
}
QList<int> SongCatalogModel::orderedRows(const QList<Entry> &entries) const
{
    QList<int> order;
    const auto needle = m_query.trimmed();
    for (int i = 0; i < entries.size(); ++i) {
        const auto &e = entries[i];
        if (needle.isEmpty() || (e.view->title.contains(needle, Qt::CaseInsensitive) ||
                                 e.view->artist.contains(needle, Qt::CaseInsensitive) ||
                                 e.view->album.contains(needle, Qt::CaseInsensitive)))
            order.append(i);
    }
    if (m_sortField)
        std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
            const auto &x = entries[a], &y = entries[b];
            int comparison = 0;
            switch (m_sortField) {
            case 1:
                comparison = a < b ? -1 : a > b ? 1 : 0;
                break;
            case 2:
                comparison = m_collator.compare(x.view->titleSort, y.view->titleSort);
                if (!comparison)
                    comparison = m_collator.compare(x.view->artistSort, y.view->artistSort);
                break;
            case 3:
                comparison = m_collator.compare(x.view->artistSort, y.view->artistSort);
                if (!comparison)
                    comparison = m_collator.compare(x.view->titleSort, y.view->titleSort);
                break;
            case 4:
                comparison = m_collator.compare(x.view->albumSort, y.view->albumSort);
                break;
            case 5:
                comparison = x.view->durationMs < y.view->durationMs   ? -1
                             : x.view->durationMs > y.view->durationMs ? 1
                                                                       : 0;
                break;
            }
            return m_sortDirection == 2 ? comparison > 0 : comparison < 0;
        });
    return order;
}
void SongCatalogModel::rebuild(bool resetStarted)
{
    if (m_store && !m_cards && !resetStarted) {
        if (m_storageFailed) {
            emit changed();
            return;
        }
        ++m_viewRevision;
        m_queryNeeded = true;
        m_queryRefresh.start();
        emit changed();
        return;
    }
    if (!resetStarted)
        beginResetModel();
    m_order = orderedRows(m_entries);
    m_viewRows.fill(-1, m_entries.size());
    for (int row = 0; row < m_order.size(); ++row)
        m_viewRows[m_order[row]] = row;
    endResetModel();
    emit changed();
}
void SongCatalogModel::applyOrder(QList<int> order)
{
    if (m_order != order) {
        beginResetModel();
        m_order = std::move(order);
        m_viewRows.fill(-1, m_entries.size());
        for (int row = 0; row < m_order.size(); ++row)
            m_viewRows[m_order[row]] = row;
        endResetModel();
    }
    emit changed();
}
int SongCatalogModel::indexOf(const QString &id) const
{
    for (int i = 0; i < m_order.size(); ++i)
        if (m_entries[m_order[i]].id == id)
            return i;
    return -1;
}
bool SongCatalogModel::hasPlayableTracks() const
{
    return std::any_of(m_order.cbegin(), m_order.cend(), [this](int source) {
        const auto &entry = m_entries[source];
        return entry.playable && entry.trackPlayable;
    });
}
void SongCatalogModel::toggleSelected(const QString &id)
{
    if (std::none_of(m_entries.cbegin(), m_entries.cend(),
                     [&](const auto &e) { return e.id == id; }))
        return;
    if (m_selected.contains(id))
        m_selected.remove(id);
    else
        m_selected.insert(id);
    const int row = indexOf(id);
    if (row >= 0)
        emit dataChanged(index(row), index(row), {Selected});
    emit selectionChanged();
}
void SongCatalogModel::selectAll(bool selected)
{
    m_selected.clear();
    if (selected)
        for (const auto &e : m_entries)
            m_selected.insert(e.id);
    if (count())
        emit dataChanged(index(0), index(count() - 1), {Selected});
    emit selectionChanged();
}
void SongCatalogModel::clearSelection()
{
    selectAll(false);
}
} // namespace EchoMusic
