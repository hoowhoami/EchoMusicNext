#pragma once
#include "PlaybackTrack.h"
#include <QJsonObject>
#include <QStringList>
#include <optional>

namespace EchoMusic
{
class PlaybackService;
/// Playback-owned domain adapter. The facade remains the only Qt state owner;
/// this adapter creates no parallel queue, clock, session or native runtime.
class PlaybackOutputController final
{
public:
    explicit PlaybackOutputController(PlaybackService &owner) : m_playback(owner) {}
    void onEvent(const QJsonObject &event);
    void mediaRouteChanged();
    void consumeOutputEvent(const QJsonObject &event);
    void publishMedia();
    void publishTimeline();
    void syncMediaState();

private:
    friend class PlaybackServiceTestAccess;
    bool mediaPlaying() const;
    // Publication cache only; playback intent remains owned by PlaybackService.
    std::optional<bool> m_publishedPlaying;
    PlaybackService &m_playback;
};
} // namespace EchoMusic
