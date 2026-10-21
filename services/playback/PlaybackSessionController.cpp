#include "PlaybackSessionController.h"
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
void PlaybackSessionController::invalidate()
{
    m_playback.m_climax.clear();
    m_playback.m_progress.reset();
    m_playback.publishProgressStatus();
    m_playback.m_resolvedQuality.clear();
    m_playback.m_failure->cancel();
    m_playback.m_failure->sourceAdopted();
    m_playback.m_recovery->cancel();
    m_playback.m_transition->clear();
    ++m_playback.m_sequence;
    m_playback.m_audioSequence = ++m_playback.m_audioSerial;
    ++m_playback.m_sourceRevision;
    m_playback.m_engine.cancelSourceReplacement();
    m_playback.m_artwork.cancel();
    m_playback.m_embeddedCover.clear();
    m_playback.m_lyricSource.clear();
    m_playback.m_source.cancel();
    m_playback.m_urls.clear();
    m_playback.m_urlIndex = 0;
    m_playback.cancelTrackProbe();
    if (const auto embedded = std::exchange(m_playback.m_embeddedRequest, 0))
        m_playback.m_native.cancel(embedded);
    m_playback.m_probedSource.clear();
    m_playback.m_audioTrack = 0;
}

void PlaybackSessionController::ensureLyrics()
{
    if (m_playback.m_closing || !m_playback.hasTrack() || m_playback.m_lyricState != "idle" ||
        m_playback.m_lyrics.rowCount())
        return;
    // load() emits changed while resetting its previous source; leave idle first
    // so presentation observers cannot start a second request reentrantly.
    m_playback.m_lyricState = "loading";
    requestLyrics();
}

void PlaybackSessionController::requestLyrics()
{
    m_playback.m_lyricSource.load(m_playback.m_track);
    m_playback.m_lyricSource.setDuration(m_playback.m_duration);
}

void PlaybackSessionController::requestEmbeddedMetadata()
{
    if (m_playback.m_embeddedRequest)
        m_playback.m_native.cancel(m_playback.m_embeddedRequest);
    m_playback.m_embeddedRequest = m_playback.m_native.submit(
        NativeBridge::EmbeddedMetadata,
        QJsonObject{{"seq", qint64(m_playback.m_audioSequence)},
                    {"cover_cache_dir", m_playback.m_embeddedDirectory.path()}},
        m_playback.m_sequence);
}

void PlaybackSessionController::shutdown()
{
    if (m_playback.m_listenTogether)
        m_playback.m_listenTogether->shutdown();
    m_playback.m_personalFm->shutdown();
    m_playback.m_discoverQueue->shutdown();
    if (std::exchange(m_playback.m_closing, true))
        return;
    m_playback.m_sleepInhibitor.setRequired(false);
    m_playback.m_recovery->cancel();
    finishHistory();
    m_playback.m_listening.shutdown();
    m_playback.persist();
    m_playback.m_saveTimer.stop();
    m_playback.m_spectrum.stop();
    m_playback.m_mediaTimer.stop();
    invalidate();
    m_playback.m_sleepTimer.cancel();
    m_playback.m_effectCatalog.shutdown();
    m_playback.m_audioControls.shutdownImports();
    // The selected remote renderer must acknowledge Stop before the native
    // actor starts cancellation/join. Audio/SQL owners can drain in parallel.
    m_playback.m_cast->shutdown([guard = QPointer<PlaybackService>(&m_playback)] {
        if (guard)
            guard->m_native.shutdown();
    });
    if (m_playback.m_repository)
        m_playback.m_repository->shutdown();
    m_playback.m_engine.shutdown();
}

void PlaybackSessionController::finishHistory()
{
    m_playback.m_listening.flush();
    if (m_playback.m_repository && m_playback.m_listenedMs > 0)
        m_playback.m_repository->record(m_playback.m_account, m_playback.m_track,
                                        m_playback.m_listenedMs);
    m_playback.m_listenedMs = 0;
    m_playback.m_listenTick.restart();
}
} // namespace EchoMusic
