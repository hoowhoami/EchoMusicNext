#pragma once
#include "PlaybackTrack.h"
#include <QVariantList>

namespace EchoMusic
{
/// Presentation facts shared by every song row backed by a catalog track.
/// Rights and resource labels never become playback authorization.
QVariantList songPrivilegeTags(const PlaybackTrack &track);
QString songQualityLabel(const PlaybackTrack &track);
QVariantList songQualityTags(const PlaybackTrack &track);
} // namespace EchoMusic
