#include "SettingsKeys.h"
#include "PlaybackService.h"
#include "PlaybackSessionController.h"
#include "PlaybackOutputController.h"
#include "PlaybackQueueController.h"
#include "TrackTransitionController.h"
#include "PlaybackRecoveryController.h"
#include "PlaybackFailureController.h"
#include "SongMetadataService.h"
#include "EmbeddedCoverPresentation.h"
#include "ArtistCoverPresentation.h"
#include <QJsonDocument>
#include <QJsonArray>
#include <QFileDialog>
#include <QFileInfo>
#include <QUrl>
#include <cmath>
namespace EchoMusic
{
namespace
{
QJsonObject engineStartupOptions(const PlayerEnginePreferences &engine,
                                 const NetworkPreferences *network)
{
    auto options = engine.startupOptions();
    // Native track probing runs on a different actor and snapshots the core's
    // configuration. Initialize its deadline with the engine, rather than rely
    // solely on a queued command after ready that could race the first probe.
    options.insert("network_timeout_secs", network ? network->playerTimeoutSeconds() : 60);
    if (network)
        options.insert("http_proxy", network->transportEndpoint().toString(QUrl::FullyEncoded));
    return options;
}
} // namespace
void PlaybackService::retryEngineInitialization()
{
    if (m_closing || m_ready)
        return;
    m_initializationError.clear();
    m_error.clear();
    m_engine.retryInitialization(engineStartupOptions(m_enginePreferences, m_networkPreferences));
    emit engineInitializationChanged();
}
void PlaybackService::setStartupHeld(bool held)
{
    m_startupHeld = held;
    tryStartupAutoPlay();
}
void PlaybackService::tryStartupAutoPlay()
{
    if (m_startupHeld || !m_ready || !m_startupAutoPlayPending || m_restoring || !hasTrack() ||
        m_closing)
        return;
    m_startupAutoPlayPending = false;
    const auto sequence = m_sequence;
    QTimer::singleShot(0, this, [this, sequence] {
        if (!m_closing && !m_startupHeld && sequence == m_sequence)
            play();
    });
}
SongMetadataService *PlaybackService::metadata() const
{
    return m_metadata;
}
QString PlaybackService::cover() const
{
    return m_track.cover.isEmpty() || (!m_embeddedCover.isEmpty() &&
                                       hasArtistCover(m_track.catalogMetadata, m_track.cover))
               ? m_embeddedCover
               : m_track.cover;
}
void PlaybackService::attachMetadata(SongMetadataService &metadata)
{
    if (m_metadata == &metadata)
        return;
    m_metadata = &metadata;
    const auto bindQueue = [this](PlaybackQueueModel *model) {
        model->attachMetadata(m_metadata);
        connect(model, &PlaybackQueueModel::presentationChanged, this, [this] {
            if (!m_closing) {
                m_saveTimer.start();
                emit changed();
            }
        });
    };
    connect(&m_catalog, &PlaybackQueueCatalog::modelAdded, this, bindQueue);
    QSet<PlaybackQueueModel *> models{queue()};
    for (const auto &state : m_catalog.snapshot())
        if (auto *model = m_catalog.modelForKey(state.key))
            models.insert(model);
    for (auto *model : models)
        bindQueue(model);
    connect(&m_api, &ApiClient::sessionChanged, this, [this] { m_playbackFacts.clear(); });
    // Current-song presentation follows the same cache without restarting the
    // audio/lyric/reporting state machines on a metadata-only correction.
    const auto watchCurrent = [this] {
        if (!m_metadata)
            return;
        if (m_closing || !hasTrack()) {
            m_metadata->unwatch(this);
            return;
        }
        const auto apply = [this](const QStringList &) {
            if (!m_metadata || m_closing || !hasTrack())
                return;
            const auto beforeCover = cover();
            if (!m_track.applyPresentation(m_metadata->complete(m_track.toCatalog())))
                return;
            if (const auto source = cover(); source != beforeCover) {
                if (source.isEmpty()) {
                    m_coverPng.clear();
                    m_coverColor = QColor();
                }
                m_artwork.load(source, m_sequence);
            }
            publishMedia();
            emit changed();
        };
        m_metadata->watch(this, {{"current", m_track.toCatalog()}}, apply);
        apply({});
    };
    // Identity changes are sparse. Avoid rebuilding subscriptions on 250ms
    // playback progress or ordinary playing/volume notifications.
    auto last = std::make_shared<QString>(hasTrack() ? m_track.stableKey() : QString());
    connect(this, &PlaybackService::changed, this, [this, last, watchCurrent] {
        const auto key = m_closing || !hasTrack() ? QString() : m_track.stableKey();
        if (*last != key) {
            *last = key;
            watchCurrent();
        }
    });
    connect(&metadata, &SongMetadataService::cacheCleared, this, watchCurrent,
            Qt::QueuedConnection);
    watchCurrent();
    emit metadataServiceChanged();
}
void PlaybackService::attachLibrary(UserPlaylistModel &library, PlaylistMutationService &mutations)
{
    if (m_libraryActions)
        return;
    m_libraryActions =
        std::make_unique<PlaybackLibraryActions>(*this, m_api, library, mutations, this);
    connect(m_libraryActions.get(), &PlaybackLibraryActions::favoriteWritten, m_personalFm.get(),
            &PersonalFmController::favoriteFeedback);
    emit changed();
}
PlaybackService::PlaybackService(ApiClient &api, EchoMusic::SettingsStore &settings,
                                 QObject *parent, const QString &storagePath,
                                 NetworkPreferences *network)
    : QObject(parent), m_sleepTimer(this), m_api(api), m_settings(settings),
      m_preferences(settings, this), m_enginePreferences(settings, this),
      m_networkPreferences(network), m_membership(api, this),
      m_engine(nullptr, engineStartupOptions(m_enginePreferences, network)),
      m_spectrum(*this, m_engine), m_cloudSources(api, this), m_sourceExtensions(this),
      m_source(api, m_membership, this, &m_cloudSources, &m_sourceExtensions),
      m_audioControls(m_engine, m_native, settings, this),
      m_effectCatalog(api, m_audioControls, this), m_catalog(this), m_lyrics(this),
      m_climax(api, this), m_listening(api, this), m_lyricAppearance(settings, this),
      m_dynamicCover(api, this), m_portraits(api, *m_lyricAppearance.portrait(), this),
      m_lyricText(m_lyrics, m_native, settings, this), m_lyricSource(api, settings, this)
{
    m_sessionController = std::make_unique<PlaybackSessionController>(*this);
    m_outputController = std::make_unique<PlaybackOutputController>(*this);
    m_queueController = std::make_unique<PlaybackQueueController>(*this);
    connect(&m_songCommands, &SongCommands::sharingChanged, this, &PlaybackService::sharingChanged);

    connect(this, &PlaybackService::changed, this, &PlaybackService::publishProgressStatus);
    connect(this, &PlaybackService::changed, this, [this] { syncClock(); });
    connect(this, &PlaybackService::positionChanged, this, [this] { syncClock(true); });
    connect(this, &PlaybackService::progressBusyReasonChanged, this, [this] { syncClock(); });
    connect(this, &PlaybackService::timelineReset, this, [this] {
        m_clockSeeking = false;
        syncClock(true, "reset");
    });
    connect(&m_listening, &ListeningReporter::drained, this,
            &PlaybackService::listeningReportsDrained);
    m_dynamicCover.setNetworkPreferences(network);
    m_cast = std::make_unique<CastService>(m_native, *this, settings);
    m_cast->setNetwork(network);
    connect(m_cast.get(), &CastService::mediaRouteChanged, this,
            &PlaybackService::mediaRouteChanged);
    connect(m_cast.get(), &CastService::mediaEvent, this, &PlaybackService::consumeOutputEvent);
    connect(m_cast.get(), &CastService::mediaSourceExpired, this, [this] {
        if (m_closing || !hasTrack() || !m_cast->remoteMedia())
            return;
        if (m_castRefreshSequence == m_sequence) {
            m_loading = m_playing = false;
            m_error = tr("投放音源已过期，请重试");
            m_failure->failed();
            emit changed();
            return;
        }
        m_castRefreshSequence = m_sequence;
        refreshCurrentSource();
    });
    connect(this, &PlaybackService::changed, this, [this] {
        m_dynamicCover.setTrack(m_track);
        m_portraits.setTrack(m_track);
    });
    // The application owns network policy. Only an immutable deadline is copied
    // to the audio actor; playback does not persist a second network preference.
    if (network)
        connect(network, &NetworkPreferences::timeoutsChanged, this, [this] {
            if (m_ready && !m_closing)
                m_engine.networkTimeout(m_networkPreferences->playerTimeoutSeconds());
        });
    connect(&m_membership, &MembershipService::changed, this, &PlaybackService::changed);
    m_transition = std::make_unique<TrackTransitionController>(*this, api, settings);
    m_recovery = std::make_unique<PlaybackRecoveryController>(*this);
    m_failure = std::make_unique<PlaybackFailureController>(*this);
    m_personalFm = std::make_unique<PersonalFmController>(*this, api, settings);
    m_discoverQueue = std::make_unique<DiscoverQueueController>(*this, api);
    m_listenTogether = std::make_unique<ListenTogetherController>(*this, api);
    connect(this, &PlaybackService::changed, &m_sleepInhibitor, [this] {
        m_sleepInhibitor.setRequired(!m_closing && m_autoplay && m_playing &&
                                     m_preferences.preventSleep());
    });
    connect(&m_sleepInhibitor, &SystemSleepInhibitor::changed, this, &PlaybackService::changed);
    if (network)
        connect(&m_sleepInhibitor, &SystemSleepInhibitor::systemResumed, network,
                &NetworkPreferences::recoverAfterWake);
    connect(&m_enginePreferences, &PlayerEnginePreferences::stallTimeoutChanged, &m_engine,
            &AudioEngine::stallTimeout);
    connect(&m_source, &PlaybackSourceRequest::privilegesReady, this,
            [this](const PlaybackTrack &track, const QJsonObject &privileges) {
                if (m_closing || track.stableKey() != m_track.stableKey())
                    return;
                // The index may have upgraded m_track.id while this request was
                // in flight; re-adopt instead of reverting to the legacy id.
                const auto accepted = mergeResolvedTrack(track);
                m_track = accepted;
                m_privileges = privileges;
                if (m_metadata)
                    m_track.applyPresentation(m_metadata->complete(m_track.toCatalog()));
                m_artwork.load(cover(), m_sequence);
                requestLyrics();
                emit changed();
            });
    connect(&m_source, &PlaybackSourceRequest::resolved, this,
            [this](const PlaybackTrack &track, const QJsonObject &privileges,
                   const QJsonObject &source, bool fallback, const QString &resolvedQuality) {
                if (m_closing || track.stableKey() != m_track.stableKey())
                    return;
                // Keep the canonical view id adopted by the settled index; the
                // asynchronous source snapshot still carries the legacy one.
                const auto accepted = mergeResolvedTrack(track);
                m_track = accepted;
                m_privileges = privileges;
                m_sourceKind = source.value("source_kind")
                                   .toString(track.localPath.isEmpty() ? "catalog" : "local");
                m_sourceEffect = source.value("effect").toString(fallback ? "none" : m_songEffect);
                m_sourceNoticeCode = source.value("notice_code").toString();
                if (m_sourceKind != "mv") {
                    m_sourceFailure.clear();
                    m_mvFailure.clear();
                }
                if (m_sourceKind == "mv") {
                    m_loaded = false;
                    m_seamless = false;
                    if (!m_mvRecovery) {
                        m_resumePosition = -1;
                        m_position = 0;
                    }
                    m_mvRecovery = false;
                    m_duration = 0;
                    m_audioTrack = 0;
                    m_climax.clear();
                    m_transition->clear();
                    emit timelineReset();
                    emit positionChanged();
                }
                m_resolvedQuality = resolvedQuality;
                m_effectFallback = fallback;
                m_effectError = fallback ? tr("所选音效暂未生效，继续使用原声") : QString();
                m_pendingLoudness = {{"loudness", source.value("loudness")},
                                     {"peak", source.value("peak")},
                                     {"gain", source.value("gain")}};
                m_urls.clear();
                m_urlAudioTracks.clear();
                m_urlIndex = 0;
                for (const auto &v : source.value("sources").toArray()) {
                    const auto item = v.toObject();
                    m_urls.append(item.value("url").toString());
                    m_urlAudioTracks.append(item.value("audioTrackId").toInteger());
                }
                for (const auto &url : PlaybackSourceRequest::usableUrls(source))
                    if (!m_urls.contains(url)) {
                        m_urls.append(url);
                        m_urlAudioTracks.append(0);
                    }
                if (!m_loaded)
                    m_duration = qint64(source.value("duration_ms").toDouble());
                loadNextUrl();
                emit changed();
            });
    connect(&m_source, &PlaybackSourceRequest::failed, this, [this](const QString &message) {
        if (m_closing)
            return;
        if (tryMvFallback(message))
            return;
        m_recovery->cancel();
        m_loading = false;
        m_playing = false;
        if (!m_sourceFailure.isEmpty() && m_source.mvAttempted())
            m_mvFailure = message;
        m_error = m_sourceFailure.isEmpty() ? message : m_sourceFailure;
        m_failure->failed();
        emit changed();
    });
    connect(&m_lyricSource, &LyricSourceController::changed, this, [this] {
        m_lyricState = m_lyricSource.state();
        emit changed();
    });
    connect(&m_lyricSource, &LyricSourceController::activated, this,
            [this](const QJsonObject &lyrics, const QString &trackKey) {
                if (m_closing || trackKey != m_track.stableKey())
                    return;
                if (!m_lyricText.replace(lyrics, trackKey))
                    m_lyricState = "error";
                m_lyricText.setPosition(m_position);
                emit changed();
            });
    m_volume = qBound(
        0., settings.value(EchoMusic::SettingsKeys::Playback::Volume, DefaultVolume).toDouble(),
        150.);
    m_unmutedVolume = m_volume > 0 ? m_volume : DefaultVolume;
    m_speed =
        qBound(.1, settings.value(EchoMusic::SettingsKeys::Playback::Speed, 1).toDouble(), 5.);
    m_mode =
        PlayMode(qBound(0, settings.value(EchoMusic::SettingsKeys::Playback::Mode, 0).toInt(), 3));
    m_quality = m_preferences.defaultQuality();
    m_startupAutoPlayPending = m_preferences.autoPlayOnLaunch();
    connect(&m_cloudSources, &CloudAudioIndex::settled, this, &PlaybackService::changed);
    connect(&m_cloudSources, &CloudAudioIndex::settled, this, [this] {
        // The index may complete after playback started from a legacy snapshot
        // (id "0"); re-adopt the file identity so every surface highlights the
        // same song. The stable key keeps fetched embedded lyrics attached.
        if (!hasTrack() || m_closing)
            return;
        auto adopted = m_track;
        if (!adoptCloudIdentity(adopted) || adopted.key() == m_track.key())
            return;
        const auto row = queue()->currentIndex();
        const auto *rowTrack = row >= 0 ? queue()->at(row) : nullptr;
        if (rowTrack && rowTrack->stableKey() == adopted.stableKey())
            queue()->adoptIdentity(row, adopted);
        m_track = adopted;
        publishMedia();
        emit changed();
    });
    connect(&m_preferences, &PlaybackPreferences::mvPolicyChanged, this, [this] {
        if (!m_preferences.mvAudioFallbackEnabled()) {
            m_source.disableMv();
        }
        emit changed();
    });
    connect(&m_preferences, &PlaybackPreferences::changed, this, [this] {
        if (!m_preferences.viperQualityEnabled() && m_quality.startsWith("viper_"))
            setQuality("hi_res");
        emit changed();
    });
    connect(&m_preferences, &PlaybackPreferences::sourcePolicyChanged, this, [this] {
        if (hasTrack() && m_track.localPath.isEmpty())
            refreshCurrentSource();
    });
    const auto effect =
        settings.value(EchoMusic::SettingsKeys::Playback::SongEffect, "none").toString();
    if (QStringList{"none", "piano", "vocal", "accompaniment", "subwoofer", "ancient", "surnay",
                    "dj"}
            .contains(effect))
        m_songEffect = effect;
    connect(
        &m_native, &NativeBridge::completed, this,
        [this](quint64 id, quint64 revision, bool success, const QByteArray &bytes) {
            if (id && id == m_embeddedRequest) {
                m_embeddedRequest = 0;
                if (!success || revision != m_sequence || m_closing)
                    return;
                const auto metadata = QJsonDocument::fromJson(bytes).object();
                const auto title = metadata.value("title").toString();
                const auto artist = metadata.value("artist").toString();
                const auto album = metadata.value("album").toString();
                // Provider policies mask sensitive artist names with asterisks
                // (e.g. "***"); embedded tags carry the real identity. Treat a
                // masked value as missing so it can adopt the embedded fact.
                const auto missing = [](const QString &value, const QStringList &unknown) {
                    return value.trimmed().isEmpty() ||
                           unknown.contains(value.trimmed().toLower()) ||
                           SongMetadataService::maskedName(value);
                };
                if (!title.isEmpty() &&
                    (missing(m_track.title, {"未知歌曲", "unknown title", "unknown"}) ||
                     (!m_track.localPath.isEmpty() &&
                      m_track.title == QFileInfo(m_track.localPath).completeBaseName())))
                    m_track.title = title;
                if (!artist.isEmpty() && missing(m_track.artist, {"未知歌手", "未知艺术家",
                                                                  "unknown artist", "unknown"})) {
                    m_track.artist = artist;
                    // The bar renders artistLinks from retained refs; a masked
                    // provider snapshot must not shadow the file's own tags.
                    const auto refs = SongMetadataService::artistRefs(artist);
                    if (!refs.isEmpty()) {
                        m_track.catalogMetadata["artist_refs"] = refs;
                        m_track.catalogMetadata.remove("filename_artist_name");
                    }
                }
                if (!album.isEmpty() &&
                    missing(m_track.album, {"未知专辑", "unknown album", "unknown"}))
                    m_track.album = album;
                const auto coverFile = metadata.value("cover_file").toString();
                if (!coverFile.isEmpty() && QFileInfo(coverFile).fileName() == coverFile) {
                    const auto path = m_embeddedDirectory.filePath(coverFile);
                    if (m_metadata)
                        m_metadata->publishEmbeddedCover(m_track.toCatalog(), path);
                    if ((m_track.cover.isEmpty() ||
                         hasArtistCover(m_track.catalogMetadata, m_track.cover)) &&
                        QFileInfo::exists(path)) {
                        m_embeddedCover = QUrl::fromLocalFile(path).toString();
                        m_artwork.load(m_embeddedCover, m_sequence);
                    }
                }
                m_lyricSource.updateEmbeddedDocument(
                    m_track.stableKey(), title, artist, metadata.value("lyric").toString(),
                    metadata.value("lyric_error").toObject().value("message").toString());
                // Unmatched uploads often lack a provider duration; the
                // demuxed stream the engine reports is authoritative.
                if (!m_track.durationMs && m_duration > 0)
                    m_track.durationMs = quint64(m_duration);
                // Queue copies and catalog pages under the same file join the
                // presentation once the file itself identified its own facts.
                // Reader artwork belongs to this playback session. Publishing
                // its temporary URL as a catalog fact would let queues and
                // history retain it after the owning directory is destroyed.
                const auto durableCover =
                    (hasEmbeddedCover(m_track.catalogMetadata, m_track.cover) ||
                     hasArtistCover(m_track.catalogMetadata, m_track.cover))
                        ? QString()
                        : m_track.cover;
                const auto facts = QJsonObject{
                    {"id", m_track.id},
                    {"hash", m_track.hash},
                    {"hash_std", m_track.catalogMetadata.value("hash_std")},
                    {"title", m_track.title},
                    {"artist", m_track.artist},
                    {"album", m_track.album},
                    {"album_id", m_track.albumId ? QString::number(m_track.albumId) : QString()},
                    {"duration_ms",
                     m_track.durationMs ? QJsonValue(qint64(m_track.durationMs)) : QJsonValue()},
                    {"cover_url", durableCover},
                    {"cloud_source", m_track.catalogMetadata.value("cloud_source")}};
                queue()->updateFacts(m_track.id, m_track.hash, m_track.title, m_track.artist,
                                     m_track.album, durableCover, m_track.durationMs);
                // Session memory keyed by audio hash: lists loaded after the
                // play replay these corrections at projection time.
                if (!m_track.hash.isEmpty())
                    m_playbackFacts.insert(
                        m_track.hash.toLower(), new QJsonObject(facts),
                        std::max(
                            1, int((QJsonDocument(facts).toJson(QJsonDocument::Compact).size() * 2 +
                                    1999) /
                                   2000)));
                if (m_metadata)
                    m_metadata->publishEmbedded(
                        {{"hash", m_track.hash},
                         {"local_path", m_track.localPath},
                         {"title", title},
                         {"artists", SongMetadataService::artistRefs(artist)},
                         {"album_title", album},
                         {"duration_ms", m_duration > 0 ? QJsonValue(m_duration) : QJsonValue()}});
                emit trackFactsApplied(facts);
                publishMedia();
                emit changed();
                return;
            }
            if (!m_trackProbe || id != m_trackProbe || revision != m_sequence || m_closing)
                return;
            m_trackProbe = 0;
            m_audioTrack = effectAudioTrack(
                success ? QJsonDocument::fromJson(bytes).array() : QJsonArray{}, m_sourceEffect);
            if (m_seamless)
                m_engine.switchSource(m_probedSource, m_audioSequence, m_audioTrack,
                                      m_sourceRevision);
            else
                m_engine.load(m_probedSource, (m_audioSequence = ++m_audioSerial), m_audioTrack);
            m_probedSource.clear();
        });
    m_mediaTimer.setInterval(1000);
    connect(&m_mediaTimer, &QTimer::timeout, this, [this] {
        m_sleepTimer.tick(hasTrack() ? m_track.stableKey() : QString(), m_playing);
        m_transition->tick();
        if (m_ready && hasTrack())
            m_engine.mediaTimeline(m_position, m_duration, m_playing, speed());
    });
    m_mediaTimer.start();
    connect(&m_audioControls, &PlaybackAudioControls::selectedOutputDisconnected, this,
            &PlaybackService::pause);
    connect(&m_sleepTimer, &SleepTimer::actionRequested, this, [this](const QString &action) {
        if (action == "pause") {
            pause();
            m_sleepTimer.executionFinished();
        } else {
            stop();
            emit sleepActionRequested(action);
        }
    });
    connect(&m_effectCatalog, &AudioEffectCatalog::closed, this, [this] {
        if (++m_closedWorkers == (m_repository ? 5 : 4))
            emit closed();
    });
    connect(&m_native, &NativeBridge::closed, this, [this] {
        if (++m_closedWorkers == (m_repository ? 5 : 4))
            emit closed();
    });
    m_revision = api.sessionRevision();
    connect(&m_audioControls, &PlaybackAudioControls::importsClosed, this, [this] {
        if (++m_closedWorkers == (m_repository ? 5 : 4))
            emit closed();
    });
    m_account = api.currentAccountUserId();
    connect(&m_artwork, &PlaybackArtwork::ready, this,
            [this](quint64 sequence, const QByteArray &png, const QColor &color) {
                if (m_closing || sequence != m_sequence)
                    return;
                m_coverColor = color;
                m_coverPng = png;
                publishMedia();
                emit changed();
            });
    m_listenTick.start();
    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(750);
    connect(&m_saveTimer, &QTimer::timeout, this, &PlaybackService::persist);
    connect(&m_catalog, &PlaybackQueueCatalog::changed, this, [this] {
        if (!m_restoring && !m_closing) {
            if (m_queueRestorePending && !m_catalog.snapshot().isEmpty())
                m_queueRestorePending = false;
            m_saveTimer.start();
        }
        m_transition->queueChanged();
        emit changed();
    });
    const auto bindQueue = [this](PlaybackQueueModel *model) {
        connect(model, &PlaybackQueueModel::currentRemoved, this, [this, model] {
            if (m_closing || m_restoring || model != queue())
                return;
            if (m_personalFm->active())
                m_personalFm->abandonQueue();
            stop();
            m_track = {};
            m_sourceKind = "catalog";
            m_sourceNoticeCode.clear();
            m_sourceEffect = "none";
            m_sourcePreference.clear();
            m_sourceFailure.clear();
            m_mvFailure.clear();
            // stop() preserves a selected song's lyrics for replay. Removing
            // that selection retires its presentation on every lyric surface.
            m_duration = 0;
            m_coverColor = QColor();
            m_coverPng.clear();
            m_privileges = {};
            m_error.clear();
            m_lyricSource.clear();
            m_lyricText.clear();
            publishMedia();
            emit changed();
        });
    };
    connect(&m_catalog, &PlaybackQueueCatalog::modelAdded, this, bindQueue);
    bindQueue(queue());
    if (!storagePath.isEmpty()) {
        m_repository = std::make_unique<PlaybackRepository>(storagePath);
        m_listenTogether->attachRepository(*m_repository);
        connect(m_repository.get(), &PlaybackRepository::loaded, this,
                [this](quint64 account, quint64 revision, const PlaybackQueueStates &states,
                       const QString &active, const QString &lastLocal) {
                    if (m_closing || !m_queueRestorePending || revision != m_restoreSequence ||
                        account != PlaybackRepository::SharedQueueScope ||
                        m_sequence != m_restoreSequence)
                        return;
                    m_queueRestorePending = false;
                    m_restoring = true;
                    m_catalog.restore(states, active, lastLocal, true);
                    m_queueTitle = m_catalog.activeTitle();
                    m_sourceKey = m_catalog.activeKey();
                    m_track = {};
                    if (const auto *track = queue()->at(queue()->currentIndex())) {
                        m_track = *track;
                        m_duration = qint64(track->durationMs);
                    }
                    // A restored track never runs startTrack, and the metadata
                    // presentation only reloads the artwork when the cover URL
                    // itself changes, so without this the native surfaces
                    // (taskbar preview, SMTC) would keep the placeholder while
                    // the QML covers, which fetch the URL directly, look fine.
                    loadCurrentArtwork();
                    m_restoring = false;
                    if (std::any_of(states.cbegin(), states.cend(), [](const auto &state) {
                            return state.key.startsWith("fm/") ||
                                   state.key == "queue:home-discover" ||
                                   state.key == "queue:listen-together";
                        }))
                        m_saveTimer.start();
                    m_loaded = false;
                    emit changed();
                    tryStartupAutoPlay();
                });
        connect(m_repository.get(), &PlaybackRepository::failed, this, [](const QString &message) {
            qWarning().noquote() << "Playback storage:" << message;
        });
        connect(m_repository.get(), &PlaybackRepository::historyLoaded, this,
                [this](quint64 account, quint64 revision, const QJsonArray &entries) {
                    if (m_closing || account != m_account || revision != m_api.sessionRevision())
                        return;
                    emit historyLoaded(entries);
                });
        connect(m_repository.get(), &PlaybackRepository::historyFailed, this,
                &PlaybackService::historyFailed);
        connect(m_repository.get(), &PlaybackRepository::historyChanged, this,
                [this](quint64 account) {
                    if (!m_closing && account == m_account)
                        requestHistory();
                });
        connect(m_repository.get(), &PlaybackRepository::closed, this, [this] {
            if (++m_closedWorkers == 5)
                emit closed();
        });
        m_restoreSequence = m_sequence;
        m_queueRestorePending = true;
        m_repository->load(PlaybackRepository::SharedQueueScope, m_restoreSequence);
    }
    connect(&api, &ApiClient::sessionChanged, this, [this] {
        const bool restorePending = m_queueRestorePending && m_sequence == m_restoreSequence;
        const auto states = m_catalog.snapshot();
        const auto active = m_catalog.activeKey();
        const auto lastLocal = m_catalog.lastLocalKey();
        finishHistory();
        persist();
        m_saveTimer.stop();
        invalidate();
        m_engine.command(AudioEngine::Stop);
        m_track = {};
        m_sourceKind = "catalog";
        m_sourcePreference.clear();
        m_sourceFailure.clear();
        m_mvFailure.clear();
        m_mvRecovery = false;
        m_coverColor = {};
        m_coverPng.clear();
        m_privileges = {};
        m_lyricState = "idle";
        m_loaded = false;
        m_restoring = true;
        m_catalog.restore(states, active, lastLocal, true);
        m_queueTitle = m_catalog.activeTitle();
        m_sourceKey = m_catalog.activeKey();
        m_restoring = false;
        m_lyricText.clear();
        m_position = m_duration = 0;
        if (const auto *track = queue()->at(queue()->currentIndex())) {
            m_track = *track;
            m_duration = qint64(track->durationMs);
        }
        // Re-adopting the queue's track must not leave the native artwork
        // surfaces (taskbar preview, SMTC) on the placeholder: the cleared PNG
        // only comes back when the loader runs again.
        loadCurrentArtwork();
        m_error.clear();
        m_playing = m_loading = false;
        publishMedia();
        m_revision = m_api.sessionRevision();
        m_account = m_api.currentAccountUserId();
        m_restoreSequence = m_sequence;
        m_queueRestorePending = restorePending;
        if (m_repository && restorePending)
            m_repository->load(PlaybackRepository::SharedQueueScope, m_restoreSequence);
        else
            persist();
        // Account-owned history is never shared across sessions.
        emit historyLoaded({});
        requestHistory();
        emit changed();
        emit timelineReset();
        emit positionChanged();
    });
    connect(&m_engine, &AudioEngine::eventReceived, this, &PlaybackService::onEvent);
    connect(&m_engine, &AudioEngine::closed, this, [this] {
        if (++m_closedWorkers == (m_repository ? 5 : 4))
            emit closed();
    });
    syncClock(true);
}
PlaybackService::~PlaybackService()
{
    shutdown();
}
void PlaybackService::syncClock(bool sample, const QString &reason)
{
    if (m_clock.snapshot().value("trackSeq").toString() != QString::number(m_sequence) ||
        m_loading || !hasTrack())
        m_clockSeeking = false;
    const auto busy = m_clockSeeking ? ProgressSeeking : progressBusyReason();
    const auto state = !hasTrack()                 ? "idle"
                       : !m_error.isEmpty()        ? "error"
                       : m_loading                 ? "loading"
                       : busy == ProgressSeeking   ? "seeking"
                       : busy == ProgressBuffering ? "buffering"
                       : m_playing                 ? "playing"
                                                   : "paused";
    QVariantMap values{{"trackId", trackId()},
                       {"trackSeq", QString::number(m_sequence)},
                       {"sourceRevision", QString::number(m_sourceRevision)},
                       {"positionMs", m_position},
                       {"durationMs", m_duration},
                       {"isPlaying", m_playing},
                       {"playbackRate", speed()},
                       {"isAdvancing", hasTrack() && !m_closing && !m_loading &&
                                           m_error.isEmpty() && m_playing && busy == ProgressReady},
                       {"displayState", state},
                       {"busyReason", busy == ProgressSeeking     ? "seek"
                                      : busy == ProgressBuffering ? "buffering"
                                                                  : ""}};
    if (m_clock.observe(values, sample, reason))
        emit clockChanged();
}
void PlaybackService::publishProgressStatus()
{
    const auto reason = progressBusyReason();
    if (reason != m_publishedProgress) {
        m_publishedProgress = reason;
        emit progressBusyReasonChanged();
    }
}
void PlaybackService::invalidate(bool preserveLyrics)
{
    m_sessionController->invalidate(preserveLyrics);
}
void PlaybackService::playTracks(const PlaybackTracks &tracks, int index, const QString &title,
                                 const QString &sourceKey)
{
    m_queueController->playTracks(tracks, index, title, sourceKey);
}
void PlaybackService::playFromSelection(const PlaybackTracks &tracks, int index,
                                        const QString &title, const QString &sourceKey,
                                        bool selectedSong)
{
    m_queueController->playFromSelection(tracks, index, title, sourceKey, selectedSong);
}
void PlaybackService::queueAndPlayTrack(const PlaybackTrack &track, const QString &title,
                                        const QString &sourceKey)
{
    m_queueController->queueAndPlayTrack(track, title, sourceKey);
}
void PlaybackService::appendTracks(const PlaybackTracks &tracks, bool next)
{
    m_queueController->appendTracks(tracks, next);
}
void PlaybackService::enqueueTracks(const PlaybackTracks &tracks, bool cut)
{
    m_queueController->enqueueTracks(tracks, cut);
}
void PlaybackService::appendTracksImpl(const PlaybackTracks &tracks, bool next, bool sequential)
{
    m_queueController->appendTracksImpl(tracks, next, sequential);
}
void PlaybackService::playIndex(int index)
{
    m_queueController->playIndex(index);
}
void PlaybackService::loadCurrentArtwork()
{
    const auto source = cover();
    if (source.isEmpty()) {
        m_coverColor = QColor();
        m_coverPng.clear();
        return;
    }
    m_artwork.load(source, m_sequence);
}
void PlaybackService::startIndex(int index, bool preserveFailureChain)
{
    const auto *track = queue()->at(index);
    if (m_closing || !track)
        return;
    if (m_personalFm->active()) {
        m_personalFm->startCandidate(*track, preserveFailureChain);
        return;
    }
    startTrack(*track, index, preserveFailureChain);
}

bool PlaybackService::adoptCloudIdentity(PlaybackTrack &track) const
{
    // Unmatched cloud uploads persisted by older versions carry the placeholder
    // id "0" without a file identity. The audio hash is the identity shared by
    // cloud lists, playlists and history, so adopt it (plus the file record and
    // duration for source resolution and display) and every surface highlights
    // one song instead of two. Local files never adopt a cloud identity even if
    // the same audio exists there.
    if (track.localPath.isEmpty() && track.songId == 0 && !track.hash.isEmpty()) {
        const auto cloud = m_cloudSources.source(track);
        if (!cloud.isEmpty()) {
            if (track.catalogMetadata.value("cloud_source").toObject().isEmpty())
                track.catalogMetadata["cloud_source"] = cloud;
            if (!track.durationMs)
                track.durationMs = quint64(cloud.value("duration_ms").toDouble());
            track.id = QStringLiteral("cloud:%1").arg(track.hash.toLower());
            return true;
        }
    }
    return false;
}
PlaybackTrack PlaybackService::mergeResolvedTrack(const PlaybackTrack &track) const
{
    auto merged = track;
    adoptCloudIdentity(merged);
    if (merged.id.isEmpty() || merged.id == "0")
        merged.id = m_track.id;
    if (!merged.durationMs)
        merged.durationMs = m_track.durationMs;
    const auto cloud = m_track.catalogMetadata.value("cloud_source").toObject();
    if (!cloud.isEmpty() && merged.catalogMetadata.value("cloud_source").toObject().isEmpty())
        merged.catalogMetadata["cloud_source"] = cloud;
    return merged;
}
void PlaybackService::startTrack(const PlaybackTrack &snapshot, int index,
                                 bool preserveFailureChain)
{
    auto resolved = snapshot;
    adoptCloudIdentity(resolved);
    m_failure->started(preserveFailureChain);
    finishHistory();
    if (resolved.stableKey() != m_track.stableKey())
        m_quality = m_preferences.defaultQuality();
    m_startupAutoPlayPending = false;
    invalidate();
    m_engine.command(AudioEngine::Stop);
    if (index >= 0) {
        queue()->setCurrent(index);
        queue()->adoptIdentity(index, resolved);
    }
    m_mvRecovery = false;
    m_sourcePreference.clear();
    m_sourceFailure.clear();
    m_mvFailure.clear();
    m_sourceKind = "catalog";
    m_sourceNoticeCode.clear();
    m_sourceEffect = "none";
    m_track = resolved;
    queue()->consumePlayedPriority(resolved.id.isEmpty() ? resolved.key() : resolved.id);
    m_pendingLoudness = {};
    m_audioControls.setTrackLoudness({});
    m_coverColor = {};
    m_coverPng.clear();
    m_artwork.load(m_track.cover, m_sequence);
    m_loaded = false;
    m_privileges = {};
    m_seamless = false;
    m_effectFallback = false;
    m_appliedEffect = "none";
    m_effectError.clear();
    m_resumePosition = -1;
    m_position = 0;
    m_duration = qint64(resolved.durationMs);
    m_error.clear();
    m_playing = false;
    publishMedia();
    m_loading = true;
    m_autoplay = true;
    m_lyricText.clear();
    m_lyricState = "loading";
    m_revision = m_api.sessionRevision();
    emit changed();
    emit timelineReset();
    emit positionChanged();
    if (m_ready)
        resolve();
}
void PlaybackService::resolve()
{
    if (!hasTrack() || m_closing)
        return;
    // Local files, cloud files (including legacy snapshots that lost their
    // file record: unmatched uploads keep songId 0) carry their own identity
    // complete; catalog plays start lyrics once privileges exist.
    if (!m_track.localPath.isEmpty() || m_track.songId == 0 ||
        !m_track.catalogMetadata.value("cloud_source").toObject().isEmpty())
        requestLyrics();
    m_source.resolve(m_track, apiQuality(), apiEffect(), {}, m_preferences.compatibilityMode(),
                     m_preferences.viperQualityEnabled(),
                     m_listenTogether->roomSourceAllowed(m_track) ? m_listenTogether->roomId()
                                                                  : QString(),
                     m_sourcePreference, m_audioControls.volumeNormalization());
}
void PlaybackService::loadNextUrl()
{
    if (m_urlIndex >= m_urls.size()) {
        if (recoverSource(m_error.isEmpty() ? tr("无法加载歌曲音频") : m_error))
            return;
        m_loading = false;
        if (!m_seamless)
            m_playing = false;
        if (m_error.isEmpty())
            m_error = tr("无法加载歌曲音频");
        m_failure->failed();
        emit changed();
        return;
    }
    m_audioTrack = m_urlAudioTracks.value(m_urlIndex);
    const auto source = m_urls[m_urlIndex++];
    m_cast->prepareTransportCommand();
    if (m_cast->remoteMedia()) {
        m_progress.reset();
        publishProgressStatus();
        m_cast->loadMedia(source, m_track, (m_audioSequence = ++m_audioSerial));
        return;
    }
    if (!m_audioTrack && !m_effectFallback &&
        (m_sourceEffect == "vocal" || m_sourceEffect == "accompaniment") &&
        QUrl(source).path().endsWith(".mkv", Qt::CaseInsensitive)) {
        m_probedSource = source;
        m_trackProbe = m_native.submit(NativeBridge::AudioTracks, source, m_sequence);
        if (m_trackProbe)
            return;
        m_audioTrack = m_sourceEffect == "vocal" ? 2 : 1;
    }
    m_progress.reset();
    publishProgressStatus();
    if (m_seamless)
        m_engine.switchSource(source, m_audioSequence, m_audioTrack, ++m_sourceRevision);
    else
        m_engine.load(source, (m_audioSequence = ++m_audioSerial), m_audioTrack);
}
void PlaybackService::revokePluginMedia(const QUrl &url)
{
    if (m_transition->containsSource(url))
        m_transition->clear();
    if (m_urlIndex > 0 && m_urlIndex <= m_urls.size() && QUrl(m_urls[m_urlIndex - 1]) == url) {
        stop();
        emit notice(tr("媒体文件的访问已结束"), false);
    }
}
void PlaybackService::ensureLyrics()
{
    m_sessionController->ensureLyrics();
}
void PlaybackService::requestLyrics()
{
    m_sessionController->requestLyrics();
}
void PlaybackService::requestEmbeddedMetadata()
{
    m_sessionController->requestEmbeddedMetadata();
}
void PlaybackService::onEvent(const QJsonObject &event)
{
    m_outputController->onEvent(event);
}
void PlaybackService::mediaRouteChanged()
{
    m_outputController->mediaRouteChanged();
}
void PlaybackService::consumeOutputEvent(const QJsonObject &event)
{
    m_outputController->consumeOutputEvent(event);
}
void PlaybackService::toggle()
{
    m_failure->cancel(true);
    if (m_loading) {
        m_autoplay = !m_autoplay;
        if (m_autoplay)
            m_listenTogether->localPlayRequested();
        else
            m_listenTogether->localPauseRequested();
        if (!m_autoplay)
            m_engine.command(AudioEngine::Pause);
    } else if (m_playing)
        pause();
    else
        play();
}
void PlaybackService::play()
{
    m_listenTogether->localPlayRequested();
    m_cast->prepareTransportCommand();
    m_failure->cancel(true);
    if (!hasTrack()) {
        m_sourceKey = m_catalog.activeKey();
        m_queueTitle = m_catalog.activeTitle();
        if (m_personalFm->active())
            m_personalFm->play();
        else if (queue()->rowCount())
            playIndex(m_mode == Shuffle ? QRandomGenerator::global()->bounded(queue()->rowCount())
                                        : 0);
        return;
    }
    m_autoplay = true;
    if (m_loading)
        return;
    if (!m_loaded || !m_error.isEmpty()) {
        if (m_personalFm->active() && m_personalFm->m_resetPending) {
            m_personalFm->play();
            return;
        }
        retry();
        return;
    }
    if (m_cast->remoteMedia()) {
        m_cast->playMedia();
        return;
    }
    m_recovery->resumeRequested();
    m_engine.command(AudioEngine::Play);
}
void PlaybackService::pause()
{
    m_listenTogether->localPauseRequested();
    m_discoverQueue->cancelWait();
    m_cast->prepareTransportCommand();
    m_failure->cancel(true);
    m_recovery->cancel();
    m_listening.resetPosition();
    m_autoplay = false;
    m_sleepInhibitor.setRequired(false);
    if (m_cast->remoteMedia()) {
        m_cast->pauseMedia();
        return;
    }
    m_engine.command(AudioEngine::Pause);
}
void PlaybackService::stop()
{
    m_discoverQueue->cancelWait();
    if (m_cast->remoteMedia())
        m_cast->stopMedia();
    finishHistory();
    m_loaded = false;
    invalidate(true);
    m_autoplay = false;
    m_playing = m_loading = false;
    m_position = 0;
    m_engine.command(AudioEngine::Stop);
    emit changed();
    emit timelineReset();
    emit positionChanged();
}
void PlaybackService::seek(qint64 position)
{
    m_discoverQueue->cancelWait();
    m_listening.resetPosition();
    if (!hasTrack() || m_loading)
        return;
    m_clockSeeking = true;
    syncClock(false, "seek");
    m_listenTogether->localSeekRequested(position);
    m_transition->clear();
    m_cast->seek(qBound<qint64>(0, position, m_duration));
    if (m_cast->remoteMedia())
        return;
    m_engine.command(AudioEngine::Seek, double(qBound<qint64>(0, position, m_duration)) / 1000.);
}
void PlaybackService::previous()
{
    const auto index = m_transition->previousIndex();
    if (index >= 0)
        playIndex(index);
}
void PlaybackService::next(bool automatic)
{
    if (automatic && m_listenTogether->suppressAutomaticNext())
        return;
    if (!hasTrack())
        return;
    if (automatic)
        m_listenTogether->nativeTrackEnded();
    else
        m_listenTogether->localTrackRequested();
    // Manual clicks advance synchronously; only automatic EOF waits
    // for the prepared native transition to commit.
    if (automatic && m_transition->commit(true))
        return;
    if (m_personalFm->active()) {
        m_personalFm->advance(automatic);
        return;
    }
    const int next = m_transition->nextIndex(automatic);
    if (next < 0 && m_sourceKey == DiscoverQueueController::queueKey()) {
        m_discoverQueue->advance(automatic);
        return;
    }
    m_transition->consumeNext(next, automatic);
    if (next >= 0) {
        if (automatic)
            startIndex(next, false);
        else
            playIndex(next);
    } else if (queue()->rowCount() &&
               (m_mode == Sequential || m_sourceKey == "queue:home-discover"))
        pause();
}
void PlaybackService::toggleMute()
{
    setVolume(m_volume == 0 ? m_unmutedVolume : 0);
}
ApiClient::PlaybackQuality PlaybackService::apiQuality() const
{
    return ApiClient::PlaybackQuality(QStringList{"standard", "higher", "flac", "hi_res",
                                                  "viper_tape", "viper_clear", "viper_atmos"}
                                          .indexOf(m_quality));
}
QVariantList PlaybackService::qualities() const
{
    const QStringList keys{"standard",   "higher",      "flac",       "hi_res",
                           "viper_tape", "viper_clear", "viper_atmos"};
    const QStringList labels{tr("标准"),     tr("高品质"),   tr("无损"),      "Hi-Res",
                             tr("蝰蛇母带"), tr("蝰蛇超清"), tr("蝰蛇全景声")};
    const QStringList tags{"SD", "HQ", "SQ", "HR", "VPT", "VPC", "VPA"};
    const QStringList colors{"#64748B", "#8B5CF6", "#2563EB", "#F59E0B",
                             "#E11D48", "#0891B2", "#7C3AED"};
    QVariantList result;
    for (int i = 0; i < keys.size(); ++i) {
        if (m_track.catalogMetadata.value("source_kind").toString() == "cloud" &&
            m_track.catalogMetadata.value("cloud_source")
                .toObject()
                .value("hash_std")
                .toString()
                .isEmpty())
            continue;
        if (!m_preferences.viperQualityEnabled() && keys[i].startsWith("viper_"))
            continue;
        const auto file =
            PlaybackSourcePlan::qualityVariant(ApiClient::PlaybackQuality(i), m_privileges);
        const bool exists = !file.isEmpty();
        auto size = file.value("size_bytes");
        if (i <= 1) {
            const auto ogg = m_privileges.value(i == 0 ? "ogg_standard" : "ogg_high").toObject();
            if (ogg.value("size_bytes").isDouble())
                size = ogg.value("size_bytes");
        }
        auto reason = PlaybackSourcePlan::qualityAccessReason(
            ApiClient::PlaybackQuality(i), m_membership.facts(),
            m_preferences.viperQualityEnabled(), m_privileges);
        const bool enabled = hasTrack() && m_track.localPath.isEmpty() && reason.isEmpty();
        result.append(
            QVariantMap{{"value", keys[i]},
                        {"label", labels[i]},
                        {"tag", tags[i]},
                        {"color", colors[i]},
                        {"enabled", enabled},
                        {"detail", !reason.isEmpty() ? reason
                                   : exists && size.isDouble()
                                       ? tr("%1 MB").arg(size.toDouble() / 1024 / 1024, 0, 'f', 1)
                                       : QString()}});
    }
    const auto cloud = m_cloudSources.source(m_track);
    if (!cloud.isEmpty())
        result.prepend(QVariantMap{
            {"value", "cloud"},
            {"label", tr("云盘文件")},
            {"tag", "CLD"},
            {"color", "#0EA5E9"},
            {"enabled", hasTrack() && !m_loading},
            {"detail",
             cloud.value("size_bytes").isDouble()
                 ? tr("%1 MB").arg(double(cloud.value("size_bytes").toInteger()) / 1024 / 1024, 0,
                                   'f', 1)
                 : QString()}});
    return result;
}
void PlaybackService::setVolume(double value)
{
    if (!std::isfinite(value))
        return;
    value = qBound(0., value, m_remoteVolume ? 100. : 150.);
    if (m_volume == value)
        return;
    if (value > 0)
        m_unmutedVolume = value;
    m_volume = value;
    if (!m_remoteVolume) {
        m_settings.setValue(EchoMusic::SettingsKeys::Playback::Volume, value);
        if (m_ready)
            m_engine.command(AudioEngine::Volume, value);
    }
    emit changed();
}
void PlaybackService::applyOutputVolume(double volume)
{
    if (!m_remoteVolume) {
        m_localVolume = m_volume;
        m_localUnmutedVolume = m_unmutedVolume;
        m_remoteVolume = true;
    }
    setVolume(volume);
}
void PlaybackService::restoreLocalVolume()
{
    if (!m_remoteVolume)
        return;
    m_remoteVolume = false;
    setVolume(m_localVolume);
    m_unmutedVolume = m_localUnmutedVolume;
}
void PlaybackService::setSpeed(double value)
{
    if (m_cast->remoteMedia())
        return;
    if (!std::isfinite(value))
        return;
    value = qBound(.1, value, 5.);
    if (m_speed == value)
        return;
    m_transition->clear();
    m_speed = value;
    m_settings.setValue(EchoMusic::SettingsKeys::Playback::Speed, value);
    if (m_ready)
        m_engine.command(AudioEngine::Speed, value);
    emit changed();
}
void PlaybackService::setPlayMode(PlayMode mode)
{
    if (mode < ListLoop || mode > Sequential)
        return;
    m_transition->clear();
    m_mode = mode;
    m_settings.setValue(EchoMusic::SettingsKeys::Playback::Mode, int(mode));
    emit changed();
}
void PlaybackService::cycleMode()
{
    setPlayMode(PlayMode((int(m_mode) + 1) % 4));
}
void PlaybackService::cancelTrackProbe()
{
    if (const auto probe = std::exchange(m_trackProbe, 0))
        m_native.cancel(probe);
}
void PlaybackService::refreshCurrentSource()
{
    if (!hasTrack() || m_closing)
        return;
    // Keep catalog/lyrics identity stable; retire replacement work immediately,
    // before awaiting a new URL. A separate revision rejects old switch acks.
    m_transition->clear();
    ++m_sourceRevision;
    m_engine.cancelSourceReplacement();
    m_source.cancel();
    m_urls.clear();
    m_urlIndex = 0;
    cancelTrackProbe();
    m_audioTrack = 0;
    m_probedSource.clear();
    m_effectFallback = false;
    m_effectError.clear();
    m_error.clear();
    m_mvRecovery = false;
    m_sourceFailure.clear();
    m_mvFailure.clear();
    // EchoMusic refreshCurrentTrack only swaps seamlessly while playing.
    // Paused refresh reloads and restores position without resuming transport.
    // An MV edition may have a different timeline/codec. Load a normal source
    // through the regular transport, then restore position; a same-song PCM
    // replacement can retain the MV when its hand-off buffer is unavailable.
    m_seamless = m_loaded && m_playing && !m_cast->remoteMedia() && m_sourceKind != "mv";
    m_loading = true;
    if (!m_seamless) {
        m_resumePosition = m_position;
        m_autoplay = m_playing || m_autoplay;
    }
    m_source.resolve(m_track, apiQuality(), apiEffect(), m_privileges,
                     m_preferences.compatibilityMode(), m_preferences.viperQualityEnabled(),
                     m_listenTogether->roomSourceAllowed(m_track) ? m_listenTogether->roomId()
                                                                  : QString(),
                     m_sourcePreference, m_audioControls.volumeNormalization(), true);
    emit changed();
}
ApiClient::SoundEffect PlaybackService::apiEffect() const
{
    static const QStringList effects{"none",      "piano",   "vocal",  "accompaniment",
                                     "subwoofer", "ancient", "surnay", "dj"};
    return ApiClient::SoundEffect(qMax(0, effects.indexOf(m_songEffect)));
}
void PlaybackService::setSongEffect(const QString &effect)
{
    if (m_closing || effect == m_songEffect ||
        !QStringList{"none", "piano", "vocal", "accompaniment", "subwoofer", "ancient", "surnay",
                     "dj"}
             .contains(effect))
        return;
    m_songEffect = effect;
    m_settings.setValue(EchoMusic::SettingsKeys::Playback::SongEffect, effect);
    if (hasTrack() && m_track.localPath.isEmpty()) {
        refreshCurrentSource();
    }
    emit changed();
}
void PlaybackService::setQuality(const QString &quality)
{
    if (quality == "cloud") {
        if (!hasTrack() || m_cloudSources.source(m_track).isEmpty())
            return;
        m_sourcePreference = "cloud";
        m_failure->sourceOperation(tr("音源切换失败"));
        refreshCurrentSource();
        return;
    }
    if (!QStringList{"standard", "higher", "flac", "hi_res", "viper_tape", "viper_clear",
                     "viper_atmos"}
             .contains(quality))
        return;
    if (m_quality == quality && m_sourceKind == "catalog" && m_sourcePreference == "catalog")
        return;
    m_sourcePreference = "catalog";
    m_quality = quality;
    if (hasTrack()) {
        m_failure->sourceOperation(tr("音质切换失败"));
        refreshCurrentSource();
    }
    emit changed();
}
void PlaybackService::openLocalFiles()
{
    if (m_closing || m_localFileDialog)
        return;
    auto *dialog = new QFileDialog(nullptr, tr("添加本地音乐"), {},
                                   tr("音频文件 (*.mp3 *.flac *.wav *.m4a *.aac *.ogg *.opus "
                                      "*.aiff *.ape *.dsf *.dff *.mkv);;所有文件 (*)"));
    m_localFileDialog = dialog;
    dialog->setFileMode(QFileDialog::ExistingFiles);
    dialog->setWindowModality(Qt::ApplicationModal);
    const auto revision = m_api.sessionRevision();
    connect(dialog, &QDialog::finished, this, [this, dialog, revision](int result) {
        m_localFileDialog = nullptr;
        const auto files = dialog->selectedFiles();
        dialog->deleteLater();
        if (m_closing || revision != m_api.sessionRevision() || result != QDialog::Accepted)
            return;
        PlaybackTracks tracks;
        for (const auto &path : files) {
            PlaybackTrack track;
            track.id = path;
            track.localPath = path;
            track.title = QFileInfo(path).completeBaseName();
            tracks.append(track);
        }
        if (!tracks.isEmpty())
            playTracks(tracks, 0, tr("本地音乐"), "local");
    });
    connect(&m_api, &ApiClient::sessionChanged, dialog, &QDialog::reject);
    dialog->show();
}

void PlaybackService::retry()
{
    if (m_personalFm->active() && queue()->currentIndex() < 0) {
        m_personalFm->play();
        return;
    }
    if (queue()->currentIndex() >= 0)
        playIndex(queue()->currentIndex());
}
void PlaybackService::requestHistory()
{
    m_queueController->requestHistory();
}
void PlaybackService::removeHistory(const QStringList &keys)
{
    m_queueController->removeHistory(keys);
}
void PlaybackService::clearHistory()
{
    m_queueController->clearHistory();
}
void PlaybackService::shutdown()
{
    if (m_localFileDialog) {
        m_localFileDialog->disconnect(this);
        delete m_localFileDialog;
        m_localFileDialog = nullptr;
    }
    m_sessionController->shutdown();
}
void PlaybackService::playViewedIndex(int index)
{
    m_queueController->playViewedIndex(index);
}
void PlaybackService::resumeViewedQueue()
{
    m_queueController->resumeViewedQueue();
}
void PlaybackService::removeViewedIndex(int index)
{
    m_queueController->removeViewedIndex(index);
}
void PlaybackService::publishMedia()
{
    m_outputController->publishMedia();
}
void PlaybackService::persist()
{
    m_queueController->persist();
}
void PlaybackService::finishHistory()
{
    m_sessionController->finishHistory();
}
} // namespace EchoMusic

bool EchoMusic::PlaybackService::tryMvFallback(const QString &reason)
{
    if (m_closing || !m_preferences.mvAudioFallbackEnabled() || m_sourceKind == "mv" ||
        !m_track.localPath.isEmpty())
        return false;
    if (!m_track.songId) {
        m_mvFailure = tr("缺少歌曲关联 ID，无法查询曲库 MV");
        return false;
    }
    if (!m_source.tryMv(m_track))
        return false;
    if (m_sourceFailure.isEmpty())
        m_sourceFailure = reason;
    m_mvFailure.clear();
    m_mvRecovery = false;
    m_transition->clear();
    m_recovery->cancel();
    m_engine.cancelSourceReplacement();
    m_engine.command(AudioEngine::Stop);
    m_audioSequence = ++m_audioSerial;
    ++m_sourceRevision;
    m_loading = true;
    m_loaded = false;
    m_seamless = false;
    m_error.clear();
    emit changed();
    return true;
}
bool EchoMusic::PlaybackService::recoverSource(const QString &reason)
{
    if (m_closing || !hasTrack() || !m_track.localPath.isEmpty())
        return false;
    const bool mv = m_sourceKind == "mv";
    if (!mv) {
        if (m_sourceFailure.isEmpty())
            m_sourceFailure = reason;
        m_loading = true;
        m_loaded = false;
        m_seamless = false;
        m_resumePosition = m_position;
        if (!m_source.hasContext(m_track)) {
            m_source.resolve(m_track, apiQuality(), apiEffect(), m_privileges,
                             m_preferences.compatibilityMode(), m_preferences.viperQualityEnabled(),
                             {}, m_sourcePreference, m_audioControls.volumeNormalization());
            return true;
        }
        if (m_source.continueAfterFailure())
            return true;
        if (tryMvFallback(reason))
            return true;
    }
    m_loading = false;
    if (!m_sourceFailure.isEmpty()) {
        if (mv)
            m_mvFailure = reason;
        m_error = m_sourceFailure;
    }
    return false;
}

void EchoMusic::PlaybackService::ensureQualities()
{
    if (hasTrack() && m_track.localPath.isEmpty()) {
        if (m_track.catalogMetadata.value("cloud_source").toObject().isEmpty())
            m_cloudSources.ensure();
        if (m_privileges.isEmpty())
            m_source.ensureCatalogRights(m_track);
    }
}
