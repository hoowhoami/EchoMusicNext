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
        QString name;
        qint64 plays = 0, songs = 0;
    };
    struct Song
    {
        QString key, title, artist, cover;
        qint64 plays = 0;
        QDateTime at;
    };
    qint64 totalPlays = 0, totalSeconds = 0, replayed = 0;
    QSet<QDate> activeDays;
    QDateTime latest, oldest;
    QMap<QString, Artist> artists;
    QMap<QString, Album> albums;
    QList<Song> songs;
    QList<QDateTime> playedAts;
    for (const auto &value : entries) {
        const auto entry = value.toObject();
        const auto track = entry["track"].toObject();
        const auto plays = std::max<qint64>(1, entry["play_count"].toInteger());
        const auto at = QDateTime::fromMSecsSinceEpoch(entry["played_at"].toInteger());
        totalPlays += plays;
        const auto duration = track["duration_ms"].toDouble();
        totalSeconds += qint64(duration / 1000.0) * plays;
        if (plays > 1)
            ++replayed;
        if (at.isValid()) {
            activeDays.insert(at.date());
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
        songs.append(Song{entry["key"].toString(), track["title"].toString(),
                          track["artist"].toString(), track["cover"].toString(), plays, at});
    }
    const auto totalSongs = entries.size();
    const auto spanDays = oldest.isValid() ? oldest.date().daysTo(latest.date()) + 1 : 0;
    QVariantList cards{
        QVariantMap{{"label", QObject::tr("累计播放")},
                    {"value", compact(totalPlays)},
                    {"caption", QObject::tr("%1 首本地历史").arg(totalSongs)},
                    {"icon", "play"},
                    {"tone", "primary"}},
        QVariantMap{{"label", QObject::tr("估算时长")},
                    {"value", listenDuration(totalSeconds)},
                    {"caption", QObject::tr("按歌曲时长与次数估算")},
                    {"icon", "clock"},
                    {"tone", "cyan"}},
        QVariantMap{{"label", QObject::tr("活跃天数")},
                    {"value", QObject::tr("%1 天").arg(activeDays.size())},
                    {"caption", spanDays > 0 ? QObject::tr("覆盖最近 %1 天").arg(spanDays)
                                             : QObject::tr("暂无日期记录")},
                    {"icon", "heartbeat"},
                    {"tone", "emerald"}},
        QVariantMap{{"label", QObject::tr("复听占比")},
                    {"value", totalSongs > 0
                                  ? QObject::tr("%1%").arg(qRound(replayed * 100.0 / totalSongs))
                                  : QStringLiteral("0%")},
                    {"caption", QObject::tr("%1 首听过多次").arg(replayed)},
                    {"icon", "star"},
                    {"tone", "amber"}}};
    const QStringList weekdays{"一", "二", "三", "四", "五", "六", "日"};
    QVariantList recent;
    qint64 maxDay = 1;
    QList<qint64> dayValues;
    const auto today = QDate::currentDate();
    for (int i = 0; i < 7; ++i) {
        const auto date = today.addDays(i - 6);
        const auto start = QDateTime(date, QTime(0, 0));
        const auto end = start.addDays(1);
        qint64 count = 0;
        for (const auto &at : std::as_const(playedAts))
            if (at >= start && at < end)
                ++count;
        dayValues.append(count);
        maxDay = std::max(maxDay, count);
    }
    for (int i = 0; i < 7; ++i) {
        const auto date = today.addDays(i - 6);
        recent.append(
            QVariantMap{{"label", i == 6 ? QObject::tr("今天")
                                         : QObject::tr("周%1").arg(weekdays[date.dayOfWeek() - 1])},
                        {"dateLabel", QStringLiteral("%1/%2").arg(date.month()).arg(date.day())},
                        {"value", dayValues[i]},
                        {"ratio", double(dayValues[i]) / double(maxDay)}});
    }
    const QList<QPair<QString, QString>> bucketInfo{{QObject::tr("凌晨"), "00:00-05:59"},
                                                    {QObject::tr("上午"), "06:00-11:59"},
                                                    {QObject::tr("下午"), "12:00-17:59"},
                                                    {QObject::tr("夜晚"), "18:00-23:59"}};
    QList<qint64> bucketCounts(4, 0);
    for (const auto &at : std::as_const(playedAts))
        ++bucketCounts[std::clamp(at.time().hour() / 6, 0, 3)];
    qint64 maxBucket = 1;
    for (auto count : std::as_const(bucketCounts))
        maxBucket = std::max(maxBucket, count);
    QVariantList buckets;
    for (int i = 0; i < 4; ++i)
        buckets.append(QVariantMap{{"label", bucketInfo[i].first},
                                   {"hint", bucketInfo[i].second},
                                   {"value", bucketCounts[i]},
                                   {"ratio", double(bucketCounts[i]) / double(maxBucket)}});
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
    for (int i = 0; i < artistList.size() && i < 6; ++i)
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
                                                            {"plays", albumList.first().plays},
                                                            {"songs", albumList.first().songs}};
    QVariantList topSongs;
    for (int i = 0; i < songs.size() && i < 6; ++i)
        topSongs.append(QVariantMap{{"key", songs[i].key},
                                    {"title", songs[i].title},
                                    {"artist", songs[i].artist},
                                    {"cover", songs[i].cover},
                                    {"plays", songs[i].plays},
                                    {"lastPlayedLabel", playDate(songs[i].at)}});
    return {{"totalSongs", totalSongs},
            {"totalPlays", totalPlays},
            {"latestPlayedLabel", playDate(latest)},
            {"statCards", cards},
            {"recentDays", recent},
            {"timeBuckets", buckets},
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
    if (m_closed)
        return;
    const auto track = m_songs.track(id);
    if (!track)
        return;
    const auto key = track->catalogMetadata.value("history_key").toString();
    if (key.isEmpty())
        return;
    cancelMetadata();
    m_playback.removeHistory({key});
    emit notice(tr("已从播放历史移除"));
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
