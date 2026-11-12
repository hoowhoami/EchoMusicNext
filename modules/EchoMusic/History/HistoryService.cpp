#include "HistoryService.h"
#include "PlaybackService.h"
#include <QDate>
#include <QDateTime>
#include <QLocale>
#include <QQmlEngine>
#include <QSet>
#include <algorithm>

namespace EchoMusic
{
namespace
{
QJsonObject historyTrackSong(const QJsonObject &track)
{
    auto metadata = track["metadata"].toObject();
    QJsonArray artists;
    if (metadata["artist_refs"].isArray()) {
        artists = metadata["artist_refs"].toArray();
    } else if (!track["artist"].toString().isEmpty()) {
        artists.append(QJsonObject{{"name", track["artist"]}});
    }
    return {{"id", track["id"]},
            {"hash", track["hash"]},
            {"local_path", track["local_path"]},
            {"title", track["title"]},
            {"artists", artists},
            {"album_id", track["album_id"]},
            {"album_title", track["album"]},
            {"duration_ms", track["duration_ms"]},
            {"cover_url", track["cover"]},
            {"privilege", track["privilege"]},
            {"metadata", metadata}};
}
QJsonObject historySong(const QJsonObject &entry)
{
    auto song = historyTrackSong(entry["track"].toObject());
    auto metadata = song["metadata"].toObject();
    for (const auto &key : {"played_at", "play_count", "listened_ms"})
        metadata[key] = entry[key];
    metadata["history_key"] = entry["key"];
    song["metadata"] = metadata;
    return song;
}
/// Unmatched uploads were stored under the provider placeholder id "0" before
/// the canonical cloud:<hash> identity existed. Presentations address rows by
/// this id, so every loaded history row must use the same canonical id as the
/// cloud/playlist surfaces or facts would fan out across unrelated uploads.
QJsonObject normalizeHistoryEntry(QJsonObject entry)
{
    auto track = entry["track"].toObject();
    const auto id = track["id"].toString();
    bool numeric = false;
    const auto catalogId = id.toULongLong(&numeric);
    if ((numeric && catalogId) || !track["local_path"].toString().isEmpty())
        return entry;
    const auto hash = track["hash"].toString().trimmed().toLower();
    if (hash.isEmpty())
        return entry;
    const auto canonical = QStringLiteral("cloud:%1").arg(hash);
    if (track["id"].toString() == canonical)
        return entry;
    track["id"] = canonical;
    entry["track"] = track;
    return entry;
}
QString compact(qint64 value)
{
    return QLocale(QLocale::Chinese, QLocale::China).toString(value);
}
QString listenDuration(qint64 seconds)
{
    if (seconds <= 0)
        return QObject::tr("0 分钟");
    const auto minutes = std::max<qint64>(1, qRound64(seconds / 60.0));
    if (minutes < 60)
        return QObject::tr("%1 分钟").arg(minutes);
    const double hours = seconds / 3600.0;
    if (hours < 100)
        return hours >= 10 ? QObject::tr("%1 小时").arg(QString::number(hours, 'f', 0))
                           : QObject::tr("%1 小时").arg(QString::number(hours, 'f', 1));
    const double days = hours / 24.0;
    return days >= 10 ? QObject::tr("%1 天").arg(QString::number(days, 'f', 0))
                      : QObject::tr("%1 天").arg(QString::number(days, 'f', 1));
}
QString playDate(const QDateTime &at)
{
    return at.isValid() ? QObject::tr("%1月%2日 %3:%4")
                              .arg(at.date().month())
                              .arg(at.date().day())
                              .arg(at.time().hour(), 2, 10, QChar('0'))
                              .arg(at.time().minute(), 2, 10, QChar('0'))
                        : QObject::tr("未知");
}
QStringList artistNames(const QJsonObject &track)
{
    QStringList names;
    const auto refs = track["metadata"].toObject()["artist_refs"].toArray();
    for (const auto &value : refs) {
        const auto name = value.toObject()["name"].toString().trimmed();
        if (!name.isEmpty())
            names.append(name);
    }
    if (!names.isEmpty())
        return names;
    auto text = track["artist"].toString();
    text.replace(QRegularExpression(QStringLiteral("\\s*(?:,|，|、|/|&| feat\\.?| ft\\.?)\\s*"),
                                    QRegularExpression::CaseInsensitiveOption),
                 QStringLiteral("\n"));
    for (const auto &name : text.split('\n', Qt::SkipEmptyParts))
        if (!name.trimmed().isEmpty())
            names.append(name.trimmed());
    if (names.isEmpty())
        names.append(QObject::tr("未知歌手"));
    return names;
}
QVariantMap buildStats(const QJsonArray &entries)
{
    struct Artist
    {
        QString name;
        qint64 plays = 0, songs = 0;
    };
    struct Album
    {
        QString name, cover;
        qint64 coverPlays = -1;
        qint64 plays = 0, songs = 0;
    };
    struct Song
    {
        QString key, id, title, artist, cover;
        qint64 plays = 0;
        QDateTime at;
    };
    qint64 totalPlays = 0, listenedMs = 0, replayed = 0;
    QDateTime latest, oldest;
    QMap<QString, Artist> artists;
    QMap<QString, Album> albums;
    QList<Song> songs;
    QList<QDateTime> playedAts;
    for (const auto &value : entries) {
        const auto entry = value.toObject();
        const auto track = entry["track"].toObject();
        const auto plays = std::max<qint64>(1, entry["play_count"].toInteger());
        const auto timestamp = entry["played_at"].toInteger();
        const auto at = timestamp > 0 ? QDateTime::fromMSecsSinceEpoch(timestamp) : QDateTime{};
        totalPlays += plays;
        // This is the settled listening ledger, not duration multiplied by plays.
        listenedMs += std::max<qint64>(0, entry["listened_ms"].toInteger());
        if (plays > 1)
            ++replayed;
        if (at.isValid()) {
            playedAts.append(at);
            if (!latest.isValid() || at > latest)
                latest = at;
            if (!oldest.isValid() || at < oldest)
                oldest = at;
        }
        for (const auto &name : artistNames(track)) {
            auto &artist = artists[name];
            artist.name = name;
            artist.plays += plays;
            artist.songs += 1;
        }
        const auto albumName = !track["album"].toString().trimmed().isEmpty()
                                   ? track["album"].toString().trimmed()
                                   : QObject::tr("未知专辑");
        auto &album = albums[albumName];
        album.name = albumName;
        album.plays += plays;
        album.songs += 1;
        const auto cover = track["cover"].toString();
        if (!cover.isEmpty() && plays > album.coverPlays) {
            album.cover = cover;
            album.coverPlays = plays;
        }
        songs.append(Song{entry["key"].toString(), track["id"].toString(),
                          track["title"].toString(), track["artist"].toString(),
                          track["cover"].toString(), plays, at});
    }
    const auto totalSongs = entries.size();
    const auto replayPercent = totalSongs > 0 ? qRound(replayed * 100.0 / totalSongs) : 0;
    QVariantList cards{QVariantMap{{"label", QObject::tr("听过的歌曲")},
                                   {"value", compact(totalSongs)},
                                   {"unit", QObject::tr("首")},
                                   {"caption", QObject::tr("每首歌，都是一次相遇")},
                                   {"icon", "music"}},
                       QVariantMap{{"label", QObject::tr("已记录时长")},
                                   {"value", listenDuration(listenedMs / 1000)},
                                   {"unit", ""},
                                   {"caption", QObject::tr("来自已结算的收听记录")},
                                   {"icon", "clock"}},
                       QVariantMap{{"label", QObject::tr("常听歌手")},
                                   {"value", compact(artists.size())},
                                   {"unit", QObject::tr("位")},
                                   {"caption", QObject::tr("不同的声音，同样的喜欢")},
                                   {"icon", "microphone"}},
                       QVariantMap{{"label", QObject::tr("复听歌曲占比")},
                                   {"value", QString::number(replayPercent)},
                                   {"unit", "%"},
                                   {"caption", QObject::tr("%1 首歌曲不止听过一次").arg(replayed)},
                                   {"icon", "repeat"}}};
    QList<Artist> artistList;
    for (const auto &artist : std::as_const(artists))
        artistList.append(artist);
    std::sort(artistList.begin(), artistList.end(), [](const auto &a, const auto &b) {
        return a.plays != b.plays   ? a.plays > b.plays
               : a.songs != b.songs ? a.songs > b.songs
                                    : a.name < b.name;
    });
    QVariantList topArtists;
    const auto maxArtistPlays = artistList.isEmpty() ? 1 : artistList.first().plays;
    for (int i = 0; i < artistList.size() && i < 12; ++i)
        topArtists.append(
            QVariantMap{{"name", artistList[i].name},
                        {"songs", artistList[i].songs},
                        {"plays", artistList[i].plays},
                        {"ratio", double(artistList[i].plays) / double(maxArtistPlays)}});
    std::sort(songs.begin(), songs.end(), [](const auto &a, const auto &b) {
        return a.plays != b.plays ? a.plays > b.plays : a.at > b.at;
    });
    QList<Album> albumList;
    for (const auto &album : std::as_const(albums))
        albumList.append(album);
    std::sort(albumList.begin(), albumList.end(), [](const auto &a, const auto &b) {
        return a.plays != b.plays   ? a.plays > b.plays
               : a.songs != b.songs ? a.songs > b.songs
                                    : a.name < b.name;
    });
    const auto topAlbum = albumList.isEmpty() ? QVariantMap{}
                                              : QVariantMap{{"name", albumList.first().name},
                                                            {"cover", albumList.first().cover},
                                                            {"plays", albumList.first().plays},
                                                            {"songs", albumList.first().songs}};
    QVariantList topSongs;
    for (int i = 0; i < songs.size() && i < 6; ++i)
        topSongs.append(QVariantMap{{"key", songs[i].key},
                                    {"id", songs[i].id},
                                    {"title", songs[i].title},
                                    {"artist", songs[i].artist},
                                    {"cover", songs[i].cover},
                                    {"plays", songs[i].plays},
                                    {"lastPlayedLabel", playDate(songs[i].at)}});
    // Only a track's latest date/hour is retained by the repository. These
    // bounded views show that footprint, never a fabricated event timeline.
    QHash<QDate, int> dateCounts;
    QList<int> hourCounts(24, 0);
    for (const auto &at : std::as_const(playedAts)) {
        ++dateCounts[at.date()];
        ++hourCounts[at.time().hour()];
    }
    const auto today = QDate::currentDate();
    const auto calendarEnd = today.addDays(7 - today.dayOfWeek());
    const auto calendarStart = calendarEnd.addDays(-34);
    int calendarMax = 1, calendarSongs = 0, maxHour = 1, peakHour = -1;
    for (int i = 0; i < 35; ++i)
        calendarMax = std::max(calendarMax, dateCounts.value(calendarStart.addDays(i)));
    for (int i = 0; i < 24; ++i) {
        if (hourCounts[i] > (peakHour < 0 ? 0 : hourCounts[peakHour]))
            peakHour = i;
        maxHour = std::max(maxHour, hourCounts[i]);
    }
    QVariantList calendar, hours;
    for (int i = 0; i < 35; ++i) {
        const auto date = calendarStart.addDays(i);
        const auto count = dateCounts.value(date);
        calendarSongs += count;
        calendar.append(QVariantMap{{"day", date.day()},
                                    {"date", date.toString("yyyy-MM-dd")},
                                    {"count", count},
                                    {"ratio", double(count) / calendarMax},
                                    {"today", date == today},
                                    {"future", date > today}});
    }
    for (int i = 0; i < 24; ++i)
        hours.append(QVariantMap{
            {"hour", i}, {"count", hourCounts[i]}, {"ratio", double(hourCounts[i]) / maxHour}});
    return {
        {"totalSongs", totalSongs},
        {"totalPlays", totalPlays},
        {"totalPlaysLabel", compact(totalPlays)},
        {"calendarDays", calendar},
        {"calendarSongs", calendarSongs},
        {"calendarLabel",
         calendarStart.toString("MM.dd") + QStringLiteral(" — ") + today.toString("MM.dd")},
        {"hourlyFootprint", hours},
        {"peakHourLabel", peakHour < 0 ? QStringLiteral("—")
                                       : QStringLiteral("%1:00").arg(peakHour, 2, 10, QChar('0'))},
        {"listenedMs", listenedMs},
        {"datedSongs", playedAts.size()},
        {"replayPercent", replayPercent},
        {"dateRangeLabel",
         oldest.isValid()
             ? oldest.toString("yyyy.MM.dd") + QStringLiteral(" — ") + latest.toString("yyyy.MM.dd")
             : QString{}},
        {"latestPlayedLabel", playDate(latest)},
        {"statCards", cards},
        {"topArtists", topArtists},
        {"topAlbum", topAlbum},
        {"topSongs", topSongs}};
}
} // namespace
HistoryService::HistoryService(ApiClient &api, PlaybackService &playback,
                               SongMetadataService &metadata, RouteController &router, bool network,
                               QObject *parent)
    : QObject(parent), m_api(api), m_playback(playback), m_metadata(metadata),
      m_actions(m_songs, playback, this), m_statsShare(*this, playback, this), m_network(network)
{
    m_songs.attachMetadata(&metadata);
    m_songs.setMetadataActive(false);
    QQmlEngine::setObjectOwnership(this, QQmlEngine::CppOwnership);
    connect(&api, &ApiClient::sessionChanged, &m_statsShare, &HistoryStatsShare::cancel);
    m_actions.setQueueContext(tr("播放历史"), "queue:history");
    connect(&playback, &PlaybackService::historyLoaded, this, &HistoryService::accept);
    connect(&playback, &PlaybackService::historyFailed, this, &HistoryService::fail);
    connect(&metadata, &SongMetadataService::completed, this, &HistoryService::acceptMetadata);
    connect(&playback, &PlaybackService::trackFactsApplied, this,
            &HistoryService::applyPlaybackFacts);
    connect(&api, &ApiClient::sessionChanged, this, &HistoryService::cancelMetadata);
    connect(&api, &ApiClient::closed, this, &HistoryService::shutdown);
    connect(&router, &RouteController::pageActivated, this, [this](RouteContext *context) {
        if (m_closed)
            return;
        const bool active = context && context->name() == "history";
        m_songs.setMetadataActive(active && m_network);
        if (!active || context->query().value("tab").toString() != "stats")
            m_statsShare.cancel();
        if (m_active == active)
            return;
        m_active = active;
        if (active)
            completeSongs();
        else
            cancelMetadata();
    });
}
HistoryService::~HistoryService()
{
    shutdown();
}
void HistoryService::refresh()
{
    if (m_closed || m_loading)
        return;
    cancelMetadata();
    m_loading = true;
    m_error.clear();
    m_playback.requestHistory();
    emit changed();
}
void HistoryService::remove(const QString &id)
{
    removeMany({id});
}
void HistoryService::removeMany(const QStringList &ids)
{
    if (m_closed)
        return;
    QStringList keys;
    QSet<QString> seen;
    for (const auto &id : ids) {
        const auto track = m_songs.track(id);
        if (!track)
            continue;
        const auto key = track->catalogMetadata.value("history_key").toString();
        if (!key.isEmpty() && !seen.contains(key)) {
            seen.insert(key);
            keys.append(key);
        }
    }
    if (keys.isEmpty())
        return;
    cancelMetadata();
    m_playback.removeHistory(keys);
    emit notice(keys.size() == 1 ? tr("已从播放历史移除")
                                 : tr("已从播放历史移除 %1 首").arg(keys.size()));
}
void HistoryService::clear()
{
    if (m_closed)
        return;
    cancelMetadata();
    m_playback.clearHistory();
    emit notice(tr("播放历史已清空"));
}
void HistoryService::accept(const QJsonArray &entries)
{
    if (m_closed)
        return;
    cancelMetadata();
    const auto previous = m_entries.isEmpty() ? QJsonObject{} : m_entries.first().toObject();
    const auto latest = entries.isEmpty() ? QJsonObject{} : entries.first().toObject();
    const bool promoted = !previous.isEmpty() && !latest.isEmpty() &&
                          (latest["played_at"].toInteger() > previous["played_at"].toInteger() ||
                           (latest["key"] == previous["key"] &&
                            latest["play_count"].toInteger() > previous["play_count"].toInteger()));
    m_entries = entries;
    QJsonArray songs;
    for (qsizetype i = 0; i < m_entries.size(); ++i) {
        const auto entry = normalizeHistoryEntry(m_entries[i].toObject());
        m_entries[i] = entry;
        songs.append(historySong(entry));
    }
    m_stats = buildStats(m_entries);
    m_songs.replaceSongs(songs, true);
    m_loading = false;
    m_error.clear();
    // A page load may arrive after its facts: replay session corrections
    // instead of letting the repository snapshot reset them.
    reapplyPlaybackFacts();
    completeSongs();
    emit changed();
    if (promoted)
        emit recordPromoted();
}
void HistoryService::completeSongs()
{
    if (m_closed || !m_active || !m_network || !m_metadataRequests.isEmpty())
        return;
    // Local/offline history stays usable; only real remote IDs are accepted by
    // the shared completion service, with no extra request per rendered row.
    const auto ids = m_songs.incompletePresentationIds();
    if (const auto ticket = m_metadata.request(ids)) {
        m_metadataSession = m_api.sessionRevision();
        m_metadataRequests.insert(ticket, m_metadataGeneration);
    }
}
void HistoryService::acceptMetadata(quint64 ticket, const QJsonArray &values)
{
    if (!m_metadataRequests.contains(ticket))
        return;
    const auto generation = m_metadataRequests.take(ticket);
    if (m_closed || !m_active || generation != m_metadataGeneration ||
        m_metadataSession != m_api.sessionRevision())
        return;
    QHash<QString, QJsonObject> presentations;
    for (const auto &value : values) {
        const auto presentation = value.toObject();
        presentations.insert(presentation["id"].toString(), presentation);
    }
    bool changed = false;
    for (qsizetype i = 0; i < m_entries.size(); ++i) {
        auto entry = m_entries[i].toObject();
        auto track = entry["track"].toObject();
        const auto found = presentations.constFind(track["id"].toString());
        if (found == presentations.cend())
            continue;
        const auto original = historyTrackSong(track);
        const auto completed = SongMetadataService::completeSong(original, *found);
        if (completed == original)
            continue;
        const auto presentation = PlaybackTrack::fromDomain(completed).toDomain();
        for (const auto &key : {"title", "artist", "album_id", "album", "cover", "metadata"})
            track[key] = presentation[key];
        entry["track"] = track;
        m_entries[i] = entry;
        changed = true;
    }
    m_songs.applyPresentations(values);
    if (changed) {
        // Presentation-only snapshot: never call record/remove or write SQL,
        // so completion cannot count a play, change dates or promote a row.
        m_stats = buildStats(m_entries);
        emit this->changed();
    }
}
void HistoryService::cancelMetadata()
{
    ++m_metadataGeneration;
    const auto tickets = m_metadataRequests.keys();
    m_metadataRequests.clear();
    for (const auto ticket : tickets)
        m_metadata.cancel(ticket);
}
bool HistoryService::applyFactsToRow(const QJsonObject &facts, QJsonObject &track,
                                     QJsonArray &projected)
{
    auto presentation = QJsonObject{{"id", track["id"]},
                                    {"title", facts.value("title")},
                                    {"album_title", facts.value("album")},
                                    {"duration_ms", facts.value("duration_ms")}};
    const auto cover = facts.value("cover_url").toString().trimmed();
    if (!cover.isEmpty())
        presentation["cover_url"] = cover;
    const auto refs = SongMetadataService::artistRefs(facts.value("artist").toString());
    if (!refs.isEmpty())
        presentation["artists"] = refs;
    // Keep the source snapshot aligned for rebuilds and stats.
    const auto original = historyTrackSong(track);
    auto completed = SongMetadataService::completeSong(original, presentation);
    if (completed == original)
        return false;
    const auto song = PlaybackTrack::fromDomain(completed).toDomain();
    for (const auto &key : {"title", "artist", "album", "cover", "duration_ms", "metadata"})
        if (song.value(key) != QJsonValue::Undefined)
            track[key] = song.value(key);
    projected.append(presentation);
    return true;
}
void HistoryService::applyPlaybackFacts(const QJsonObject &facts)
{
    if (m_closed)
        return;
    const auto hash = facts.value("hash").toString().trimmed();
    if (hash.isEmpty())
        return;
    // Corrections live in the playback service's session memory; this only
    // refreshes the already-loaded rows (accept() replays via lookup).
    QJsonArray projected;
    bool touched = false;
    for (qsizetype i = 0; i < m_entries.size(); ++i) {
        auto entry = m_entries[i].toObject();
        auto track = entry["track"].toObject();
        if (hash.compare(track["hash"].toString(), Qt::CaseInsensitive) != 0)
            continue;
        if (!applyFactsToRow(facts, track, projected))
            continue;
        entry["track"] = track;
        m_entries[i] = entry;
        touched = true;
    }
    if (projected.isEmpty())
        return;
    m_songs.applyPresentations(projected);
    if (touched) {
        m_stats = buildStats(m_entries);
        emit this->changed();
    }
}
void HistoryService::reapplyPlaybackFacts()
{
    if (m_closed || m_entries.isEmpty())
        return;
    QJsonArray projected;
    bool touched = false;
    for (qsizetype i = 0; i < m_entries.size(); ++i) {
        auto entry = m_entries[i].toObject();
        auto track = entry["track"].toObject();
        const auto found = m_playback.playbackFacts(track["hash"].toString());
        if (found.isEmpty())
            continue;
        if (!applyFactsToRow(found, track, projected))
            continue;
        entry["track"] = track;
        m_entries[i] = entry;
        touched = true;
    }
    if (projected.isEmpty())
        return;
    m_songs.applyPresentations(projected);
    if (touched) {
        m_stats = buildStats(m_entries);
        emit this->changed();
    }
}
void HistoryService::fail(const QString &message)
{
    if (m_closed)
        return;
    m_loading = false;
    m_error = message;
    emit changed();
}
void HistoryService::shutdown()
{
    m_songs.setMetadataActive(false);
    if (m_closed)
        return;
    m_closed = true;
    m_statsShare.cancel();
    cancelMetadata();
    disconnect(&m_playback, nullptr, this, nullptr);
    emit changed();
}
} // namespace EchoMusic

namespace EchoMusic
{
void HistoryService::registerRoutes(RouteController &router)
{
    router.registerPage({"history",
                         "/main/history",
                         QUrl("qrc:/qt/qml/EchoMusic/History/HistoryPage.qml"),
                         {"tab"},
                         true,
                         true,
                         false,
                         {{"service", QVariant::fromValue(this)}}});
}
} // namespace EchoMusic
