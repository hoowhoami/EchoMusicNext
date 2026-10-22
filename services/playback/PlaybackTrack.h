#pragma once
#include <QString>
#include <QJsonObject>
#include <QJsonArray>
#include <QMetaType>
#include <QVariantList>
#include <algorithm>

namespace EchoMusic
{
/// Select the same FFmpeg audio ordinal for live and preloaded sources.
qint64 effectAudioTrack(const QJsonArray &tracks, const QString &effect);
/// Shared artist projection for catalog rows and retained playback tracks.
QVariantList catalogArtistLinks(const QJsonArray &refs, const QString &fallbackArtist);
/// Host-owned catalog identity, not an upstream DTO or signed stream URL.
struct PlaybackTrack
{
    QString id, hash, title, artist, album, cover, localPath;
    quint64 albumId = 0, songId = 0, durationMs = 0;
    /// Catalog metadata survives queue and storage round trips without QML reparsing.
    QJsonObject catalogMetadata;
    quint32 catalogPrivilege = 0;
    bool operator==(const PlaybackTrack &) const = default;
    bool playable() const
    {
        return (!id.isEmpty() && !catalogMetadata.value("plugin_source").toString().isEmpty()) ||
               !localPath.isEmpty() ||
               (hash.size() == 32 && !id.isEmpty() &&
                std::all_of(hash.cbegin(), hash.cend(), [](QChar c) {
                    const auto n = c.unicode();
                    return (n >= '0' && n <= '9') || (n >= 'a' && n <= 'f') ||
                           (n >= 'A' && n <= 'F');
                }));
    }
    QString key() const
    {
        return localPath.isEmpty() ? id + "/" + hash : localPath;
    }
    /// Identity that survives view-id renames of one audio (legacy "0"
    /// snapshots vs "cloud:<hash>"): the local path, else the audio hash for
    /// unmatched uploads, else id + hash.
    QString stableKey() const
    {
        if (!localPath.isEmpty())
            return localPath;
        if (!songId && !hash.isEmpty())
            return QStringLiteral("unmatched:") + hash;
        return id + "/" + hash;
    }
    static PlaybackTrack fromDomain(const QJsonObject &song);
    QJsonObject toDomain() const;
    /// Typed catalog projection for a view following a live queue. The storage
    /// snapshot above has different presentation field names.
    QJsonObject toCatalog() const;
    QVariantMap publicValue() const;
    /// UI links from retained domain identities; text-only artists never acquire an ID.
    QVariantList artistLinks() const;
};
using PlaybackTracks = QList<PlaybackTrack>;
} // namespace EchoMusic
Q_DECLARE_METATYPE(EchoMusic::PlaybackTrack)
Q_DECLARE_METATYPE(EchoMusic::PlaybackTracks)
