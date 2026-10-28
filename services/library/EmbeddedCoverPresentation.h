#pragma once
#include <QJsonObject>
#include <QString>
namespace EchoMusic
{
/// A cache URL is a revocable display lease, not durable song metadata.
inline bool hasEmbeddedCover(const QJsonObject &metadata, const QString &cover)
{
    const auto lease = metadata.value("embedded_cover_url").toString();
    return !lease.isEmpty() && lease == cover;
}
inline QJsonObject withoutEmbeddedCover(QJsonObject song,
                                        const QString &coverField = QStringLiteral("cover_url"))
{
    auto metadata = song.value("metadata").toObject();
    if (hasEmbeddedCover(metadata, song.value(coverField).toString()))
        song[coverField] = QString();
    metadata.remove("embedded_cover_url");
    if (song.contains("metadata"))
        song["metadata"] = metadata;
    return song;
}
} // namespace EchoMusic
