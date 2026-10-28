#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QUrl>

namespace EchoMusic
{
inline bool hasArtistCover(const QJsonObject &metadata, const QString &cover)
{
    const auto fallback = metadata.value("artist_cover_url").toString();
    return !fallback.isEmpty() && fallback == cover;
}
inline QJsonObject withoutArtistCover(QJsonObject song,
                                      const QString &coverField = QStringLiteral("cover_url"))
{
    auto metadata = song.value("metadata").toObject();
    if (hasArtistCover(metadata, song.value(coverField).toString()))
        song[coverField] = QString();
    metadata.remove("artist_cover_url");
    if (song.contains("metadata"))
        song["metadata"] = metadata;
    return song;
}
inline bool samePresentationArtist(const QJsonObject &a, const QJsonObject &b)
{
    const auto left = a["id"].toVariant().toULongLong();
    const auto right = b["id"].toVariant().toULongLong();
    if (left && right)
        return left == right;
    const auto name = a["name"].toString().trimmed();
    return !name.isEmpty() && !QString(name).remove('*').trimmed().isEmpty() &&
           name.compare(b["name"].toString().trimmed(), Qt::CaseInsensitive) == 0;
}
inline QString presentationImageUrl(const QString &value)
{
    const auto text = value.trimmed();
    const QUrl url(text);
    return url.isValid() && !url.host().isEmpty() &&
                   (url.scheme() == "https" || url.scheme() == "http")
               ? text
               : QString();
}
inline QString artistAvatar(const QJsonObject &artist)
{
    return presentationImageUrl(artist["avatar_url"].toString());
}
/// Keep authoritative song order/identity; add only an explicitly matching
/// provider avatar. Different positive IDs never merge on a shared name.
inline QJsonArray mergeArtistAvatars(QJsonArray artists, const QJsonArray &details,
                                     bool replace = false)
{
    for (qsizetype i = 0; i < artists.size(); ++i) {
        auto artist = artists[i].toObject();
        if (!replace && !artistAvatar(artist).isEmpty())
            continue;
        for (const auto &value : details) {
            const auto detail = value.toObject();
            const auto avatar = artistAvatar(detail);
            if (!avatar.isEmpty() && samePresentationArtist(artist, detail)) {
                artist["avatar_url"] = avatar;
                artists[i] = artist;
                break;
            }
        }
    }
    return artists;
}
inline QJsonArray presentationArtists(const QJsonObject &song)
{
    const auto metadata = song["metadata"].toObject();
    auto artists = song["artists"].toArray();
    if (artists.isEmpty())
        artists = metadata["artist_refs"].toArray();
    artists = mergeArtistAvatars(artists, metadata["artists"].toArray());
    return mergeArtistAvatars(artists, metadata["artist_refs"].toArray());
}
} // namespace EchoMusic
