#include "PlaybackOutputController.h"
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
void PlaybackOutputController::onEvent(const QJsonObject &event)
{
    if (m_playback.m_closing)
        return;
    m_playback.m_audioControls.handleEvent(event);
    const auto kind = event.value("event").toString();
    if (m_playback.m_cast->remoteMedia() && kind != "ready" && kind != "system-command" &&
        kind != "media-error")
        return;
    consumeOutputEvent(event);
}

void PlaybackOutputController::mediaRouteChanged()
{
    if (m_playback.m_closing)
        return;
    if (!m_playback.hasTrack()) {
        emit m_playback.changed();
        return;
    }
    m_playback.m_transition->clear();
    m_playback.m_recovery->cancel();
    m_playback.cancelTrackProbe();
    const bool autoplay = m_playback.m_playing || m_playback.m_loading && m_playback.m_autoplay;
    m_playback.m_resumePosition = m_playback.m_position;
    m_playback.m_engine.command(AudioEngine::Pause);
    m_playback.m_playing = false;
    m_playback.m_loaded = false;
    m_playback.m_loading = true;
    m_playback.m_autoplay = autoplay;
    m_playback.m_seamless = false;
    m_playback.m_error.clear();
    emit m_playback.changed();
    if (!m_playback.m_urls.isEmpty()) {
        m_playback.m_urlIndex = qMax(0, m_playback.m_urlIndex - 1);
        m_playback.loadNextUrl();
    } else if (m_playback.m_ready)
        m_playback.resolve();
}

void PlaybackOutputController::consumeOutputEvent(const QJsonObject &event)
{
    if (m_playback.m_closing)
        return;
    const auto kind = event.value("event").toString();
    if (kind == "ready") {
        m_playback.m_ready = true;
        m_playback.m_initializationError.clear();
        emit m_playback.engineInitializationChanged();
        // Queue this before transition setup, restored queues or pending loads.
        // Changes made while native initialization was pending are read here.
        m_playback.m_engine.networkTimeout(
            m_playback.m_networkPreferences
                ? m_playback.m_networkPreferences->playerTimeoutSeconds()
                : 60);
        m_playback.m_engine.command(AudioEngine::Volume, m_playback.localVolume());
        m_playback.m_engine.command(AudioEngine::Speed, m_playback.m_speed);
        m_playback.m_audioControls.initialize();
        m_playback.m_transition->initialize();
        m_playback.tryStartupAutoPlay();
        if (m_playback.m_loading)
            m_playback.resolve();
        return;
    }
    if (kind == "system-command") {
        const auto c = event.value("command").toString();
        if (c == "Play")
            m_playback.play();
        else if (c == "Pause")
            m_playback.pause();
        else if (c == "Stop")
            m_playback.stop();
        else if (c == "NextSong")
            m_playback.next();
        else if (c == "PreviousSong")
            m_playback.previous();
        else if (c == "Seek")
            m_playback.seek(qint64(event.value("position_ms").toDouble()));
        else if (c == "SeekForward")
            m_playback.seek(m_playback.m_position +
                            qint64(event.value("offset_ms").toDouble(10000)));
        else if (c == "SeekBackward")
            m_playback.seek(m_playback.m_position -
                            qint64(event.value("offset_ms").toDouble(10000)));
        return;
    }
    if (kind == "media-error") {
        qWarning().noquote() << "System media:" << event.value("message").toString();
        return;
    }
    // Capture A before an actual file-loaded boundary adopts B and clears the
    // live model. Nested identities authenticate the pending native request.
    if (!m_playback.m_cast->remoteMedia() && event.contains("transition_playback"))
        m_playback.m_transitionPresentation->observe(event.value("transition_playback").toObject(),
                                                     m_playback.m_audioSequence, m_playback.m_track,
                                                     QUrl(m_playback.cover()), m_playback.m_lyrics,
                                                     m_playback.m_lyricState,
                                                     m_playback.m_lyricText.errorMessage().isEmpty()
                                                         ? m_playback.m_lyricSource.errorMessage()
                                                         : m_playback.m_lyricText.errorMessage());
    if (!m_playback.m_cast->remoteMedia() && m_playback.m_transition->handleEvent(event))
        return;
    const auto seq = event.value("track_seq");
    if (!seq.isNull() && !seq.isUndefined() &&
        quint64(seq.toDouble()) != m_playback.m_audioSequence)
        return;
    if (!event.value("source_revision").isNull() && !event.value("source_revision").isUndefined() &&
        quint64(event.value("source_revision").toInteger()) != m_playback.m_sourceRevision)
        return;
    if (!m_playback.m_cast->remoteMedia() && kind != "state-change" &&
        m_playback.m_recovery->handleEvent(event))
        return;
    // Recovery may reject old output positions; do not let those clear busy.
    if (kind == "time-update" || kind == "seeked") {
        if (!m_playback.m_cast->remoteMedia() &&
            !m_playback.m_recovery->acceptsPosition(qint64(event.value("time").toDouble() * 1000)))
            return;
    }
    m_playback.m_progress.observe(event);
    m_playback.publishProgressStatus();
    if (kind == "source-switched" ||
        (kind == "file-loaded" &&
         quint64(event.value("seq").toDouble()) == m_playback.m_audioSequence))
        m_playback.requestEmbeddedMetadata();
    if (kind == "source-switched") {
        m_playback.m_failure->sourceAdopted();
        m_playback.m_progress.reset();
        m_playback.m_audioControls.setTrackLoudness(m_playback.m_pendingLoudness);
        m_playback.m_loading = false;
        m_playback.m_loaded = true;
        m_playback.m_appliedEffect = m_playback.m_sourceEffect;
        m_playback.m_seamless = false;
        m_playback.m_position = qint64(event.value("time").toDouble() * 1000);
        m_playback.m_duration = qint64(event.value("duration").toDouble() * 1000);
        m_playback.m_error.clear();
        publishMedia();
        m_playback.m_lyricText.setPosition(m_playback.m_position);
        if (m_playback.m_sourceKind == "mv")
            m_playback.m_climax.clear();
        else
            m_playback.m_climax.load(m_playback.m_track, m_playback.m_duration);
        m_playback.m_lyricSource.setDuration(m_playback.m_duration);
        emit m_playback.changed();
        emit m_playback.timelineReset();
        emit m_playback.positionChanged();
    } else if (kind == "file-loaded") {
        if (quint64(event.value("seq").toDouble()) != m_playback.m_audioSequence)
            return;
        m_playback.m_failure->sourceAdopted();
        m_playback.m_audioControls.setTrackLoudness(m_playback.m_pendingLoudness);
        m_playback.m_loading = false;
        m_playback.m_loaded = true;
        m_playback.m_appliedEffect = m_playback.m_sourceEffect;
        m_playback.m_error.clear();
        publishMedia();
        if (m_playback.m_resumePosition >= 0) {
            if (m_playback.m_cast->remoteMedia())
                m_playback.m_cast->seek(
                    qBound<qint64>(0, m_playback.m_resumePosition, m_playback.m_duration));
            else
                m_playback.m_engine.command(
                    AudioEngine::Seek,
                    double(qBound<qint64>(0, m_playback.m_resumePosition, m_playback.m_duration)) /
                        1000.);
            m_playback.m_resumePosition = -1;
        }
        if (m_playback.m_autoplay) {
            if (m_playback.m_cast->remoteMedia())
                m_playback.m_cast->playMedia();
            else if (m_playback.m_preferences.volumeFade())
                m_playback.m_engine.playFaded(m_playback.localVolume(),
                                              m_playback.m_preferences.volumeFadeMs());
            else
                m_playback.m_engine.command(AudioEngine::Play);
        }
        if (m_playback.m_sourceKind == "mv")
            m_playback.m_climax.clear();
        else
            m_playback.m_climax.load(m_playback.m_track, m_playback.m_duration);
        m_playback.m_lyricSource.setDuration(m_playback.m_duration);
        m_playback.m_listenTogether->nativeTrackReady();
        emit m_playback.changed();
    } else if (kind == "duration-change") {
        m_playback.m_duration = qint64(event.value("duration").toDouble() * 1000);
        m_playback.m_climax.setDuration(m_playback.m_duration);
        m_playback.m_lyricSource.setDuration(m_playback.m_duration);
        emit m_playback.changed();
    } else if (kind == "time-update" || kind == "seeked") {
        const auto position = qint64(event.value("time").toDouble() * 1000);
        if (kind == "seeked")
            m_playback.m_listening.resetPosition();
        // Only the resolved online catalog source is reported. Cloud files and
        // MV audio keep their own source kind and must not become CSCC plays.
        m_playback.m_listening.tick(m_playback.m_track.key(), m_playback.m_track.songId,
                                    m_playback.m_sourceKind == "catalog",
                                    m_playback.m_playing && !m_playback.m_loading &&
                                        m_playback.progressBusyReason() ==
                                            PlaybackService::ProgressReady,
                                    position, m_playback.speed());
        const auto elapsed = m_playback.m_listenTick.restart();
        if (m_playback.m_playing && elapsed > 0 && elapsed < 2000)
            m_playback.m_listenedMs += elapsed;
        m_playback.m_position = position;
        if (kind == "seeked")
            m_playback.m_listenTogether->nativeSeekCompleted();
        m_playback.m_lyricText.setPosition(m_playback.m_position);
        if (kind == "seeked") {
            emit m_playback.timelineReset();
            emit m_playback.seekCompleted();
        }
        emit m_playback.positionChanged();
    } else if (kind == "state-change") {
        m_playback.m_listening.resetPosition();
        const auto s = event.value("state").toObject();
        m_playback.m_playing = s.value("playing").toBool() && !s.value("paused").toBool();
        if (m_playback.m_playing)
            m_playback.m_failure->succeeded();
        if (!m_playback.m_cast->remoteMedia())
            m_playback.m_recovery->handleEvent(event);
        m_playback.m_listenTick.restart();
        m_playback.m_engine.mediaTimeline(m_playback.m_position, m_playback.m_duration,
                                          m_playback.m_playing, m_playback.speed());
        emit m_playback.changed();
    } else if (kind == "playback-end") {
        if (event.value("reason").toString() != "eof")
            return;
        emit m_playback.playbackEnded();
        m_playback.m_listening.flush(true);
        m_playback.m_playing = false;
        if (!m_playback.m_sleepTimer.trackEnded())
            m_playback.next(true);
    } else if (kind == "error") {
        if (!m_playback.m_ready) {
            m_playback.m_initializationError = event.value("message").toString();
            emit m_playback.engineInitializationChanged();
        }
        m_playback.m_progress.reset();
        m_playback.m_error = event.value("message").toString();
        if (!m_playback.m_seamless)
            m_playback.m_playing = false;
        const bool mediaFailure = PlaybackSourceRequest::mediaError(event);
        if (mediaFailure && !m_playback.m_urls.isEmpty() &&
            m_playback.m_urlIndex < m_playback.m_urls.size()) {
            m_playback.m_loaded = false;
            m_playback.m_seamless = false;
            m_playback.m_loading = true;
            m_playback.m_resumePosition =
                m_playback.m_sourceKind == "mv" ? -1 : m_playback.m_position;
            m_playback.loadNextUrl();
        } else if (mediaFailure && m_playback.recoverSource(m_playback.m_error)) {
            emit m_playback.changed();
        } else {
            // Recoverable URL/source fallbacks keep the wall-clock segment
            // open. Only a terminal failure actually ends this listen.
            if (!m_playback.m_seamless)
                m_playback.m_listening.flush();
            m_playback.m_loading = false;
            m_playback.m_failure->failed();
            emit m_playback.changed();
        }
    }
}

void PlaybackOutputController::publishMedia()
{
    if (!m_playback.m_ready)
        return;
    m_playback.m_engine.mediaMetadata(m_playback.m_track.title, m_playback.m_track.artist,
                                      m_playback.m_track.album, m_playback.m_duration,
                                      m_playback.m_coverPng);
    m_playback.m_engine.mediaTimeline(m_playback.m_position, m_playback.m_duration,
                                      m_playback.m_playing, m_playback.speed());
}
} // namespace EchoMusic
