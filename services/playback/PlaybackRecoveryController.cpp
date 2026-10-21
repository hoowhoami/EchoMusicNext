#include "PlaybackRecoveryController.h"
#include "PlaybackService.h"
#include "PlaybackFailureController.h"
#include "TrackTransitionController.h"
#include <QDebug>
namespace EchoMusic
{
PlaybackRecoveryController::PlaybackRecoveryController(PlaybackService &playback)
    : QObject(&playback), m_playback(playback)
{
    m_resumeTimer.setSingleShot(true);
    m_recoveryTimer.setSingleShot(true);
    connect(&m_resumeTimer, &QTimer::timeout, this, [this] {
        auto &p = m_playback;
        if (!p.m_closing && m_sequence == p.m_sequence && m_revision == p.m_api.sessionRevision() &&
            p.m_autoplay && !p.m_playing && !p.m_loading)
            recover(false);
    });
    connect(&m_recoveryTimer, &QTimer::timeout, this, [this] {
        auto &p = m_playback;
        if (m_target < 0 || p.m_closing || m_sequence != p.m_sequence ||
            m_revision != p.m_api.sessionRevision())
            return;
        // Never leave a stalled reload pinned to a progress guard indefinitely.
        cancel();
        p.m_source.cancel();
        p.m_loading = false;
        p.m_error = tr("播放恢复超时，请重试");
        p.m_playing = false;
        p.m_engine.command(AudioEngine::Pause);
        p.m_failure->failed();
        emit p.changed();
    });
}
void PlaybackRecoveryController::cancel()
{
    m_resumeTimer.stop();
    m_recoveryTimer.stop();
    m_target = -1;
}
void PlaybackRecoveryController::resumeRequested()
{
    auto &p = m_playback;
    if (p.m_playing || p.m_enginePreferences.playResumeTimeout() <= 0)
        return;
    m_sequence = p.m_sequence;
    m_revision = p.m_api.sessionRevision();
    m_resumeTimer.start(qRound(p.m_enginePreferences.playResumeTimeout() * 1000));
}
bool PlaybackRecoveryController::handleEvent(const QJsonObject &event)
{
    if (event.value("event") == "state-change" && m_playback.m_playing)
        m_resumeTimer.stop();
    if (event.value("event") == "error") {
        m_recoveryTimer.stop();
        m_target = -1;
    }
    if (event.value("event") != "stalled")
        return false;
    auto &p = m_playback;
    if (p.m_playing && !p.m_loading && m_target < 0 &&
        p.m_enginePreferences.playbackStallTimeout() > 0)
        recover(true);
    return true;
}
bool PlaybackRecoveryController::acceptsPosition(qint64 position)
{
    if (m_target < 0)
        return true;
    // New readers emit zero before the recovery seek acknowledges. Keep the
    // displayed progress at its saved point until the new session reaches it.
    if (position + 200 < m_target)
        return false;
    m_target = -1;
    m_recoveryTimer.stop();
    return true;
}
void PlaybackRecoveryController::recover(bool stall)
{
    auto &p = m_playback;
    if (p.m_closing || !p.hasTrack() || p.m_loading || !p.m_autoplay)
        return;
    if (m_attemptTrack != p.m_track.stableKey()) {
        m_attemptTrack = p.m_track.stableKey();
        m_attempts = 0;
    }
    if (stall && m_attempts >= p.m_enginePreferences.playbackStallMaxAttempts()) {
        p.m_error = tr("播放失败，已达到最大自动恢复次数");
        cancel();
        p.m_playing = false;
        p.m_engine.command(AudioEngine::Pause);
        p.m_failure->failed();
        emit p.changed();
        return;
    }
    if (stall)
        ++m_attempts;
    m_resumeTimer.stop();
    m_target = p.m_position;
    m_sequence = p.m_sequence;
    m_revision = p.m_api.sessionRevision();
    p.m_transition->clear();
    p.m_engine.cancelSourceReplacement();
    p.m_source.cancel();
    ++p.m_sourceRevision;
    p.m_loaded = false;
    p.m_seamless = false;
    p.m_loading = true;
    p.m_resumePosition = m_target;
    p.m_error.clear();
    p.m_engine.command(AudioEngine::Stop);
    m_recoveryTimer.start(20000);
    // Try an unused backup before resolving signed URLs again.
    if (stall && p.m_urlIndex < p.m_urls.size())
        p.loadNextUrl();
    else {
        p.m_urls.clear();
        p.m_urlIndex = 0;
        if (p.m_sourceKind == "mv") {
            p.m_mvRecovery = true;
            if (!p.m_preferences.mvAudioFallbackEnabled() || !p.m_source.tryMv(p.m_track)) {
                p.m_loading = false;
                p.m_error = tr("MV 音频无法恢复");
                p.m_failure->failed();
            }
        } else
            p.m_source.resolve(
                p.m_track, p.apiQuality(), p.apiEffect(), p.m_privileges,
                p.m_preferences.compatibilityMode(), p.m_preferences.viperQualityEnabled(),
                p.m_listenTogether->roomSourceAllowed(p.m_track) ? p.m_listenTogether->roomId()
                                                                 : QString(),
                p.m_sourcePreference, p.m_audioControls.volumeNormalization());
    }
    qInfo() << "Playback recovery" << (stall ? "stall" : "resume") << "attempt" << m_attempts
            << "position" << m_target;
    emit p.changed();
}
} // namespace EchoMusic
