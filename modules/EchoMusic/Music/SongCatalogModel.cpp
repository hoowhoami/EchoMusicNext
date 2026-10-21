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
    // QML borrows this application-owned object, including explicit invokable
    // returns. GC and engine teardown must never delete a C++ member/context.
    QQmlEngine::setObjectOwnership(this, QQmlEngine::CppOwnership);

    // Use zh-Hans-CN base collation, independently of OS locale or natural-number sorting.
    m_collator.setLocale(QLocale(QLocale::Chinese, QLocale::SimplifiedHanScript, QLocale::China));
    m_collator.setNumericMode(false);
    m_collator.setCaseSensitivity(Qt::CaseInsensitive);
}
int SongCatalogModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : count();
}
QHash<int, QByteArray> SongCatalogModel::roleNames() const
{
    return {{ArtistLinks, "artistLinks"},
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
    const auto &entry = m_entries.at(m_order.at(index.row()));
    switch (role) {
    case Title:
        return entry.title;
    case ArtistText:
        return entry.artist;
    case ArtistLinks: {
        const auto refs =
            entry.domainSong.contains("artists")
                ? entry.domainSong.value("artists").toArray()
                : entry.domainSong.value("metadata").toObject().value("artist_refs").toArray();
        return catalogArtistLinks(refs, entry.domainSong.value("artists").isArray() ? entry.artist
                                                                                    : QString());
    }
    case AlbumId:
        return entry.albumId ? QString::number(entry.albumId) : QString();
    case AlbumText:
        return entry.album;
    case CoverUrl:
        return entry.cover;
    case ExtraText:
        return entry.extra;
    case ResourceId:
        return entry.id;
    case DurationMs:
        return QVariant::fromValue(entry.durationMs);
    case DurationText: {
        if (!entry.domainSong.value("duration_ms").isDouble())
            return QString();
        const auto seconds = entry.durationMs / 1000;
        return QString("%1:%2").arg(seconds / 60).arg(seconds % 60, 2, 10, QLatin1Char('0'));
    }
    case Selected:
        return m_selected.contains(entry.id);
    case PrivilegeTags:
        return entry.badges;
    case QualityLabel:
        return entry.quality;
    case QualityTags:
        return entry.qualityTags;
    case CatalogPlayable:
        return entry.playable;
    case Original:
        return entry.original;
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
    return {{"title", e.title},
            {"artistText", e.artist},
            {"albumText", e.album},
            {"coverUrl", e.cover},
            {"resourceId", e.id},
            {"selected", m_selected.contains(e.id)},
            {"privilegeTags", e.badges},
            {"qualityLabel", e.quality},
            {"qualityTags", e.qualityTags},
            {"catalogPlayable", e.playable},
            {"original", e.original}};
}
SongCatalogModel::Entry SongCatalogModel::songEntry(const QJsonObject &song)
{
    const auto track = PlaybackTrack::fromDomain(song);
    Entry entry;
    entry.domainSong = song;
    entry.albumId = track.albumId;
    entry.durationMs = track.durationMs;
    entry.trackPlayable = track.playable();
    entry.original = track.catalogMetadata.value("originality_code").toInt() > 0;
    entry.id = track.id;
    entry.title = track.title;
    entry.artist = track.artist;
    entry.album = track.album;
    entry.cover = track.cover;
    entry.titleSort = baseSortText(entry.title.trimmed());
    entry.artistSort = baseSortText(entry.artist);
    entry.albumSort = baseSortText(entry.album);
    entry.badges = songPrivilegeTags(track);
    entry.quality = songQualityLabel(track);
    entry.qualityTags = songQualityTags(track);
    entry.playable = track.catalogPrivilege != 40 &&
                     (track.catalogPrivilege != 5 ||
                      track.catalogMetadata.value("old_copyright_code").toInt() == 1);
    return entry;
}
void SongCatalogModel::applyPresentations(const QJsonArray &presentations)
{
    QSet<int> changed;
    QMultiHash<QString, int> sources;
    for (int source = 0; source < m_entries.size(); ++source)
        sources.insert(m_entries[source].domainSong.value("id").toString(), source);
    for (const auto &value : presentations) {
        const auto p = value.toObject();
        const auto id = p["id"].toString();
        if (id.isEmpty())
            continue;
        const auto matches = sources.values(id);
        for (const int source : matches) {
            const auto song = SongMetadataService::completeSong(m_entries[source].domainSong, p);
            if (song != m_entries[source].domainSong) {
                const auto key = m_entries[source].id;
                m_entries[source] = songEntry(song);
                m_entries[source].id = key;
                changed.insert(source);
            }
        }
    }
    if (changed.isEmpty())
        return;
    if (!m_query.isEmpty() || (m_sortField >= 2 && m_sortField <= 4)) {
        rebuild();
        return;
    }
    for (int row = 0; row < m_order.size(); ++row)
        if (changed.contains(m_order[row]))
            emit dataChanged(index(row), index(row));
    emit this->changed();
}
QStringList SongCatalogModel::incompletePresentationIds() const
{
    QStringList ids;
    for (const auto &entry : m_entries)
        if (SongMetadataService::needsCompletion(entry.domainSong))
            ids.append(entry.domainSong.value("id").toString());
    return ids;
}
std::optional<PlaybackTrack> SongCatalogModel::track(const QString &id) const
{
    const auto it = m_indices.constFind(id);
    return it == m_indices.cend()
               ? std::nullopt
               : std::optional(PlaybackTrack::fromDomain(m_entries[*it].domainSong));
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
    QList<Entry> added;
    QSet<QString> seen;
    for (const auto &value : songs) {
        auto entry = songEntry(value.toObject());
        if (entry.id.isEmpty() || m_indices.contains(entry.id) || seen.contains(entry.id))
            entry.id = QStringLiteral("catalog-row:%1:%2")
                           .arg(m_entries.size() + added.size())
                           .arg(entry.id);
        seen.insert(entry.id);
        added.append(std::move(entry));
    }
    if (added.isEmpty())
        return;
    const bool plain = m_query.trimmed().isEmpty() && !m_sortField;
    if (plain)
        beginInsertRows({}, count(), count() + added.size() - 1);
    for (auto &entry : added) {
        const int source = m_entries.size();
        m_indices.insert(entry.id, source);
        m_entries.append(std::move(entry));
        if (plain)
            m_order.append(source);
    }
    if (plain) {
        endInsertRows();
        emit changed();
    } else
        rebuild();
}
void SongCatalogModel::replaceSongs(const QJsonArray &songs, bool preserveSelection)
{
    beginResetModel();
    m_entries.clear();
    m_indices.clear();
    m_order.clear();
    if (!preserveSelection)
        m_selected.clear();
    for (const auto &value : songs) {
        auto entry = songEntry(value.toObject());
        if (entry.id.isEmpty() || m_indices.contains(entry.id))
            entry.id = QStringLiteral("catalog-row:%1:%2").arg(m_entries.size()).arg(entry.id);
        m_indices.insert(entry.id, m_entries.size());
        m_entries.append(std::move(entry));
    }
    m_selected.removeIf([this](const QString &id) { return !m_indices.contains(id); });
    rebuild(true);
    emit selectionChanged();
}
void SongCatalogModel::replaceCards(const QJsonArray &cards)
{
    beginResetModel();
    m_entries.clear();
    m_indices.clear();
    m_order.clear();
    m_selected.clear();
    for (const auto &value : cards) {
        const auto o = value.toObject();
        Entry entry;
        const auto id = o["id"];
        // Card ids arrive as strings from discovery and as numbers from album
        // pages. Preserve either provider representation as an opaque view id.
        entry.id = id.isString()   ? id.toString()
                   : id.isDouble() ? QString::number(id.toInteger())
                                   : QString();
        entry.title = o["title"].toString();
        entry.artist = o["creator"].toString();
        const auto count = o["song_count"];
        entry.extra = entry.artist;
        if (count.isDouble() && count.toInteger() > 0) {
            const auto label = tr("%1 首歌曲").arg(count.toInteger());
            entry.extra = entry.extra.isEmpty() ? label : entry.extra + " • " + label;
        }
        entry.cover = o["cover_url"].toString();
        if (o["extra_text"].isString())
            entry.extra = o["extra_text"].toString();
        // Card rows may carry a duration; project it for the shared card badge.
        // Missing durations remain unknown.
        const auto duration = o["duration_ms"];
        if (duration.isDouble() && duration.toInteger() > 0) {
            entry.durationMs = quint64(duration.toInteger());
            entry.domainSong = QJsonObject{{"duration_ms", duration}};
        }
        m_entries.append(std::move(entry));
    }
    rebuild(true);
    emit selectionChanged();
}
void SongCatalogModel::clear()
{
    m_query.clear();
    m_sortField = m_sortDirection = 0;
    replaceSongs({});
}
void SongCatalogModel::setQuery(const QString &value)
{
    if (m_query == value)
        return;
    m_query = value;
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
void SongCatalogModel::rebuild(bool resetStarted)
{
    if (!resetStarted)
        beginResetModel();
    m_order.clear();
    const auto needle = m_query.trimmed();
    for (int i = 0; i < m_entries.size(); ++i) {
        const auto &e = m_entries[i];
        if (needle.isEmpty() || (e.title.contains(needle, Qt::CaseInsensitive) ||
                                 e.artist.contains(needle, Qt::CaseInsensitive) ||
                                 e.album.contains(needle, Qt::CaseInsensitive)))
            m_order.append(i);
    }
    if (m_sortField)
        std::stable_sort(m_order.begin(), m_order.end(), [&](int a, int b) {
            const auto &x = m_entries[a], &y = m_entries[b];
            int comparison = 0;
            switch (m_sortField) {
            case 1:
                comparison = a < b ? -1 : a > b ? 1 : 0;
                break;
            case 2:
                comparison = m_collator.compare(x.titleSort, y.titleSort);
                if (!comparison)
                    comparison = m_collator.compare(x.artistSort, y.artistSort);
                break;
            case 3:
                comparison = m_collator.compare(x.artistSort, y.artistSort);
                if (!comparison)
                    comparison = m_collator.compare(x.titleSort, y.titleSort);
                break;
            case 4:
                comparison = m_collator.compare(x.albumSort, y.albumSort);
                break;
            case 5:
                comparison = x.durationMs < y.durationMs ? -1 : x.durationMs > y.durationMs ? 1 : 0;
                break;
            }
            return m_sortDirection == 2 ? comparison > 0 : comparison < 0;
        });
    endResetModel();
    emit changed();
}
int SongCatalogModel::indexOf(const QString &id) const
{
    for (int i = 0; i < m_order.size(); ++i)
        if (m_entries[m_order[i]].id == id)
            return i;
    return -1;
}
PlaybackTracks SongCatalogModel::tracks(bool selectedOnly) const
{
    PlaybackTracks result;
    if (selectedOnly) {
        for (const auto &e : m_entries)
            if (e.playable && e.trackPlayable && m_selected.contains(e.id))
                result.append(PlaybackTrack::fromDomain(e.domainSong));
    } else
        for (int index : m_order) {
            const auto &e = m_entries[index];
            if (e.playable && e.trackPlayable)
                result.append(PlaybackTrack::fromDomain(e.domainSong));
        }
    return result;
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
