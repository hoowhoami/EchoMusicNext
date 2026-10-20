#pragma once
#include "PlaybackTrack.h"
#include <QJsonObject>
#include <QStringList>

namespace EchoMusic
{
class PlaybackService;
/// Playback-owned domain adapter. The facade remains the only Qt state owner;
/// this adapter creates no parallel queue, clock, session or native runtime.
class PlaybackSessionController final
{
public:
    explicit PlaybackSessionController(PlaybackService &owner) : m_playback(owner) {}
    void invalidate();
    void ensureLyrics();
    void requestLyrics();
    void requestEmbeddedMetadata();
    void shutdown();
    void finishHistory();

private:
    PlaybackService &m_playback;
};
} // namespace EchoMusic
