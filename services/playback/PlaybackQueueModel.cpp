#include "PlaybackQueueModel.h"
#include "ArtistCoverPresentation.h"
#include "EmbeddedCoverPresentation.h"
#include "SongMetadataService.h"
#include <algorithm>
namespace EchoMusic
{
PlaybackQueueModel::PlaybackQueueModel(QObject *parent) : QAbstractListModel(parent)
{
    connect(this, &PlaybackQueueModel::changed, this, &PlaybackQueueModel::resumeTitleChanged);
    connect(this, &QAbstractItemModel::dataChanged, this,
            [this](const QModelIndex &first, const QModelIndex &last, const QList<int> &roles) {
                const int row = qMax(0, m_current);
                if (first.row() <= row && row <= last.row() &&
                    (roles.isEmpty() || roles.contains(Title)))
                    emit resumeTitleChanged();
            });
    m_metadataRefresh.setSingleShot(true);
    connect(&m_metadataRefresh, &QTimer::timeout, this, &PlaybackQueueModel::watchMetadata);
    const auto schedule = [this] {
        if (m_metadata)
            m_metadataRefresh.start();
    };
    connect(this, &QAbstractItemModel::modelReset, this, schedule);
    connect(this, &QAbstractItemModel::rowsInserted, this, schedule);
    connect(this, &QAbstractItemModel::rowsRemoved, this, schedule);
    connect(this, &QAbstractItemModel::rowsMoved, this, schedule);
}
QString PlaybackQueueModel::resumeTitle() const
{
    const auto *track = at(qMax(0, m_current));
    return track ? track->title : QString();
}
void PlaybackQueueModel::attachMetadata(SongMetadataService *service)
{
    if (m_metadata == service)
        return;
    if (m_metadata)
        m_metadata->unwatch(this);
    disconnect(m_metadataReset);
    m_metadata = service;
    if (service)
        m_metadataReset = connect(service, &SongMetadataService::cacheCleared, this,
                                  [this] { m_metadataRefresh.start(); });
    watchMetadata();
}
void PlaybackQueueModel::watchMetadata()
{
    if (!m_metadata)
        return;
    QList<SongMetadataService::Observation> songs;
    QStringList rows;
    for (int i = 0; i < m_tracks.size(); ++i) {
        const auto row = QString::number(i);
        songs.append({row, m_tracks[i].toCatalog()});
        rows.append(row);
    }
    refreshMetadata(rows);
    m_metadata->watch(this, songs,
                      [this](const QStringList &changed) { refreshMetadata(changed); });
}
void PlaybackQueueModel::refreshMetadata(const QStringList &rows)
{
    if (!m_metadata)
        return;
    bool changed = false;
    for (const auto &key : rows) {
        const auto row = key.toInt();
        if (row < 0 || row >= m_tracks.size())
            continue;
        auto &track = m_tracks[row];
        if (track.applyPresentation(m_metadata->complete(track.toCatalog()))) {
            emit dataChanged(index(row), index(row));
            changed = true;
        }
    }
    if (changed)
        emit presentationChanged();
}

void PlaybackQueueModel::restoreNext(const QStringList &keys)
{
    m_next.clear();
    m_nextSet.clear();
    QSet<QString> valid;
    for (const auto &t : m_tracks)
        valid.insert(t.key());
    for (const auto &key : keys)
        if (valid.contains(key) && !m_nextSet.contains(key)) {
            m_next.append(key);
            m_nextSet.insert(key);
        }
    if (rowCount())
        emit dataChanged(index(0), index(rowCount() - 1), {QueuedNext});
    emit changed();
}
QVariant PlaybackQueueModel::data(const QModelIndex &i, int role) const
{
    const auto *t = at(i.row());
    if (!i.isValid() || !t)
        return {};
    switch (role) {
    case Title:
        return t->title;
    case ArtistText:
        return t->artist;
    case AlbumText:
        return t->album;
    case CoverUrl:
        return t->cover;
    case ResourceId:
        return t->key();
    case DurationText:
        return QString("%1:%2")
            .arg(t->durationMs / 60000)
            .arg(t->durationMs / 1000 % 60, 2, 10, QChar('0'));
    case Current:
        return i.row() == m_current;
    case QueuedNext:
        return m_nextSet.contains(t->key());
    default:
        return {};
    }
}
QHash<int, QByteArray> PlaybackQueueModel::roleNames() const
{
    return {{Title, "title"},       {ArtistText, "artistText"},     {AlbumText, "albumText"},
            {CoverUrl, "coverUrl"}, {DurationText, "durationText"}, {ResourceId, "resourceId"},
            {Current, "isCurrent"}, {QueuedNext, "queuedNext"}};
}
void PlaybackQueueModel::replace(PlaybackTracks tracks, int current)
{
    const QString selected =
        current >= 0 && current < tracks.size() ? tracks[current].key() : QString();
    const QString selectedStable =
        current >= 0 && current < tracks.size() ? tracks[current].stableKey() : QString();
    PlaybackTracks clean;
    QSet<QString> seen, ids;
    // One upload may queue under a superseded identity ("0" snapshots from
    // older sessions) and its file-scoped identity. Fold those on the audio
    // hash and keep the snapshot with the richer, more current facts.
    const auto score = [](const PlaybackTrack &t) {
        int value = 0;
        if (!t.id.isEmpty() && t.id != "0")
            value += 4;
        if (!t.catalogMetadata.value("cloud_source").toObject().isEmpty())
            value += 2;
        if (t.durationMs)
            value += 2;
        if (!t.cover.isEmpty())
            value += 1;
        return value;
    };
    // "0" is the provider placeholder, not a view identity: several unmatched
    // uploads may share it until each adopts cloud:<hash>.
    const auto hasViewId = [](const PlaybackTrack &t) { return !t.id.isEmpty() && t.id != "0"; };
    for (auto &t : tracks) {
        if (!t.playable() || seen.contains(t.key()) || (hasViewId(t) && ids.contains(t.id)))
            continue;
        if (t.songId == 0 && !t.hash.isEmpty()) {
            auto existing = std::find_if(clean.begin(), clean.end(), [&](const auto &row) {
                return row.songId == 0 && !row.hash.isEmpty() &&
                       row.hash.compare(t.hash, Qt::CaseInsensitive) == 0;
            });
            if (existing != clean.end()) {
                if (score(t) >= score(*existing))
                    *existing = t;
                // Register the surviving identity too so a later duplicate of
                // the winner's view id still folds instead of stacking.
                seen.insert(t.key());
                if (hasViewId(t))
                    ids.insert(t.id);
                continue;
            }
        }
        seen.insert(t.key());
        if (hasViewId(t))
            ids.insert(t.id);
        clean.append(std::move(t));
    }
    bool appendOnly = clean.size() >= m_tracks.size();
    for (int i = 0; appendOnly && i < m_tracks.size(); ++i)
        appendOnly = clean[i].stableKey() == m_tracks[i].stableKey();
    QStringList nextStable;
    if (appendOnly)
        for (const auto &key : m_next)
            for (const auto &track : m_tracks)
                if (track.key() == key)
                    nextStable.append(track.stableKey());
    beginResetModel();
    ++m_revision;
    m_tracks = std::move(clean);
    m_next.clear();
    m_nextSet.clear();
    for (const auto &stable : nextStable)
        for (const auto &track : m_tracks)
            if (track.stableKey() == stable) {
                m_next.append(track.key());
                m_nextSet.insert(track.key());
                break;
            }
    m_current = -1;
    for (int i = 0; i < m_tracks.size(); ++i)
        if (m_tracks[i].key() == selected)
            m_current = i;
    if (m_current < 0 && !selectedStable.isEmpty())
        for (int i = 0; i < m_tracks.size(); ++i)
            if (m_tracks[i].stableKey() == selectedStable) {
                m_current = i;
                break;
            }
    endResetModel();
    emit changed();
}
void PlaybackQueueModel::updateFacts(const QString &id, const QString &hash, const QString &title,
                                     const QString &artist, const QString &album,
                                     const QString &cover, quint64 durationMs)
{
    if ((id.isEmpty() && hash.isEmpty()) ||
        (title.isEmpty() && artist.isEmpty() && album.isEmpty() && cover.isEmpty() && !durationMs))
        return;
    const bool hasViewId = !id.isEmpty() && id != "0";
    int target = -1;
    for (int i = 0; i < m_tracks.size(); ++i) {
        const auto &row = m_tracks[i];
        // Legacy "0" and file-scoped snapshots of one upload share the audio
        // hash; fall back to it so facts cannot land on an unrelated upload.
        const bool sameAudio = row.localPath.isEmpty() && !hash.isEmpty() && row.songId == 0 &&
                               row.hash.compare(hash, Qt::CaseInsensitive) == 0;
        if ((hasViewId && row.id == id) || sameAudio) {
            target = i;
            break;
        }
    }
    if (target < 0)
        return;
    auto &row = m_tracks[target];
    bool changed = false;
    // Masked provider names ("***") are censored identities, not facts; embedded
    // tags carry the real value and replace empty or masked presentation only.
    const auto adoptable = [](const QString &value) {
        return value.trimmed().isEmpty() || SongMetadataService::maskedName(value);
    };
    if (!artist.isEmpty() && adoptable(row.artist) && row.artist != artist) {
        row.artist = artist;
        // The bar renders artistLinks from retained refs; keep the queue copy
        // from re-exposing the masked provider identities later.
        const auto refs = SongMetadataService::artistRefs(artist);
        if (!refs.isEmpty()) {
            row.catalogMetadata["artist_refs"] = refs;
            row.catalogMetadata.remove("filename_artist_name");
        }
        changed = true;
    }
    if (!title.isEmpty() && adoptable(row.title) && row.title != title) {
        row.title = title;
        changed = true;
    }
    if (!album.isEmpty() && adoptable(row.album) && row.album != album) {
        row.album = album;
        changed = true;
    }
    // This legacy channel carries durable provider covers. Display fallbacks
    // must not block a higher-priority fact or lose their provenance.
    if (!cover.isEmpty() && row.cover != cover &&
        (row.cover.trimmed().isEmpty() || hasArtistCover(row.catalogMetadata, row.cover) ||
         hasEmbeddedCover(row.catalogMetadata, row.cover))) {
        row.cover = cover;
        row.catalogMetadata.remove("artist_cover_url");
        row.catalogMetadata.remove("embedded_cover_url");
        changed = true;
    }
    // The demuxed duration is authoritative for uploads without provider data.
    if (durationMs && !row.durationMs) {
        row.durationMs = durationMs;
        changed = true;
    }
    if (changed)
        emit dataChanged(index(target), index(target),
                         {Title, ArtistText, AlbumText, CoverUrl, DurationText});
}
void PlaybackQueueModel::adoptIdentity(int row, const PlaybackTrack &track)
{
    if (row < 0 || row >= m_tracks.size())
        return;
    auto &current = m_tracks[row];
    if (current.id == track.id &&
        current.catalogMetadata.value("cloud_source") ==
            track.catalogMetadata.value("cloud_source") &&
        current.durationMs == track.durationMs)
        return;
    current.id = track.id;
    current.catalogMetadata = track.catalogMetadata;
    if (track.durationMs && current.durationMs != track.durationMs)
        current.durationMs = track.durationMs;
    emit dataChanged(index(row), index(row));
    emit changed();
}
void PlaybackQueueModel::setCurrent(int row)
{
    if (row < -1 || row >= m_tracks.size() || row == m_current)
        return;
    int old = m_current;
    m_current = row;
    if (old >= 0)
        emit dataChanged(index(old), index(old), {Current});
    if (row >= 0)
        emit dataChanged(index(row), index(row), {Current});
    emit changed();
}
void PlaybackQueueModel::append(const PlaybackTracks &tracks, bool next,
                                const QString &currentTrackId, bool sequential)
{
    const auto same = [](const PlaybackTrack &a, const PlaybackTrack &b) {
        return (!a.id.isEmpty() && a.id == b.id) ||
               (!a.hash.isEmpty() && a.hash.compare(b.hash, Qt::CaseInsensitive) == 0) ||
               (!a.localPath.isEmpty() && a.localPath == b.localPath);
    };
    if (next) {
        auto ordered = m_tracks;
        auto priorities = m_next;
        const auto selected = at(m_current) ? at(m_current)->key() : QString();
        const auto current =
            currentTrackId.isEmpty()
                ? (at(m_current) && !at(m_current)->id.isEmpty() ? at(m_current)->id : selected)
                : currentTrackId;
        for (const auto &incoming : tracks) {
            if (!incoming.playable() ||
                (incoming.id.isEmpty() ? incoming.key() : incoming.id) == current)
                continue;
            if (std::any_of(ordered.cbegin(), ordered.cend(), [&](const auto &t) {
                    return (t.id.isEmpty() ? t.key() : t.id) == current && same(t, incoming);
                }))
                continue;
            int currentIndex = -1, existing = -1;
            for (int i = 0; i < ordered.size(); ++i) {
                if ((ordered[i].id.isEmpty() ? ordered[i].key() : ordered[i].id) == current)
                    currentIndex = i;
                if (same(ordered[i], incoming))
                    existing = i;
            }
            int insert = currentIndex + 1;
            if (sequential || tracks.size() > 1)
                while (insert < ordered.size() && priorities.contains(ordered[insert].key()))
                    ++insert;
            const auto track = existing >= 0 ? ordered[existing] : incoming;
            if (existing >= 0) {
                ordered.removeAt(existing);
                if (existing < insert)
                    --insert;
            }
            ordered.insert(qBound(0, insert, int(ordered.size())), track);
            priorities.removeAll(track.key());
            if (!sequential && tracks.size() == 1)
                priorities.prepend(track.key());
            else
                priorities.append(track.key());
        }
        if (ordered == m_tracks && priorities == m_next)
            return;
        beginResetModel();
        ++m_revision;
        m_tracks = std::move(ordered);
        m_next = priorities;
        m_nextSet = QSet<QString>(m_next.cbegin(), m_next.cend());
        m_current = -1;
        for (int i = 0; i < m_tracks.size(); ++i)
            if (m_tracks[i].key() == selected)
                m_current = i;
        endResetModel();
        emit changed();
        return;
    }
    PlaybackTracks added;
    QSet<QString> ids, hashes, paths;
    for (const auto &track : m_tracks) {
        if (!track.id.isEmpty())
            ids.insert(track.id);
        if (!track.hash.isEmpty())
            hashes.insert(track.hash.toLower());
        if (!track.localPath.isEmpty())
            paths.insert(track.localPath);
    }
    for (const auto &track : tracks)
        if (track.playable() && !ids.contains(track.id) &&
            (track.hash.isEmpty() || !hashes.contains(track.hash.toLower())) &&
            (track.localPath.isEmpty() || !paths.contains(track.localPath))) {
            added.append(track);
            if (!track.id.isEmpty())
                ids.insert(track.id);
            if (!track.hash.isEmpty())
                hashes.insert(track.hash.toLower());
            if (!track.localPath.isEmpty())
                paths.insert(track.localPath);
        }
    if (added.isEmpty())
        return;
    const int n = rowCount();
    beginInsertRows({}, n, n + int(added.size()) - 1);
    ++m_revision;
    m_tracks.append(added);
    endInsertRows();
    emit changed();
}
void PlaybackQueueModel::consumePlayedPriority(const QString &trackId)
{
    for (int row = 0; row < m_tracks.size(); ++row)
        if ((m_tracks[row].id.isEmpty() ? m_tracks[row].key() : m_tracks[row].id) == trackId &&
            m_nextSet.remove(m_tracks[row].key())) {
            m_next.removeAll(m_tracks[row].key());
            emit dataChanged(index(row), index(row), {QueuedNext});
            emit changed();
            break;
        }
}
void PlaybackQueueModel::remove(int row)
{
    if (!at(row))
        return;
    const bool active = row == m_current;
    m_next.removeAll(m_tracks[row].key());
    m_nextSet.remove(m_tracks[row].key());
    beginRemoveRows({}, row, row);
    ++m_revision;
    m_tracks.removeAt(row);
    if (active)
        m_current = -1;
    else if (row < m_current)
        --m_current;
    endRemoveRows();
    emit changed();
    if (active)
        emit currentRemoved();
}
void PlaybackQueueModel::move(int from, int to)
{
    if (!at(from) || !at(to) || from == to)
        return;
    const auto current = at(m_current) ? at(m_current)->key() : QString();
    beginMoveRows({}, from, from, {}, to > from ? to + 1 : to);
    ++m_revision;
    m_tracks.move(from, to);
    for (int i = 0; i < m_tracks.size(); ++i)
        if (m_tracks[i].key() == current)
            m_current = i;
    endMoveRows();
    emit changed();
}
void PlaybackQueueModel::clear()
{
    replace({});
    emit currentRemoved();
}
int PlaybackQueueModel::takeNextPriority()
{
    return takeNextPriority({});
}
int PlaybackQueueModel::peekNextPriority() const
{
    return peekNextPriority({});
}
int PlaybackQueueModel::takeNextPriority(const QString &excludeId)
{
    bool consumed = false;
    while (!m_next.isEmpty()) {
        const auto key = m_next.takeFirst();
        m_nextSet.remove(key);
        consumed = true;
        for (int row = 0; row < m_tracks.size(); ++row)
            if (m_tracks[row].key() == key) {
                emit dataChanged(index(row), index(row), {QueuedNext});
                if ((m_tracks[row].id.isEmpty() ? m_tracks[row].key() : m_tracks[row].id) !=
                        excludeId &&
                    m_tracks[row].playable()) {
                    emit changed();
                    return row;
                }
                break;
            }
    }
    if (consumed)
        emit changed();
    return -1;
}
int PlaybackQueueModel::peekNextPriority(const QString &excludeId) const
{
    for (const auto &key : m_next)
        for (int row = 0; row < m_tracks.size(); ++row)
            if (m_tracks[row].key() == key &&
                (m_tracks[row].id.isEmpty() ? m_tracks[row].key() : m_tracks[row].id) !=
                    excludeId &&
                m_tracks[row].playable())
                return row;
    return -1;
}
} // namespace EchoMusic
