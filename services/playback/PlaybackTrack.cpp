#include "PlaybackTrack.h"
#include "EmbeddedCoverPresentation.h"
#include "ArtistCoverPresentation.h"
#include <QRegularExpression>
#include <QJsonArray>
#include <QCryptographicHash>
namespace EchoMusic
{
QString PlaybackTrack::presentationIdentity() const
{
    return QString::fromLatin1(
        QCryptographicHash::hash(stableKey().toUtf8(), QCryptographicHash::Sha256).toHex());
}
qint64 effectAudioTrack(const QJsonArray &tracks, const QString &effect)
{
    qint64 selected = effect == "vocal" ? 2 : 1, first = 0;
    bool preferred = false;
    const QStringList keys =
        effect == "vocal"
            ? QStringList{"人声", "vocal", "voice", "vox", "stem"}
            : QStringList{"伴奏", "accompaniment", "instrumental", "karaoke", "backing", "music"};
    for (const auto &value : tracks) {
        const auto track = value.toObject();
        const auto id = track.value("id").toInteger();
        if (track.value("type").toString() != "audio" || id < 1 || id > 256)
            continue;
        if (!first)
            first = id;
        preferred |= id == selected;
        const auto title = (track.value("title").toString() + " " + track.value("lang").toString() +
                            " " + track.value("codec").toString())
                               .toLower();
        for (const auto &key : keys)
            if (title.contains(key))
                return id;
    }
    return preferred || !first ? selected : first;
}

QVariantList catalogArtistLinks(const QJsonArray &refs, const QString &fallbackArtist)
{
    QVariantList result;
    for (const auto &value : refs) {
        const auto ref = value.toObject();
        const auto id = ref.value("id").toVariant().toString().toULongLong();
        result.append(QVariantMap{{"name", ref.value("name").toString()},
                                  {"id", id ? QString::number(id) : QString()}});
    }
    if (refs.isEmpty()) {
        const auto names = fallbackArtist.split(QRegularExpression("[,/，]"), Qt::SkipEmptyParts);
        for (const auto &name : names)
            if (!name.trimmed().isEmpty())
                result.append(QVariantMap{{"name", name.trimmed()}, {"id", QString()}});
    }
    return result;
}

QVariantList PlaybackTrack::artistLinks() const
{
    return catalogArtistLinks(catalogMetadata.value("artist_refs").toArray(), artist);
}

PlaybackTrack PlaybackTrack::fromDomain(const QJsonObject &s)
{
    PlaybackTrack t;
    t.id = s.value("id").toString();
    t.catalogMetadata = s.value("metadata").toObject();
    for (const auto &key : {"audio_id", "hash_std"})
        if (s.contains(key))
            t.catalogMetadata.insert(key, s.value(key));
    if (s.contains("personal_fm"))
        t.catalogMetadata.insert("personal_fm", s.value("personal_fm"));
    if (s.contains("artists"))
        t.catalogMetadata.insert("artist_refs", presentationArtists(s));
    if (s.contains("album_title"))
        t.catalogMetadata.insert("album_title", s.value("album_title"));
    if (s.value("original").isBool())
        t.catalogMetadata.insert("original_performer", s.value("original").toBool());
    // Numeric convenience drives catalog badges only. Retain null as unknown
    // through the existing metadata JSON in queues/history and database storage.
    const auto privilege = s.value("privilege");
    t.catalogMetadata.insert("catalog_privilege",
                             privilege.isDouble() ? privilege : QJsonValue(QJsonValue::Null));
    if (s.contains("original_code"))
        t.catalogMetadata.insert("original_code", s.value("original_code"));
    t.catalogPrivilege = quint32(privilege.toInt());
    t.localPath = s.value("local_path").toString();
    t.hash = s.value("hash").toString();
    t.title = s.value("title").toString();
    t.album = s.value("album_title").toString();
    t.cover = s.value("cover_url").toString();
    t.albumId = s.value("album_id").toString().toULongLong();
    // SongSummary.id is MixSongID, the album-audio identity.
    t.songId = t.id.toULongLong();
    const auto cloud = t.catalogMetadata.value("cloud_source").toObject();
    if (!cloud.isEmpty())
        t.songId = quint64(cloud.value("song_id").toInteger());
    t.catalogMetadata.insert("catalog_duration_ms", s.value("duration_ms").isUndefined()
                                                        ? QJsonValue(QJsonValue::Null)
                                                        : s.value("duration_ms"));
    t.durationMs = quint64(s.value("duration_ms").toDouble());
    QStringList names;
    for (const auto &v : s.value("artists").toArray())
        names.append(v.toObject().value("name").toString());
    t.artist = names.join(" / ");
    return t;
}
bool PlaybackTrack::applyPresentation(const QJsonObject &song)
{
    if (song == toCatalog())
        return false;
    const auto before = *this;
    const auto presentation = fromDomain(song);
    title = presentation.title;
    artist = presentation.artist;
    album = presentation.album;
    albumId = presentation.albumId;
    cover = presentation.cover;
    if (presentation.durationMs)
        durationMs = presentation.durationMs;
    for (const auto &key : {"embedded_cover_url", "artist_cover_url"}) {
        const auto lease = song["metadata"].toObject().value(key).toString();
        if (lease.isEmpty())
            catalogMetadata.remove(key);
        else
            catalogMetadata[key] = lease;
    }
    if (song.contains("artists"))
        catalogMetadata["artist_refs"] = song["artists"];
    if (!song["metadata"].toObject().contains("filename_artist_name"))
        catalogMetadata.remove("filename_artist_name");
    if (durationMs && catalogMetadata.value("catalog_duration_ms").toDouble() <= 0)
        catalogMetadata["catalog_duration_ms"] = qint64(durationMs);
    return *this != before;
}
QJsonObject PlaybackTrack::toDomain() const
{
    return withoutArtistCover(
        withoutEmbeddedCover(
            QJsonObject{{"id", id},
                        {"hash", hash},
                        {"title", title},
                        {"artist", artist},
                        {"album", album},
                        {"cover", cover},
                        {"local_path", localPath},
                        {"album_id", QString::number(albumId)},
                        {"song_id", QString::number(songId)},
                        {"duration_ms", catalogMetadata.contains("catalog_duration_ms")
                                            ? catalogMetadata.value("catalog_duration_ms")
                                            : QJsonValue(qint64(durationMs))},
                        {"metadata", catalogMetadata},
                        {"privilege", catalogMetadata.value("catalog_privilege")}},
            "cover"),
        "cover");
}
QVariantMap PlaybackTrack::publicValue() const
{
    return {{"id", id},
            {"hash", hash},
            {"title", title},
            {"artist", artist},
            {"album", album},
            {"cover", cover},
            {"albumId", QString::number(albumId)},
            {"songId", QString::number(songId)},
            {"durationMs", double(durationMs)},
            {"sourceId", catalogMetadata.value("plugin_source").toString()},
            {"data", catalogMetadata.value("plugin_data").toObject().toVariantMap()}};
}
QJsonObject PlaybackTrack::toCatalog() const
{
    auto artists = catalogMetadata.value("artist_refs").toArray();
    if (artists.isEmpty() && !artist.isEmpty())
        artists.append(QJsonObject{{"name", artist}});
    return {{"id", id},
            {"hash", hash},
            {"local_path", localPath},
            {"title", title},
            {"artists", artists},
            {"album_title", album},
            {"cover_url", cover},
            {"album_id", QString::number(albumId)},
            {"duration_ms", catalogMetadata.contains("catalog_duration_ms")
                                ? catalogMetadata.value("catalog_duration_ms")
                                : QJsonValue(qint64(durationMs))},
            {"metadata", catalogMetadata},
            {"privilege", catalogMetadata.value("catalog_privilege")}};
}
} // namespace EchoMusic
