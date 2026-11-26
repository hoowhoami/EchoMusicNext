#pragma once
#include <QJsonArray>

namespace EchoMusic
{
/// Bounded metadata candidates from the recommended complete MV. Video
/// resolution only orders inspection, never stands in for audio quality.
QJsonArray mvAudioCandidates(const QJsonArray &videos);
/// Audio-header facts rank within codec/profile/channel groups. Unknown facts
/// remain fallbacks; retain one best source per video encoding, at most three.
QJsonArray rankedMvAudioSources(const QJsonArray &candidates);
} // namespace EchoMusic
