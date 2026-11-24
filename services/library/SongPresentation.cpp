#include "SongPresentation.h"
#include "PlaybackTrack.h"
#include "SongMetadataService.h"
#include "SongRowFacts.h"
#include <QJsonArray>
namespace EchoMusic::SongPresentation
{
QJsonObject projection(const QJsonObject &song)
{
    auto result = SongMetadataService::observationInput(song);
    if (song.contains("privilege"))
        result.insert("privilege", song.value("privilege"));
    auto metadata = result.value("metadata").toObject();
    const auto original = song.value("metadata").toObject();
    // Recommendation context belongs to this source row, not the shared
    // metadata observation cache for the audio identity.
    for (const auto *key : {"history_key", "recommendation"})
        if (original.contains(key))
            metadata.insert(key, original.value(key));
    result.insert("metadata", metadata);
    return result;
}
QJsonObject value(const QJsonObject &song)
{
    const auto track = PlaybackTrack::fromDomain(song);
    return {{"song", projection(song)},
            {"badges", QJsonArray::fromVariantList(songPrivilegeTags(track))},
            {"quality", songQualityLabel(track)},
            {"quality_tags", QJsonArray::fromVariantList(songQualityTags(track))},
            {"playable", track.catalogPrivilege != 40 && !songCopyrightRestricted(track)},
            {"track_playable", track.playable()},
            {"original", track.catalogMetadata.value("originality_code").toInt() > 0},
            {"extra", ""}};
}
SongDatasetStore::Row row(const QJsonObject &song, qint64 ordinal, const QString &viewId)
{
    const auto track = PlaybackTrack::fromDomain(song);
    return {viewId.isEmpty() ? track.id : viewId,
            track.title,
            track.artist,
            track.album,
            track.durationMs,
            ordinal,
            song,
            value(song)};
}
SongDatasetStore::CachedRow reference(const QJsonObject &song,
                                      SongDatasetStore::Generation generation,
                                      SongDatasetStore::Reference reference)
{
    const auto track = PlaybackTrack::fromDomain(song);
    SongDatasetStore::Identity identity{
        track.id,         track.hash,
        track.localPath,  SongMetadataService::completionId(song),
        track.songId,     track.catalogPrivilege != 40 && !songCopyrightRestricted(track),
        track.playable(), SongMetadataService::needsCompletion(song)};
    return {std::move(generation), std::move(reference), std::move(identity)};
}
} // namespace EchoMusic::SongPresentation
