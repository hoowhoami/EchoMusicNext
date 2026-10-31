#include "PlaybackSourceRequest.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QUrl>
#include <QRegularExpression>
#include <algorithm>
#include <QSet>
#include <QFileInfo>
#include <limits>
#include <utility>

namespace EchoMusic
{
PlaybackSourceRequest::PlaybackSourceRequest(ApiClient &api, MembershipService &membership,
                                             QObject *parent, CloudAudioIndex *cloud,
                                             PlaybackSourceExtensions *extensions)
    : QObject(parent), m_extensions(extensions), m_cloud(cloud), m_api(api),
      m_membership(membership)
{
    if (m_cloud)
        connect(m_cloud, &CloudAudioIndex::settled, this, [this] {
            if (m_waitingCloud)
                startCloud();
        });
    connect(&membership, &MembershipService::settled, this, &PlaybackSourceRequest::tryStart);
    connect(&api, &ApiClient::succeeded, this, &PlaybackSourceRequest::succeed);
    connect(&api, &ApiClient::failed, this,
            [this](quint64 id, int, const QByteArray &bytes) { fail(id, bytes); });
    connect(&api, &ApiClient::sessionChanged, this, &PlaybackSourceRequest::cancel);
}
void PlaybackSourceRequest::cancel()
{
    ++m_generation;
    delete m_extensionJob.data();
    m_extensionJob = nullptr;
    m_beforeTried = m_afterTried = m_finalTried = false;
    m_pluginStage.clear();
    m_context = false;
    m_waitingCloud = false;
    m_cloudTried = m_catalogStarted = m_exhausted = m_mvTried = false;
    m_mvHashes.clear();
    m_mvError.clear();
    m_mvUrls = {};
    m_videoResults.clear();
    const auto videoRequests = std::exchange(m_videoRequests, {});
    for (auto id : videoRequests.keys())
        m_api.cancel(id);
    m_cloudPending = {};
    m_catalogLoudness = {};
    m_kind.clear();
    m_roomResolved = false;
    m_active = m_waitingPrivileges = m_hasPlan = false;
    m_catalogRefreshTried = false;
    m_candidates.clear();
    m_candidateIndex = -1;
    for (auto *id : {&m_privilegeRequest, &m_sourceRequest, &m_roomRequest, &m_cloudRequest,
                     &m_mvRequest, &m_loudnessRequest, &m_catalogRefreshRequest}) {
        const auto pending = *id;
        *id = 0;
        if (pending)
            m_api.cancel(pending);
    }
}
void PlaybackSourceRequest::resolve(const PlaybackTrack &track, ApiClient::PlaybackQuality quality,
                                    ApiClient::SoundEffect effect, const QJsonObject &known,
                                    bool compatibility, bool viperEnabled, const QString &roomId,
                                    const QString &preference, bool normalization, bool forceReload,
                                    bool preloading)
{
    cancel();
    m_revision = m_api.sessionRevision();
    m_track = track;
    m_context = true;
    m_preference = preference;
    m_normalize = normalization;
    m_forceReload = forceReload;
    m_preloading = preloading;
    const auto cloud = track.catalogMetadata.value("cloud_source").toObject();
    m_catalogHash = track.catalogMetadata.value("source_kind").toString() == "cloud"
                        ? cloud.value("hash_std").toString()
                        : track.hash;
    m_quality = quality;
    m_effect = effect;
    m_privileges = known;
    m_compatibility = compatibility;
    m_viperEnabled = viperEnabled;
    m_lastError.clear();
    if (!track.localPath.isEmpty()) {
        emit resolved(track, {},
                      {{"urls", QJsonArray{track.localPath}},
                       {"backup_urls", QJsonArray{}},
                       {"duration_ms", double(track.durationMs)}},
                      false, {});
    } else {
        m_active = true;
        if (!roomId.isEmpty()) {
            ListenMusicRequest request;
            request.action = ListenMusicAction::PlaybackUrl;
            request.roomId = roomId;
            request.audio = ListenRoomAudio{track.hash, QString::number(track.songId)};
            m_roomRequest = m_api.listenTogetherMusic(request);
            if (m_roomRequest)
                return;
        }
        if (m_cloud && cloud.isEmpty())
            m_cloud->ensure();
        if (preference == "cloud" || (preference != "catalog" && !cloud.isEmpty()))
            startCloud();
        else
            startCatalog();
    }
}
bool PlaybackSourceRequest::fallbackFromRoom()
{
    if (!m_roomResolved || m_active || m_revision != m_api.sessionRevision())
        return false;
    m_roomResolved = false;
    m_hasPlan = false;
    m_active = true;
    startCatalog();
    return true;
}
void PlaybackSourceRequest::startCatalog()
{
    if (m_revision != m_api.sessionRevision()) {
        cancel();
        return;
    }
    if (m_extensions && !m_beforeTried) {
        m_beforeTried = true;
        runExtension("before-catalog", [this] { startCatalog(); });
        return;
    }
    m_catalogStarted = true;
    if (m_cloud && m_track.catalogMetadata.value("cloud_source").toObject().isEmpty())
        m_cloud->ensure();
    if (m_catalogHash.isEmpty()) {
        afterCatalog();
        return;
    }
    m_waitingPrivileges = m_privileges.isEmpty();
    const auto generation = m_generation;
    m_membership.ensure();
    if (generation != m_generation)
        return;
    if (m_waitingPrivileges) {
        m_privilegeRequest = m_api.songPrivileges({{m_catalogHash, m_track.albumId}});
        if (!m_privilegeRequest)
            m_waitingPrivileges = false;
    }
    tryStart();
}
void PlaybackSourceRequest::tryStart()
{
    if (!m_active || m_hasPlan || m_waitingPrivileges || m_catalogRefreshRequest ||
        m_membership.loading() || m_revision != m_api.sessionRevision())
        return;
    if (refreshMissingCatalogResource())
        return;
    m_hasPlan = true;
    preparePlan();
    requestSource();
}
bool PlaybackSourceRequest::refreshMissingCatalogResource()
{
    // A removed file can still be mapped by the authorized tracker using the
    // retained album audio ID. This is resource discovery, not an adopted SD
    // source: query its current rights and run the ordinary quality plan again.
    // Other rights failures must not turn into this extra request.
    if (m_catalogRefreshTried || m_privileges.value("resource_error").toInt() != 6 ||
        !m_privileges.value("variants").isArray() ||
        !m_privileges.value("variants").toArray().isEmpty())
        return false;
    m_catalogRefreshTried = true;
    m_catalogRefreshRequest = m_api.playbackSource(m_catalogHash, m_track.albumId, m_track.songId,
                                                   ApiClient::PlaybackQuality::Automatic);
    if (!m_catalogRefreshRequest)
        failCatalogRefresh(tr("无法发起播放地址请求"));
    return true;
}
void PlaybackSourceRequest::failCatalogRefresh(const QString &message)
{
    m_waitingPrivileges = false;
    m_hasPlan = true;
    m_lastError = message;
    afterCatalog();
}
void PlaybackSourceRequest::preparePlan()
{
    m_candidates =
        PlaybackSourcePlan::catalog(m_catalogHash, m_privileges, m_quality, m_effect,
                                    m_compatibility, m_viperEnabled, m_membership.facts());
    m_lastError = PlaybackSourcePlan::qualityAccessReason(m_quality, m_membership.facts(),
                                                          m_viperEnabled, m_privileges);
    m_candidateIndex = -1;
}
void PlaybackSourceRequest::requestSource()
{
    while (++m_candidateIndex < m_candidates.size()) {
        const auto &candidate = m_candidates[m_candidateIndex];
        m_sourceRequest =
            m_api.playbackSource(candidate.hash, m_track.albumId, m_track.songId, candidate.quality,
                                 {}, {}, candidate.effect, candidate.originPage);
        if (m_sourceRequest)
            return;
        m_lastError = tr("无法发起播放地址请求");
    }
    afterCatalog();
}
void PlaybackSourceRequest::succeed(quint64 id, const QByteArray &bytes)
{
    if (m_revision != m_api.sessionRevision())
        return;
    if (id && id == m_catalogRefreshRequest) {
        m_catalogRefreshRequest = 0;
        const auto source = QJsonDocument::fromJson(bytes).object();
        const auto hash = source.value("hash").toString();
        static const QRegularExpression validHash(QStringLiteral("^[0-9a-fA-F]{32}$"));
        if (usableUrls(source).isEmpty() || !validHash.match(hash).hasMatch() ||
            hash.compare(m_catalogHash, Qt::CaseInsensitive) == 0) {
            failCatalogRefresh(tr("播放地址响应不完整"));
            return;
        }
        // Preserve the playlist/queue identity. Only this resolver's catalog
        // resource changes, and no signed locator URL is retained or published.
        m_catalogHash = hash;
        m_privileges = {};
        m_waitingPrivileges = true;
        m_privilegeRequest = m_api.songPrivileges({{m_catalogHash, m_track.albumId}});
        if (!m_privilegeRequest) {
            m_waitingPrivileges = false;
            tryStart();
        }
        return;
    }
    if (id && id == m_loudnessRequest) {
        m_loudnessRequest = 0;
        const auto volume = QJsonDocument::fromJson(bytes).object();
        for (const auto &key : {"loudness", "peak", "gain"})
            m_cloudPending[key] = volume.value(key);
        publish(m_cloudPending, "cloud");
        m_cloudPending = {};
        return;
    }
    if (m_videoRequests.contains(id)) {
        const auto ordinal = m_videoRequests.take(id);
        auto source = QJsonDocument::fromJson(bytes).object();
        source["urls"] = QJsonArray{source.value("url")};
        m_videoResults[ordinal] = usableUrls(source);
        if (m_videoRequests.isEmpty())
            nextMvUrl();
        return;
    }
    if (id && id == m_cloudRequest) {
        m_cloudRequest = 0;
        const auto source = QJsonDocument::fromJson(bytes).object();
        if (usableUrls(source).isEmpty()) {
            if (!m_catalogStarted)
                startCatalog();
            else
                finishFailure();
        } else
            publishCloud(source);
        return;
    }
    if (id && id == m_mvRequest) {
        m_mvRequest = 0;
        const auto videos = QJsonDocument::fromJson(bytes).array();
        m_mvHashes = mvHashes(videos);
        if (m_mvHashes.isEmpty()) {
            const bool allShort =
                !videos.isEmpty() && std::all_of(videos.begin(), videos.end(), [](const auto &v) {
                    const auto shortCode =
                        v.toObject().value("audio_fallback_facts").toObject().value("short_code");
                    return shortCode.isDouble() && shortCode.toInt() != 0;
                });
            m_mvError = allShort ? tr("关联视频均被平台标记为短视频，未找到可用 MV")
                                 : tr("未找到符合条件的曲库 MV");
            finishFailure();
        } else
            nextMvUrl();
        return;
    }
    if (id && id == m_roomRequest) {
        m_roomRequest = 0;
        auto source = QJsonDocument::fromJson(bytes).object();
        QJsonArray usable;
        const QRegularExpression privateFormat(QStringLiteral("\\.(?:kgs|kgm|kgma|kgg|vpr)$"),
                                               QRegularExpression::CaseInsensitiveOption);
        for (const auto &value : source.value("urls").toArray()) {
            const auto url = QUrl(value.toString());
            if ((url.scheme() == "http" || url.scheme() == "https") &&
                !privateFormat.match(QUrl::fromPercentEncoding(url.path().toUtf8())).hasMatch())
                usable.append(value);
        }
        if (usable.isEmpty()) {
            startCatalog();
            return;
        }
        source.insert("urls", usable);
        source.insert("backup_urls", QJsonArray{});
        source.insert("gain", source.value("volume_gain"));
        source.insert("peak", source.value("volume_peak"));
        m_active = false;
        m_roomResolved = true;
        emit resolved(m_track, {}, source, false, QStringLiteral("standard"));
        return;
    }
    if (id && id == m_privilegeRequest) {
        m_privilegeRequest = 0;
        const auto resources = QJsonDocument::fromJson(bytes).array();
        if (!resources.isEmpty()) {
            const auto resource = resources.first().toObject();
            if (resource.value("hash").toString().compare(m_catalogHash, Qt::CaseInsensitive) ==
                0) {
                m_privileges = resource;
                if (resource.value("song_id").toInteger() > 0)
                    m_track.songId = quint64(resource.value("song_id").toInteger());
                if (const auto album = resource.value("album_id").toString().toULongLong())
                    m_track.albumId = album;
                const auto cover =
                    resource.value("primary").toObject().value("cover_url").toString();
                if (!cover.isEmpty())
                    m_track.cover = cover;
            }
        }
        m_waitingPrivileges = false;
        const auto generation = m_generation;
        emit privilegesReady(m_track, m_privileges);
        if (generation == m_generation)
            tryStart();
    } else if (id && id == m_sourceRequest) {
        m_sourceRequest = 0;
        const auto source = QJsonDocument::fromJson(bytes).object();
        for (const auto &key : {"loudness", "peak", "gain"})
            if (!source.value(key).isNull() && !source.value(key).isUndefined())
                m_catalogLoudness[key] = source.value(key);
        if (usableUrls(source).isEmpty()) {
            m_lastError = tr("播放地址响应不完整");
            requestSource();
            return;
        }
        const auto candidate = m_candidates.value(m_candidateIndex);
        publish(source, "catalog",
                PlaybackSourcePlan::sourceQuality(source, candidate, m_privileges));
    }
}
void PlaybackSourceRequest::fail(quint64 id, const QByteArray &bytes)
{
    if (m_revision != m_api.sessionRevision())
        return;
    if (id && id == m_catalogRefreshRequest) {
        m_catalogRefreshRequest = 0;
        const auto message = QJsonDocument::fromJson(bytes).object().value("message").toString();
        failCatalogRefresh(message.isEmpty() ? tr("获取歌曲播放地址失败") : message);
        return;
    }
    if (id && id == m_loudnessRequest) {
        m_loudnessRequest = 0;
        publish(m_cloudPending, "cloud");
        m_cloudPending = {};
        return;
    }
    if (m_videoRequests.contains(id)) {
        m_videoRequests.remove(id);
        if (m_videoRequests.isEmpty())
            nextMvUrl();
        return;
    }
    if (id && id == m_cloudRequest) {
        m_cloudRequest = 0;
        if (!m_catalogStarted)
            startCatalog();
        else
            finishFailure();
        return;
    }
    if (id && id == m_mvRequest) {
        m_mvRequest = 0;
        m_mvError = tr("曲库 MV 查询失败，请稍后重试");
        finishFailure();
        return;
    }
    if (id && id == m_roomRequest) {
        m_roomRequest = 0;
        startCatalog();
        return;
    }
    if (id && id == m_privilegeRequest) {
        m_privilegeRequest = 0;
        m_waitingPrivileges = false;
        const auto generation = m_generation;
        emit privilegesReady(m_track, m_privileges);
        if (generation == m_generation)
            tryStart();
    } else if (id && id == m_sourceRequest) {
        m_sourceRequest = 0;
        const auto message = QJsonDocument::fromJson(bytes).object().value("message").toString();
        m_lastError = message.isEmpty() ? tr("获取歌曲播放地址失败") : message;
        requestSource();
    }
}
} // namespace EchoMusic

QStringList EchoMusic::PlaybackSourceRequest::usableUrls(const QJsonObject &source)
{
    QStringList result;
    for (const auto &key : {"urls", "backup_urls"})
        for (const auto &v : source.value(key).toArray()) {
            const auto text = v.toString().trimmed();
            const QUrl url(text);
            if ((url.scheme() == "http" || url.scheme() == "https" || url.scheme() == "file" ||
                 QFileInfo(text).isAbsolute()) &&
                !result.contains(text))
                result.append(text);
        }
    return result;
}
void EchoMusic::PlaybackSourceRequest::publish(QJsonObject source, const QString &kind,
                                               const QString &quality)
{
    source.insert("source_kind", source.value("source_kind").toString(kind));
    source.insert("quality", source.value("quality").toString(quality));
    const QStringList effects{"none",      "piano",   "vocal",  "accompaniment",
                              "subwoofer", "ancient", "surnay", "dj"};
    const auto nativeEffect = kind == "catalog" ? m_candidates.value(m_candidateIndex).effect
                                                : ApiClient::SoundEffect::None;
    source.insert("effect",
                  source.value("effect").toString(effects.value(int(nativeEffect), "none")));
    const auto complete = [this, kind, quality](QJsonObject candidate) {
        m_extensionJob = nullptr;
        if (candidate.isEmpty()) {
            if (kind == "catalog")
                requestSource();
            else if (kind == "cloud" && !m_catalogStarted)
                startCatalog();
            else
                finishFailure();
            return;
        }
        m_active = false;
        m_kind = kind;
        const auto fallback = candidate.value("effect").toString("none") !=
                              extensionContext().value("effect").toString();
        const auto actualQuality = candidate.value("quality").toString(quality);
        // Keep the plugin source contract's empty quality, while preventing UI
        // presentation from falling back to the requested quality as a fact.
        emit resolved(m_track, m_privileges, candidate, fallback,
                      actualQuality.isEmpty() ? QStringLiteral("unknown") : actualQuality);
    };
    if (m_extensions && (kind == "catalog" || kind == "cloud"))
        m_extensionJob = m_extensions->transform(kind, extensionContext(), source, this, complete);
    else
        complete(source);
}
void EchoMusic::PlaybackSourceRequest::startCloud()
{
    if (m_revision != m_api.sessionRevision()) {
        cancel();
        return;
    }
    if (m_cloudTried) {
        if (!m_catalogStarted)
            startCatalog();
        else
            finishFailure();
        return;
    }
    if (m_cloud && m_cloud->source(m_track).isEmpty() && m_cloud->loading()) {
        m_waitingCloud = true;
        return;
    }
    m_waitingCloud = false;
    m_cloudTried = true;
    const auto source = m_cloud ? m_cloud->source(m_track)
                                : m_track.catalogMetadata.value("cloud_source").toObject();
    if (source.isEmpty()) {
        if (!m_catalogStarted)
            startCatalog();
        else
            finishFailure();
        return;
    }
    m_cloudRequest = m_api.cloudSource(source);
    if (!m_cloudRequest) {
        if (!m_catalogStarted)
            startCatalog();
        else
            finishFailure();
    }
}
void EchoMusic::PlaybackSourceRequest::finishFailure()
{
    if (m_extensions && !m_finalTried && !m_mvTried) {
        m_finalTried = true;
        runExtension("final-fallback", [this] { finishFailure(); });
        return;
    }
    m_active = false;
    m_exhausted = true;
    m_waitingCloud = false;
    emit failed(m_mvTried && m_kind != "mv"
                    ? (m_mvError.isEmpty() ? tr("曲库 MV 音频片源均无法播放") : m_mvError)
                : m_lastError.isEmpty() ? tr("获取歌曲播放地址失败")
                                        : m_lastError);
}
bool EchoMusic::PlaybackSourceRequest::continueAfterFailure()
{
    if (!m_context || m_active || m_exhausted || m_kind == "mv" ||
        m_revision != m_api.sessionRevision())
        return false;
    m_active = true;
    if (m_roomResolved) {
        m_roomResolved = false;
        startCatalog();
    } else if (m_kind == "plugin") {
        if (m_pluginStage == "before-catalog")
            startCatalog();
        else if (m_pluginStage == "after-catalog")
            startCloud();
        else
            finishFailure();
    } else if (m_kind == "cloud") {
        if (!m_catalogStarted)
            startCatalog();
        else
            finishFailure();
    } else
        requestSource();
    return true;
}
QStringList EchoMusic::PlaybackSourceRequest::mvHashes(const QJsonArray &videos)
{
    QJsonObject selected;
    for (const auto &value : videos) {
        const auto row = value.toObject();
        if (!row.value("audio_fallback_eligible").toBool() ||
            row.value("duration_ms").toInteger() <= 0 || row.value("sources").toArray().isEmpty())
            continue;
        if (selected.isEmpty() || row.value("recommendation_code").toInt() >
                                      selected.value("recommendation_code").toInt())
            selected = row;
    }
    QList<QJsonObject> sources;
    for (const auto &value : selected.value("sources").toArray())
        sources.append(value.toObject());
    std::stable_sort(sources.begin(), sources.end(), [](const auto &a, const auto &b) {
        const auto size = [](const auto &s) {
            const auto n = s.value("size_bytes").toInteger();
            return n > 0 ? n : std::numeric_limits<qint64>::max();
        };
        return size(a) < size(b);
    });
    QSet<QString> codecs;
    QStringList hashes;
    for (const auto &source : sources) {
        const auto hash = source.value("hash").toString();
        const auto codec = source.value("codec").toString();
        if (hash.isEmpty() || codecs.contains(codec))
            continue;
        codecs.insert(codec);
        hashes.append(hash);
        if (hashes.size() == 3)
            break;
    }
    return hashes;
}
bool EchoMusic::PlaybackSourceRequest::tryMv(const PlaybackTrack &track)
{
    if (m_active || m_mvTried || m_kind == "mv" || !track.localPath.isEmpty())
        return false;
    m_mvTried = true;
    m_context = true;
    m_track = track;
    m_revision = m_api.sessionRevision();
    if (!track.songId)
        return false;
    m_active = true;
    m_mvRequest = m_api.songMv(track.songId);
    if (!m_mvRequest) {
        m_active = false;
        return false;
    }
    return true;
}
void EchoMusic::PlaybackSourceRequest::nextMvUrl()
{
    if (!m_mvHashes.isEmpty()) {
        const auto hashes = std::exchange(m_mvHashes, {});
        for (int i = 0; i < hashes.size(); ++i) {
            const auto id = m_api.videoUrl(hashes[i]);
            if (id)
                m_videoRequests.insert(id, i);
        }
        if (!m_videoRequests.isEmpty())
            return;
    }
    for (const auto &urls : m_videoResults)
        for (const auto &url : urls)
            if (!m_mvUrls.contains(url))
                m_mvUrls.append(url);
    m_videoResults.clear();
    if (m_mvUrls.isEmpty()) {
        m_mvError = tr("曲库 MV 播放地址获取失败");
        finishFailure();
        return;
    }
    publish({{"urls", m_mvUrls}, {"backup_urls", QJsonArray{}}}, "mv");
}
void EchoMusic::PlaybackSourceRequest::publishCloud(const QJsonObject &source)
{
    auto resolved = source;
    for (auto it = m_catalogLoudness.begin(); it != m_catalogLoudness.end(); ++it)
        resolved[it.key()] = it.value();
    if (m_normalize && m_catalogLoudness.isEmpty() && !m_catalogHash.isEmpty()) {
        m_cloudPending = resolved;
        m_loudnessRequest = m_api.playbackSource(m_catalogHash, m_track.albumId, m_track.songId,
                                                 ApiClient::PlaybackQuality::Automatic);
        if (m_loudnessRequest)
            return;
    }
    publish(resolved, "cloud");
}
void EchoMusic::PlaybackSourceRequest::disableMv()
{
    if (!m_mvRequest && m_videoRequests.isEmpty())
        return;
    const auto requests = std::exchange(m_videoRequests, {});
    for (auto id : requests.keys())
        m_api.cancel(id);
    m_videoResults.clear();
    for (auto *id : {&m_mvRequest, &m_loudnessRequest}) {
        const auto pending = std::exchange(*id, 0);
        if (pending)
            m_api.cancel(pending);
    }
    m_mvHashes.clear();
    m_mvUrls = {};
    finishFailure();
}
bool EchoMusic::PlaybackSourceRequest::mediaError(const QJsonObject &event)
{
    const auto code = event.value("error_code").toString();
    if (!code.isEmpty())
        return code == "decode" || code == "network";
    const auto message = event.value("message").toString();
    if (message.contains(
            QRegularExpression("superseded|cancelled|stale|output|device|dsp|filter|seek",
                               QRegularExpression::CaseInsensitiveOption)))
        return false;
    return message.contains(QRegularExpression("decode|invalid "
                                               "data|unsupported.*(codec|format)|(open|load).*?("
                                               "audio|media|source)|network|HTTP|timed out",
                                               QRegularExpression::CaseInsensitiveOption));
}

void EchoMusic::PlaybackSourceRequest::ensureCatalogRights(const PlaybackTrack &track)
{
    if (m_active || m_privilegeRequest)
        return;
    if (!hasContext(track)) {
        m_privileges = {};
        m_track = track;
        m_revision = m_api.sessionRevision();
        const auto cloud = track.catalogMetadata.value("cloud_source").toObject();
        m_catalogHash = track.catalogMetadata.value("source_kind").toString() == "cloud"
                            ? cloud.value("hash_std").toString()
                            : track.hash;
    }
    if (m_catalogHash.isEmpty() || !m_privileges.isEmpty())
        return;
    m_membership.ensure();
    m_waitingPrivileges = true;
    m_privilegeRequest = m_api.songPrivileges({{m_catalogHash, track.albumId}});
    if (!m_privilegeRequest)
        m_waitingPrivileges = false;
}

QVariantMap EchoMusic::PlaybackSourceRequest::extensionContext() const
{
    return {{"track",
             QVariantMap{
                 {"id", m_track.id},
                 {"hash", m_track.hash},
                 {"title", m_track.title},
                 {"artist", m_track.artist},
                 {"album", m_track.album},
                 {"cover", m_track.cover},
                 {"albumId", QString::number(m_track.albumId)},
                 {"songId", QString::number(m_track.songId)},
                 {"durationMs", double(m_track.durationMs)},
                 {"data", m_track.catalogMetadata.value("plugin_data").toObject().toVariantMap()},
                 {"sourceId", m_track.catalogMetadata.value("plugin_source").toString()}}},
            {"quality", PlaybackSourcePlan::qualityKey(m_quality)},
            {"effect", QStringList{"none", "piano", "vocal", "accompaniment", "subwoofer",
                                   "ancient", "surnay", "dj"}
                           .value(int(m_effect), "none")},
            {"forceReload", m_forceReload},
            {"preloading", m_preloading}};
}
void EchoMusic::PlaybackSourceRequest::runExtension(const QString &stage,
                                                    std::function<void()> fallback)
{
    const auto generation = m_generation;
    m_extensionJob = m_extensions->resolve(
        stage, extensionContext(), this,
        [this, generation, stage, fallback = std::move(fallback)](QJsonObject source) {
            if (generation != m_generation || m_revision != m_api.sessionRevision())
                return;
            m_extensionJob = nullptr;
            if (source.isEmpty()) {
                fallback();
                return;
            }
            m_pluginStage = stage;
            m_kind = "plugin";
            m_active = false;
            if (!source.contains("duration_ms") || source.value("duration_ms").toDouble() <= 0)
                source["duration_ms"] = double(m_track.durationMs);
            const auto effect = source.value("effect").toString("none");
            emit resolved(m_track, m_privileges, source,
                          effect != extensionContext().value("effect").toString(),
                          source.value("quality").toString());
        });
}
void EchoMusic::PlaybackSourceRequest::afterCatalog()
{
    if (m_extensions && !m_afterTried) {
        m_afterTried = true;
        runExtension("after-catalog", [this] { startCloud(); });
    } else
        startCloud();
}
