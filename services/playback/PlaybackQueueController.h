#pragma once
#include "PlaybackTrack.h"
#include <QJsonObject>
#include <QStringList>

namespace EchoMusic
{
class PlaybackService;
/// Playback-owned domain adapter. The facade remains the only Qt state owner;
/// this adapter creates no parallel queue, clock, session or native runtime.
class PlaybackQueueController final
{
public:
    explicit PlaybackQueueController(PlaybackService &owner) : m_playback(owner) {}
    void playTracks(const PlaybackTracks &tracks, int index, const QString &title,
                    const QString &sourceKey);
    void playFromSelection(const PlaybackTracks &tracks, int index, const QString &title,
                           const QString &sourceKey, bool selectedSong);
    void queueAndPlayTrack(const PlaybackTrack &track, const QString &title,
                           const QString &sourceKey);
    void playInCurrentQueue(const PlaybackTrack &track);
    void appendTracks(const PlaybackTracks &tracks, bool next);
    void enqueueTracks(const PlaybackTracks &tracks, bool cut);
    void appendTracksImpl(const PlaybackTracks &tracks, bool next, bool sequential);
    void playIndex(int index);
    void requestHistory();
    void removeHistory(const QStringList &keys);
    void clearHistory();
    void playViewedIndex(int index);
    void resumeViewedQueue();
    void removeViewedIndex(int index);
    void persist();

private:
    PlaybackService &m_playback;
};
} // namespace EchoMusic
