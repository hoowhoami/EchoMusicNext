#include "SettingsKeys.h"
#include "TrackTransitionController.h"
#include "PlaybackService.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QUrl>
#include <cmath>

namespace EchoMusic
{
bool TrackTransitionController::containsSource(const QUrl &url) const
{
    for (const auto &source : m_urls)
        if (QUrl(source) == url)
            return true;
    for (const auto &source : m_resolved.value("urls").toArray())
        if (QUrl(source.toString()) == url)
            return true;
    return false;
}

namespace
{
QString queueTrackId(const PlaybackTrack &track)
{
    return track.id.isEmpty() ? track.key() : track.id;
}
const QStringList modes{"automix-pro", "automix-basic", "fade", "gapless", "none"};

} // namespace
TrackTransitionController::TrackTransitionController(PlaybackService &playback, ApiClient &api,
                                                     QSettings &settings)
    : QObject(&playback), m_playback(playback), m_api(api), m_settings(settings),
      m_source(api, playback.m_membership, this, playback.cloudSources(),
               playback.sourceExtensions())
{
    const auto stored =
        settings.value(EchoMusic::SettingsKeys::Playback::TransitionMode, "automix-pro").toString();
    if (modes.contains(stored))
        m_mode = stored;
    m_fadeSeconds =
        qBound(0, settings.value(EchoMusic::SettingsKeys::Playback::FadeSeconds, 15).toInt(), 15);
    m_matchTempo = settings.value(EchoMusic::SettingsKeys::Playback::MatchTempo, false).toBool();
    m_timeout.setSingleShot(true);
    connect(&api, &ApiClient::sessionChanged, this, [this] {
        clear();
        m_shuffleSource.clear();
        m_shuffleTracks.clear();
        m_shuffleRemaining.clear();
        m_shufflePlayed.clear();
        m_shuffleHistory.clear();
    });
    connect(playback.sourceExtensions(), &PlaybackSourceExtensions::changed, this,
            [this] { clear(); });
    connect(&m_timeout, &QTimer::timeout, this, &TrackTransitionController::failAttempt);
    connect(&m_source, &PlaybackSourceRequest::failed, this, [this] { failAttempt(); });
    connect(&m_source, &PlaybackSourceRequest::resolved, this,
            [this](const PlaybackTrack &track, const QJsonObject &privileges,
                   const QJsonObject &source, bool fallback, const QString &resolvedQuality) {
                if (!stillValid() || track.stableKey() != m_targetKey)
                    return;
                m_target = track;
                m_privileges = privileges;
                m_resolved = source;
                m_resolvedQuality = resolvedQuality;
                m_fallback = fallback;
                m_urls.clear();
                m_urlAudioTracks.clear();
                for (const auto &value : source.value("sources").toArray()) {
                    const auto item = value.toObject();
                    m_urls.append(item.value("url").toString());
                    m_urlAudioTracks.append(item.value("audioTrackId").toInteger());
                }
                for (const auto &url : PlaybackSourceRequest::usableUrls(source))
                    if (!m_urls.contains(url)) {
                        m_urls.append(url);
                        m_urlAudioTracks.append(0);
                    }
                if (m_urls.isEmpty()) {
                    failAttempt();
                    return;
                }
                const auto effect =
                    source.value("effect").toString(fallback ? "none" : m_playback.m_songEffect);
                if (!m_urlAudioTracks.value(0) && !fallback &&
                    (effect == "vocal" || effect == "accompaniment") &&
                    QUrl(m_urls.first()).path().endsWith(".mkv", Qt::CaseInsensitive)) {
                    m_probe = m_playback.m_native.submit(NativeBridge::AudioTracks, m_urls.first(),
                                                         m_revision);
                    if (m_probe)
                        return;
                }
                prepareSource();
            });
    connect(&playback.m_native, &NativeBridge::completed, this,
            [this](quint64 id, quint64 revision, bool success, const QByteArray &bytes) {
                if (!m_probe || id != m_probe || revision != m_revision || !stillValid())
                    return;
                m_probe = 0;
                const auto audioTrack = effectAudioTrack(
                    success ? QJsonDocument::fromJson(bytes).array() : QJsonArray{},
                    m_resolved.value("effect").toString(m_playback.m_songEffect));
                m_preparedGain = m_playback.m_audioControls.gainForSource(m_resolved);
                m_playback.m_engine.prepareNext(m_urls.first(), audioTrack, m_sequence, m_requestId,
                                                m_revision, m_preparedGain);
            });
    connect(&playback.m_sleepTimer, &SleepTimer::changed, this, [this] {
        if (!m_context.isEmpty() &&
            (m_playback.m_sleepTimer.active() || m_playback.m_sleepTimer.waiting()))
            clear();
    });
    connect(&playback.m_preferences, &PlaybackPreferences::changed, this,
            &TrackTransitionController::queueChanged);
    connect(&playback.m_membership, &MembershipService::factsChanged, this, [this] {
        // The first lookup settles before a source is resolved. Leave that
        // request alive; discard only an already resolved/prepared entitlement.
        if (!m_resolved.isEmpty())
            clear();
    });
    connect(&playback.m_audioControls, &PlaybackAudioControls::changed, this, [this] {
        if (m_requestId && !m_resolved.isEmpty() &&
            std::abs(m_preparedGain - m_playback.m_audioControls.gainForSource(m_resolved)) > .0001)
            clear();
    });
}
void TrackTransitionController::initialize()
{
    m_playback.m_engine.transition(modes.indexOf(m_mode), m_fadeSeconds, m_matchTempo);
}
void TrackTransitionController::setMode(const QString &mode)
{
    if (!modes.contains(mode) || mode == m_mode)
        return;
    clear();
    m_mode = mode;
    m_settings.setValue(EchoMusic::SettingsKeys::Playback::TransitionMode, mode);
    initialize();
    emit changed();
}
void TrackTransitionController::setFadeSeconds(int seconds)
{
    seconds = qBound(0, seconds, 15);
    if (seconds == m_fadeSeconds)
        return;
    clear();
    m_fadeSeconds = seconds;
    m_settings.setValue(EchoMusic::SettingsKeys::Playback::FadeSeconds, seconds);
    initialize();
    emit changed();
}
void TrackTransitionController::setMatchTempo(bool enabled)
{
    if (enabled == m_matchTempo)
        return;
    clear();
    m_matchTempo = enabled;
    m_settings.setValue(EchoMusic::SettingsKeys::Playback::MatchTempo, enabled);
    initialize();
    emit changed();
}
QString TrackTransitionController::contextKey() const
{
    const auto &p = m_playback;
    return QString::number(m_api.sessionRevision()) + "/" + QString::number(p.m_sequence) + "/" +
           p.m_track.stableKey() + "/" + p.m_sourceKey + "/" + p.m_quality + "/" +
           p.m_preferences.defaultQuality() + "/" + p.m_songEffect + "/" +
           QString::number(p.m_preferences.compatibilityMode()) + "/" +
           QString::number(p.m_preferences.viperQualityEnabled()) + "/" +
           QString::number(p.m_mode) + "/" + m_mode + "/" + QString::number(m_fadeSeconds) + "/" +
           QString::number(m_matchTempo) + "/" +
           (p.m_personalFm->active() ? p.m_personalFm->transitionKey() : QString());
}
void TrackTransitionController::reconcileShuffle()
{
    auto &p = m_playback;
    QStringList ids;
    for (const auto &track : p.queue()->tracks())
        ids.append(queueTrackId(track));
    bool appendOnly = ids.size() > m_shuffleTracks.size();
    for (int i = 0; appendOnly && i < m_shuffleTracks.size(); ++i)
        appendOnly = ids[i] == m_shuffleTracks[i];
    if (m_shuffleSource != p.sourceKey() || (!appendOnly && ids != m_shuffleTracks)) {
        m_shuffleRemaining.clear();
        m_shufflePlayed.clear();
    }
    m_shuffleSource = p.sourceKey();
    m_shuffleTracks = ids;
    const auto current = queueTrackId(p.currentTrack());
    m_shufflePlayed.insert(current);
    m_shuffleRemaining.removeAll(current);
    QStringList incoming;
    const QSet<QString> remaining(m_shuffleRemaining.cbegin(), m_shuffleRemaining.cend());
    for (const auto &id : ids)
        if (id != current && !m_shufflePlayed.contains(id) && !remaining.contains(id))
            incoming.append(id);
    const auto shuffle = [](QStringList &values) {
        for (int i = values.size() - 1; i > 0; --i)
            values.swapItemsAt(i, QRandomGenerator::global()->bounded(i + 1));
    };
    shuffle(incoming);
    m_shuffleRemaining.append(incoming);
    if (m_shuffleRemaining.isEmpty() && ids.size() > 1) {
        m_shufflePlayed = {current};
        for (const auto &id : ids)
            if (id != current)
                m_shuffleRemaining.append(id);
        shuffle(m_shuffleRemaining);
    }
}
int TrackTransitionController::nextIndex(bool automatic)
{
    if (m_playback.listenTogether()->suppressAutomaticNext())
        return -1;
    auto &p = m_playback;
    if (p.m_personalFm->active())
        return p.m_personalFm->nextTrack() ? -2 : -1;
    auto *queue = p.queue();
    const int n = queue->rowCount();
    if (!n)
        return -1;
    int current = -1;
    for (int i = 0; i < n; ++i)
        if (queueTrackId(*queue->at(i)) == queueTrackId(p.currentTrack()))
            current = i;
    const auto mode =
        p.sourceKey() == "queue:home-discover" ? PlaybackService::Sequential : p.m_mode;
    // Automatic single-repeat never consumes queued-next. Explicit single is list mode.
    if (automatic && mode == PlaybackService::SingleLoop)
        return current;
    const int priority = queue->peekNextPriority(queueTrackId(p.currentTrack()));
    if (mode == PlaybackService::Shuffle)
        reconcileShuffle();
    if (priority >= 0)
        return priority;
    if (mode == PlaybackService::Shuffle) {
        for (const auto &id : m_shuffleRemaining)
            for (int i = 0; i < n; ++i)
                if (queueTrackId(*queue->at(i)) == id && queue->at(i)->playable())
                    return i;
        return current;
    }
    if (current < 0)
        return 0;
    if (mode == PlaybackService::Sequential)
        return current + 1 < n ? current + 1 : -1;
    return (current + 1) % n;
}
void TrackTransitionController::consumeNext(int index, bool automatic)
{
    auto &p = m_playback;
    auto *queue = p.queue();
    if (p.m_personalFm->active() || (automatic && p.m_mode == PlaybackService::SingleLoop &&
                                     p.sourceKey() != "queue:home-discover"))
        return;
    const int priority = queue->peekNextPriority(queueTrackId(p.currentTrack()));
    if (p.m_mode == PlaybackService::Shuffle && p.sourceKey() != "queue:home-discover" &&
        queue->at(index)) {
        m_shuffleHistory.append(queueTrackId(p.currentTrack()));
        if (m_shuffleHistory.size() > 100)
            m_shuffleHistory.removeFirst();
        if (priority != index) {
            const auto target = queueTrackId(*queue->at(index));
            const auto position = m_shuffleRemaining.indexOf(target);
            if (position >= 0)
                m_shuffleRemaining.erase(m_shuffleRemaining.begin(),
                                         m_shuffleRemaining.begin() + position + 1);
            m_shufflePlayed.insert(target);
        }
    }
    if (priority == index || priority < 0)
        queue->takeNextPriority(queueTrackId(p.currentTrack()));
}
int TrackTransitionController::previousIndex()
{
    auto &p = m_playback;
    auto *queue = p.queue();
    const auto n = queue->rowCount(), current = queue->currentIndex();
    if (!n)
        return -1;
    if (p.m_personalFm->active() || p.sourceKey() == "queue:home-discover")
        return current > 0 ? current - 1 : -1;
    if (p.m_mode == PlaybackService::Shuffle)
        while (!m_shuffleHistory.isEmpty()) {
            const auto id = m_shuffleHistory.takeLast();
            for (int i = 0; i < n; ++i)
                if (queueTrackId(*queue->at(i)) == id && queue->at(i)->playable())
                    return i;
        }
    return (current - 1 + n) % n;
}
int TrackTransitionController::failureIndex()
{
    reconcileShuffle();
    const auto *queue = m_playback.queue();
    for (const auto id : m_shuffleRemaining)
        for (int i = 0; i < queue->rowCount(); ++i)
            if (queueTrackId(*queue->at(i)) == id && queue->at(i)->playable()) {
                const auto position = m_shuffleRemaining.indexOf(id);
                m_shuffleRemaining.erase(m_shuffleRemaining.begin(),
                                         m_shuffleRemaining.begin() + position + 1);
                m_shufflePlayed.insert(id);
                return i;
            }
    return -1;
}
std::optional<PlaybackTrack> TrackTransitionController::nextTrack(bool automatic)
{
    if (m_playback.m_personalFm->active())
        return m_playback.m_personalFm->nextTrack();
    const auto *track = m_playback.queue()->at(nextIndex(automatic));
    return track ? std::optional<PlaybackTrack>(*track) : std::nullopt;
}
bool TrackTransitionController::stillValid()
{
    if (m_playback.m_closing || m_context.isEmpty() || m_context != contextKey())
        return false;
    const auto index = nextIndex();
    const auto track = nextTrack();
    if (!track || track->stableKey() != m_targetKey)
        return false;
    m_targetIndex = index;
    return true;
}
void TrackTransitionController::queueChanged()
{
    if (!m_context.isEmpty() && !stillValid())
        clear();
}
void TrackTransitionController::clear(bool allowRetry)
{
    ++m_revision;
    m_timeout.stop();
    m_source.cancel();
    if (!m_context.isEmpty()) {
        if (m_sequence) {
            m_retired.append(m_sequence);
            if (m_retired.size() > 8)
                m_retired.removeFirst();
        }
        m_playback.m_engine.clearNext();
    }
    m_context.clear();
    m_targetKey.clear();
    m_urls.clear();
    m_urlAudioTracks.clear();
    m_resolved = {};
    m_privileges = {};
    m_target = {};
    m_sequence = m_requestId = m_probe = 0;
    m_targetIndex = -1;
    m_prepared = m_committing = false;
    if (allowRetry)
        m_failedContext.clear();
}
void TrackTransitionController::failAttempt()
{
    if (m_context.isEmpty())
        return;
    const auto failed = m_context + "/" + m_targetKey;
    clear(false);
    m_failedContext = failed;
}
void TrackTransitionController::tick()
{
    auto &p = m_playback;
    if (p.m_cast->remoteMedia() || !p.m_ready || !p.m_playing || p.m_loading || !p.m_loaded ||
        !p.m_error.isEmpty() || p.m_position <= 0 || p.m_duration <= 0 || m_mode == "none" ||
        p.m_sleepTimer.active() || p.m_sleepTimer.waiting())
        return;
    const auto remaining = double(p.m_duration - p.m_position) / 1000.;
    const auto lead = m_mode.startsWith("automix") ? 75.
                      : m_mode == "fade"           ? 30. + m_fadeSeconds
                                                   : 30.;
    if (remaining > lead || !m_context.isEmpty())
        return;
    const auto index = nextIndex();
    const auto track = nextTrack();
    if (!track)
        return;
    const auto context = contextKey(), key = track->stableKey();
    if (m_failedContext == context + "/" + key)
        return;
    // Release native EOF wait two seconds before expiry.
    // Smart mixing and plain transitions have separate analysis budgets.
    const auto timeout =
        std::min(m_mode.startsWith("automix") ? 60. : 20., remaining / p.m_speed - 2.);
    if (timeout < 1)
        return;
    m_context = context;
    m_targetKey = key;
    m_target = *track;
    m_targetIndex = index;
    m_sequence = ++p.m_audioSerial;
    ++m_revision;
    m_timeout.start(int(timeout * 1000));
    p.m_engine.beginNext(m_revision);
}
void TrackTransitionController::prepareSource()
{
    if (!stillValid()) {
        clear();
        return;
    }
    m_preparedGain = m_playback.m_audioControls.gainForSource(m_resolved);
    m_playback.m_engine.prepareNext(m_urls.first(), m_urlAudioTracks.value(0), m_sequence,
                                    m_requestId, m_revision, m_preparedGain);
}
bool TrackTransitionController::commit(bool automatic)
{
    if (m_committing)
        return true;
    if (!m_prepared || !stillValid() || nextIndex(automatic) != m_targetIndex ||
        m_playback.m_sleepTimer.active() || m_playback.m_sleepTimer.waiting())
        return false;
    m_committing = true;
    m_commitAutomatic = automatic;
    m_playback.m_engine.commitNext(m_revision);
    return true;
}
QString TrackTransitionController::formatNotice(const QJsonObject &event)
{
    // Report the transition the engine actually played, including fallback modes.
    // A setting change or prepared plan is not yet audible.
    const auto info = event.value("transition").toObject();
    const auto mode = info.value("mode").toString();
    const QString label = mode == "gapless"         ? tr("无缝")
                          : mode == "fade"          ? tr("淡入淡出")
                          : mode == "automix-basic" ? tr("自然衔接")
                          : mode == "automix-pro"   ? tr("节奏融合")
                                                    : QString();
    if (label.isEmpty())
        return {};
    const auto seconds = [](double value) {
        const double rounded = std::round(value * 10.) / 10.;
        if (!std::isfinite(rounded) || rounded <= 0)
            return QString();
        auto text = QString::number(rounded, 'f', 1);
        if (text.endsWith(".0"))
            text.chop(2);
        return text;
    };
    QStringList parts{label};
    const auto overlap = seconds(info.value("overlap_secs").toDouble());
    if (mode != "gapless" && !overlap.isEmpty())
        parts.append(tr("%1 秒").arg(overlap));
    const auto skipped = seconds(event.value("time").toDouble());
    if (!skipped.isEmpty())
        parts.append(tr("跳过 %1 秒").arg(skipped));
    return parts.join(QStringLiteral(" · "));
}
bool TrackTransitionController::adopt(const QJsonObject &event)
{
    const auto sequence = quint64(event.value("seq").toInteger());
    if (!sequence || sequence != m_sequence || !stillValid())
        return false;
    auto &p = m_playback;
    if (!m_committing || m_commitAutomatic)
        p.listenTogether()->nativeTrackEnded();
    // A committed native transition completes a playback segment.
    // Preparing its successor does not start or end a report.
    p.m_listening.flush(true);
    p.finishHistory();
    const auto track = m_target;
    const auto privileges = m_privileges, source = m_resolved;
    const auto fallback = m_fallback;
    const auto index = m_targetIndex;
    // Release host preparation bookkeeping without disarming the audible mix.
    m_timeout.stop();
    m_source.cancel();
    m_context.clear();
    m_targetKey.clear();
    m_requestId = m_probe = m_sequence = 0;
    m_prepared = m_committing = false;
    m_urls.clear();
    m_urlAudioTracks.clear();
    m_resolved = {};
    m_privileges = {};
    m_target = {};
    p.m_source.cancel();
    p.m_lyricSource.clear();
    ++p.m_sequence;
    ++p.m_sourceRevision;
    if (p.m_personalFm->active())
        p.m_personalFm->adoptTransition(track);
    if (!p.m_personalFm->active() && index >= 0)
        consumeNext(index, true);
    p.m_track = track;
    p.m_quality = m_nextQuality;
    p.m_sourceKind = source.value("source_kind").toString("catalog");
    p.m_sourceEffect = source.value("effect").toString(fallback ? "none" : p.m_songEffect);
    p.m_sourceNoticeCode = source.value("notice_code").toString();
    p.m_sourcePreference.clear();
    p.m_sourceFailure.clear();
    p.m_mvFailure.clear();
    p.m_resolvedQuality = m_resolvedQuality;
    p.m_audioSequence = sequence;
    p.m_engine.adoptSequence(sequence);
    p.m_trackProbe = 0;
    p.m_probedSource.clear();
    p.m_urls.clear();
    p.m_urlAudioTracks.clear();
    p.m_urlIndex = 0;
    if (index >= 0) {
        p.queue()->setCurrent(index);
    }
    p.m_privileges = privileges;
    p.m_pendingLoudness = {{"loudness", source.value("loudness")},
                           {"peak", source.value("peak")},
                           {"gain", source.value("gain")}};
    p.m_audioControls.setTrackLoudness(p.m_pendingLoudness);
    p.m_effectFallback = fallback;
    p.m_appliedEffect = p.m_sourceEffect;
    p.m_effectError = fallback ? tr("所选音效暂未生效，继续使用原声") : QString();
    p.m_position = qint64(event.value("time").toDouble() * 1000);
    p.m_duration = qint64(source.value("duration_ms").toDouble());
    p.m_progress.reset();
    p.m_loaded = true;
    p.m_loading = false;
    p.m_error.clear();
    p.m_seamless = false;
    p.m_resumePosition = -1;
    p.m_coverColor = {};
    p.m_coverPng.clear();
    p.m_embeddedCover.clear();
    p.m_artwork.load(track.cover, p.m_sequence);
    p.m_lyricText.clear();
    p.m_lyricState = "loading";
    p.requestLyrics();
    p.requestEmbeddedMetadata();
    p.publishMedia();
    p.m_climax.load(track, p.m_duration);
    p.listenTogether()->nativeTrackReady();
    emit p.changed();
    emit p.timelineReset();
    emit p.positionChanged();
    const auto message = formatNotice(event);
    if (!message.isEmpty())
        emit notice(message);
    return true;
}
bool TrackTransitionController::handleEvent(const QJsonObject &event)
{
    const auto kind = event.value("event").toString();
    if (kind.startsWith("next-source-")) {
        if (quint64(event.value("preparation_revision").toInteger()) != m_revision ||
            m_context.isEmpty())
            return true;
        if (!stillValid()) {
            clear();
            return true;
        }
        if (kind == "next-source-begun") {
            m_requestId = quint64(event.value("request_id").toInteger());
            if (!m_requestId)
                failAttempt();
            else {
                const QStringList qualities{"standard",   "higher",      "flac",       "hi_res",
                                            "viper_tape", "viper_clear", "viper_atmos"};
                m_nextQuality = m_targetKey == m_playback.m_track.stableKey()
                                    ? m_playback.m_quality
                                    : m_playback.m_preferences.defaultQuality();
                m_source.resolve(
                    m_target, ApiClient::PlaybackQuality(qualities.indexOf(m_nextQuality)),
                    m_playback.apiEffect(), {}, m_playback.m_preferences.compatibilityMode(),
                    m_playback.m_preferences.viperQualityEnabled(),
                    m_playback.listenTogether()->roomSourceAllowed(m_target)
                        ? m_playback.listenTogether()->roomId()
                        : QString(),
                    {}, m_playback.m_audioControls.volumeNormalization(), false, true);
            }
        } else if (kind == "next-source-prepared") {
            if (quint64(event.value("next_sequence").toInteger()) != m_sequence)
                return true;
            m_prepared = event.value("prepared").toBool();
            if (m_prepared)
                m_timeout.stop();
            else if (PlaybackSourceRequest::mediaError(event)) {
                if (!m_urls.isEmpty()) {
                    m_urls.removeFirst();
                    if (!m_urlAudioTracks.isEmpty())
                        m_urlAudioTracks.removeFirst();
                }
                if (!m_urls.isEmpty())
                    prepareSource();
                else if (!m_source.continueAfterFailure())
                    failAttempt();
            } else
                failAttempt();
        } else if (kind == "next-source-committed" && !event.value("committed").toBool()) {
            const auto automatic = m_commitAutomatic;
            clear();
            m_playback.next(automatic);
        }
        return true;
    }
    if (kind == "file-loaded") {
        if (adopt(event))
            return true;
        const auto seq = quint64(event.value("seq").toInteger());
        if (m_retired.contains(seq)) {
            // An already queued audible boundary can race a queue/session edit.
            // Recover to the current Qt decision; never display that stale track.
            m_retired.removeAll(seq);
            if (!m_playback.m_loading && !m_playback.m_closing) {
                clear();
                const int next = nextIndex();
                if (next == -2)
                    m_playback.next();
                else if (next >= 0)
                    m_playback.playIndex(next);
                else
                    m_playback.pause();
            }
            return true;
        }
    }
    return false;
}
} // namespace EchoMusic
