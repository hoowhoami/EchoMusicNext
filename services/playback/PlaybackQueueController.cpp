#include "PlaybackQueueController.h"
#include "PlaybackService.h"
#include "TrackTransitionController.h"
#include "PlaybackRecoveryController.h"
#include "PlaybackFailureController.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QCoreApplication>
#include <cmath>

namespace EchoMusic
{
void PlaybackQueueController::playTracks(const PlaybackTracks &tracks, int index,
                                         const QString &title, const QString &sourceKey)
{
    if (m_playback.m_closing || index < 0 || index >= tracks.size())
        return;
    // Replaying the same source keeps its queue identity/order. Browsing alone
    // never replaces the active queue; only this explicit play command does.
    m_playback.finishHistory();
    m_playback.m_sourceKey = sourceKey;
    m_playback.m_queueTitle = title;
    m_playback.m_catalog.activate(sourceKey, title, tracks, index);
    playIndex(m_playback.queue()->currentIndex());
}

void PlaybackQueueController::playFromSelection(const PlaybackTracks &tracks, int index,
                                                const QString &title, const QString &sourceKey,
                                                bool selectedSong)
{
    if (selectedSong && m_playback.m_preferences.queueMode() == "single" && index >= 0 &&
        index < tracks.size()) {
        queueAndPlayTrack(tracks[index], title, sourceKey);
    } else
        playTracks(tracks, index, title, sourceKey);
}

void PlaybackQueueController::queueAndPlayTrack(const PlaybackTrack &track, const QString &title,
                                                const QString &sourceKey)
{
    if (m_playback.m_closing || !track.playable())
        return;
    auto *existing = m_playback.m_catalog.modelForKey(sourceKey);
    auto merged = existing ? existing->tracks() : PlaybackTracks{};
    int target = -1;
    for (int i = 0; i < merged.size(); ++i)
        if ((!track.id.isEmpty() && merged[i].id == track.id) || merged[i].key() == track.key() ||
            (!track.hash.isEmpty() &&
             merged[i].hash.compare(track.hash, Qt::CaseInsensitive) == 0)) {
            target = i;
            break;
        }
    if (target < 0) {
        target = merged.size();
        merged.append(track);
    }
    const bool current =
        m_playback.hasTrack() && m_playback.m_track.stableKey() == track.stableKey();
    if (current) {
        // Adopt context without restarting or pausing current audio.
        // Row play/pause toggles use a separate command.
        m_playback.m_sourceKey = sourceKey;
        m_playback.m_queueTitle = title;
        m_playback.m_catalog.activate(sourceKey, title, merged, target);
        emit m_playback.changed();
        if (!m_playback.m_playing)
            m_playback.play();
    } else
        playTracks(merged, target, title, sourceKey);
}
void PlaybackQueueController::playInCurrentQueue(const PlaybackTrack &track)
{
    if (m_playback.m_closing || !track.playable())
        return;
    auto &catalog = m_playback.m_catalog;
    auto key = catalog.activeKey();
    // Algorithmic/room queues retain their own membership and lifetime. Use
    // the same local destination as explicit enqueue actions in those modes.
    if (m_playback.m_personalFm->active() || key == "queue:home-discover" ||
        key == "queue:listen-together")
        key = catalog.lastLocalKey();
    if (!catalog.modelForKey(key))
        key = "default";
    m_playback.m_listenTogether->localTrackRequested();
    queueAndPlayTrack(track, catalog.titleForKey(key), key);
}

void PlaybackQueueController::appendTracks(const PlaybackTracks &tracks, bool next)
{
    appendTracksImpl(tracks, next, false);
}

void PlaybackQueueController::enqueueTracks(const PlaybackTracks &tracks, bool cut)
{
    appendTracksImpl(tracks, true, !cut);
}

void PlaybackQueueController::appendTracksImpl(const PlaybackTracks &tracks, bool next,
                                               bool sequential)
{
    auto *target = m_playback.queue();
    if (m_playback.m_personalFm->active() || m_playback.m_sourceKey == "queue:home-discover" ||
        m_playback.m_sourceKey == "queue:listen-together") {
        target = m_playback.m_catalog.modelForKey(m_playback.m_catalog.lastLocalKey());
        if (!target)
            target = m_playback.m_catalog.modelForKey("default");
    }
    const auto before = target ? target->rowCount() : 0;
    if (target)
        target->append(tracks, next,
                       m_playback.m_track.id.isEmpty() ? m_playback.m_track.key()
                                                       : m_playback.m_track.id,
                       sequential);
    const auto message = target && !tracks.isEmpty() ? target->rowCount() > before
                                                           ? QObject::tr("已添加到队列")
                                                           : QObject::tr("歌曲已在队列中")
                                                     : QString();
    emit m_playback.changed();
    if (!message.isEmpty())
        emit m_playback.notice(message, 1);
}

void PlaybackQueueController::playIndex(int index)
{
    m_playback.m_listenTogether->localTrackRequested();
    m_playback.startIndex(index, false);
}

void PlaybackQueueController::requestHistory()
{
    if (m_playback.m_closing)
        return;
    if (m_playback.m_repository)
        m_playback.m_repository->loadHistory(m_playback.m_account, m_playback.m_revision);
    else
        emit m_playback.historyLoaded({});
}

void PlaybackQueueController::removeHistory(const QStringList &keys)
{
    if (!m_playback.m_closing && m_playback.m_repository)
        m_playback.m_repository->removeHistory(m_playback.m_account, keys);
}

void PlaybackQueueController::clearHistory()
{
    if (!m_playback.m_closing && m_playback.m_repository)
        m_playback.m_repository->clearHistory(m_playback.m_account);
}

void PlaybackQueueController::playViewedIndex(int index)
{
    if (m_playback.m_catalog.viewActive() && index == m_playback.queue()->currentIndex() &&
        m_playback.m_loaded) {
        m_playback.toggle();
        return;
    }
    m_playback.m_catalog.activateViewed();
    m_playback.m_sourceKey = m_playback.m_catalog.activeKey();
    m_playback.m_queueTitle = m_playback.m_catalog.activeTitle();
    playIndex(index);
}

void PlaybackQueueController::resumeViewedQueue()
{
    auto *model = m_playback.m_catalog.viewModel();
    if (model && model->rowCount())
        playViewedIndex(qMax(0, model->currentIndex()));
}

void PlaybackQueueController::removeViewedIndex(int index)
{
    auto *model = m_playback.m_catalog.viewModel();
    if (!model || !model->at(index))
        return;
    const bool active = model == m_playback.queue() &&
                        model->at(index)->stableKey() == m_playback.m_track.stableKey();
    const bool wasPlaying = m_playback.m_playing;
    model->remove(index);
    if (active && model->rowCount()) {
        playIndex(qMin(index, model->rowCount() - 1));
        m_playback.m_autoplay = wasPlaying;
    }
}

void PlaybackQueueController::persist()
{
    if (!m_playback.m_repository || m_playback.m_restoring ||
        (m_playback.m_queueRestorePending && m_playback.m_sequence == m_playback.m_restoreSequence))
        return;
    const auto states = m_playback.m_catalog.snapshot();
    m_playback.m_repository->save(PlaybackRepository::SharedQueueScope, states,
                                  m_playback.m_catalog.activeKey(),
                                  m_playback.m_catalog.lastLocalKey());
}
} // namespace EchoMusic
