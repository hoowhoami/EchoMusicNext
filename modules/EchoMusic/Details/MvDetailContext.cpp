#include "MvDetailContext.h"
#include "NetworkPreferences.h"
#include <QJsonDocument>
#include <QVideoFrame>
#include <QQmlEngine>
#include <algorithm>
namespace EchoMusic
{
namespace
{
// Order display choices by pixels, then bitrate. Merge equal hashes only
// for presentation; keep missing dimensions and encoding facts independent.
QJsonArray sourceChoices(const QJsonArray &sources)
{
    QList<QJsonObject> choices;
    QHash<QString, qsizetype> indices;
    for (const auto &source : sources) {
        const auto item = source.toObject();
        const auto key = item.value("hash").toString().toLower();
        if (key.isEmpty())
            continue;
        if (!indices.contains(key)) {
            indices.insert(key, choices.size());
            choices.append(item);
            continue;
        }
        auto &previous = choices[indices.value(key)];
        const auto codec = previous.value("codec").toString();
        for (auto i = item.begin(); i != item.end(); ++i)
            if (!i.value().isNull() && !(i.value().isString() && i.value().toString().isEmpty()))
                previous[i.key()] = i.value();
        if ((codec == "H.264" || codec == "H.265") &&
            (item.value("codec").toString() == "MP4" || item.value("codec").toString() == "MKV"))
            previous["codec"] = codec;
    }
    std::stable_sort(choices.begin(), choices.end(), [](const auto &left, const auto &right) {
        const auto pixels = [](const auto &v) {
            return v.value("width").toDouble() * v.value("height").toDouble();
        };
        if (pixels(left) != pixels(right))
            return pixels(left) > pixels(right);
        return left.value("bit_rate").toDouble() > right.value("bit_rate").toDouble();
    });
    QJsonArray result;
    for (const auto &choice : choices)
        result.append(choice);
    return result;
}
} // namespace
MvDetailContext::MvDetailContext(ApiClient &a, PlaybackService &p,
                                 VideoCollectionService &collection, RouteContext &r, bool network)
    : QObject(&r), m_api(a), m_playback(p), m_collection(collection), m_route(r),
      m_barrage(a, &r, network, this), m_network(network)
{
    QQmlEngine::setObjectOwnership(this, QQmlEngine::CppOwnership);
    connect(&m_barrage, &BarrageController::notice, this, &MvDetailContext::notice);
    const auto q = r.query();
    m_fallback = QJsonObject::fromVariantMap({{"title", q.value("title")},
                                              {"artist", q.value("artist")},
                                              {"cover_url", q.value("cover")}});
    m_detail = m_fallback;
    connect(&m_collection, &VideoCollectionService::mutated, this,
            [this](const QString &id, bool collected) {
                if (m_detail.value("id").toVariant().toString() != id)
                    return;
                const auto count = m_detail.value("collection_count");
                if (!count.isNull() && !count.isUndefined())
                    m_detail["collection_count"] =
                        qMax<qint64>(0, count.toInteger() + (collected ? 1 : -1));
                emit changed();
            });
    connect(&a, &ApiClient::succeeded, this, &MvDetailContext::accept);
    connect(&a, &ApiClient::failed, this,
            [this](quint64 id, int, const QByteArray &b) { fail(id, b); });
    connect(&a, &ApiClient::sessionChanged, this, [this] {
        cancel();
        releasePlayer();
        m_versions = {};
        m_sources = {};
        m_detail = m_fallback;
        if (m_route.active())
            reload();
        else
            emit changed();
    });
    connect(&a, &ApiClient::closed, this, &MvDetailContext::shutdown);
    connect(&r, &RouteContext::activeChanged, this, &MvDetailContext::activate);
    activate();
}
MvDetailContext::~MvDetailContext()
{
    shutdown();
}
void MvDetailContext::request(quint64 id, Kind kind)
{
    if (id)
        m_pending.insert(id, {kind, m_epoch, m_api.sessionRevision(), m_selection});
    else
        m_error = tr("MV 请求未能启动");
}
void MvDetailContext::cancel()
{
    ++m_epoch;
    ++m_selection;
    const auto ids = m_pending.keys();
    m_pending.clear();
    for (auto id : ids)
        m_api.cancel(id);
}
void MvDetailContext::releasePlayer()
{
    if (m_player) {
        m_player->stop();
        m_player->setSource({});
        delete m_player.data();
    }
    delete m_audio.data();
    if (m_sink)
        m_sink->setVideoFrame(QVideoFrame());
    if (m_networkPreferences && !m_mediaResource.isEmpty())
        m_networkPreferences->releaseMedia(m_mediaResource);
    m_mediaResource = QUrl();
    emit playerChanged();
    emit playbackChanged();
}
void MvDetailContext::shutdown()
{
    if (m_closed)
        return;
    m_closed = true;
    m_barrage.shutdown();
    cancel();
    releasePlayer();
    disconnect(&m_api, nullptr, this, nullptr);
}
void MvDetailContext::activate()
{
    if (m_closed)
        return;
    if (!m_route.active()) {
        cancel();
        releasePlayer();
        emit changed();
    } else
        reload();
}
void MvDetailContext::setPresented(bool v)
{
    if (m_presented == v)
        return;
    m_presented = v;
    m_barrage.setPresented(v);
    if (!v) {
        ++m_selection;
        const auto ids = m_pending.keys();
        for (auto id : ids)
            if (m_pending[id].kind == Source) {
                m_pending.remove(id);
                m_api.cancel(id);
            }
        releasePlayer();
    } else if (m_route.active() && !m_hash.isEmpty())
        selectSource(m_hash);
}
void MvDetailContext::setNetworkPreferences(NetworkPreferences *network)
{
    releasePlayer();
    m_networkPreferences = network;
}
void MvDetailContext::setVideoSink(QObject *sink)
{
    auto *next = qobject_cast<QVideoSink *>(sink);
    if (m_sink == next)
        return;
    const auto frame = m_sink ? m_sink->videoFrame() : QVideoFrame();
    if (m_sink && m_sink != next)
        m_sink->setVideoFrame(QVideoFrame());
    m_sink = next;
    if (m_player)
        m_player->setVideoSink(m_sink);
    // Fullscreen uses a second native output, sharing the same player. A
    // paused decoder need not emit another frame merely because its sink moved.
    if (m_player && m_sink && frame.isValid())
        m_sink->setVideoFrame(frame);
}
void MvDetailContext::reload()
{
    if (m_closed || !m_route.active())
        return;
    cancel();
    releasePlayer();
    m_hash.clear();
    m_barrage.setResource({}, {});
    m_error.clear();
    m_versions = {};
    m_sources = {};
    m_detail = m_fallback;
    if (m_network) {
        const auto q = m_route.query();
        const auto albumAudio = q.value("albumAudioId", q.value("mixSongId")).toULongLong();
        QString hash = q.value("hash").toString();
        QString video = q.value("videoId").toString();
        if (!albumAudio && hash.isEmpty() && m_route.resourceId().size() == 32)
            hash = m_route.resourceId();
        if (video.isEmpty() && !albumAudio && m_route.resourceId() != hash)
            video = m_route.resourceId();
        if (albumAudio)
            request(m_api.songMv(albumAudio), Versions);
        if (!video.isEmpty() && video != hash)
            request(m_api.videoDetail(video), Detail);
        if (!hash.isEmpty())
            request(m_api.videoPrivilege(hash), Privilege);
        if (!hash.isEmpty())
            selectSource(hash);
        if (m_pending.isEmpty())
            m_error = tr("暂无可用的 MV 标识");
    }
    emit changed();
}
void MvDetailContext::applyVersion()
{
    if (m_version < 0 || m_version >= m_versions.size())
        return;
    auto v = m_versions[m_version].toObject();
    m_detail = v;
    for (auto i = m_fallback.begin(); i != m_fallback.end(); ++i)
        if (m_detail.value(i.key()).isNull() || m_detail.value(i.key()).toString().isEmpty())
            m_detail[i.key()] = i.value();
    m_sources = sourceChoices(v.value("sources").toArray());
    if (!v.value("id").isNull())
        m_collection.ensureLoaded();
    QString preferred = m_route.query().value("hash").toString();
    bool found = false;
    for (const auto &s : m_sources)
        if (s.toObject().value("hash").toString() == preferred)
            found = true;
    if (!found) {
        preferred =
            m_sources.isEmpty() ? QString() : m_sources[0].toObject().value("hash").toString();
        for (const auto &source : m_sources)
            if (source.toObject().value("codec").toString() == "H.264") {
                preferred = source.toObject().value("hash").toString();
                break;
            }
    }
    if (!preferred.isEmpty()) {
        bool requested = false;
        for (const auto &pending : m_pending)
            requested |= pending.kind == Source;
        // A route hash can already be resolving while version metadata arrives.
        // Reuse that exact selection instead of issuing a second URL request.
        if (preferred != m_hash || (!m_player && !requested))
            selectSource(preferred);
    } else {
        releasePlayer();
        m_hash.clear();
    }
    emit changed();
}
void MvDetailContext::selectVersion(int v)
{
    if (v < 0 || v >= m_versions.size() || v == m_version)
        return;
    m_version = v;
    applyVersion();
}
void MvDetailContext::selectSource(const QString &hash)
{
    if (m_closed || !m_route.active() || hash.isEmpty())
        return;
    ++m_selection;
    const auto ids = m_pending.keys();
    for (auto id : ids)
        if (m_pending[id].kind == Source) {
            m_pending.remove(id);
            m_api.cancel(id);
        }
    releasePlayer();
    m_hash = hash;
    m_barrage.setResource(hash, m_detail.value("title").toString());
    m_error.clear();
    if (m_network && m_presented)
        request(m_api.videoUrl(hash), Source);
    emit changed();
}
void MvDetailContext::openPlayer(const QString &url)
{
    if (!m_presented || !m_route.active() || url.isEmpty())
        return;
    releasePlayer();
    m_audio = new QAudioOutput(this);
    m_audio->setVolume(float(m_volume));
    m_player = new QMediaPlayer(this);
    m_player->setAudioOutput(m_audio);
    m_player->setVideoSink(m_sink);
    connect(m_player, &QMediaPlayer::positionChanged, this, &MvDetailContext::playbackChanged);
    connect(m_player, &QMediaPlayer::durationChanged, this, &MvDetailContext::playbackChanged);
    connect(m_player, &QMediaPlayer::playbackStateChanged, this, [this] {
        if (playing() && m_playback.playing())
            m_playback.pause();
        emit playbackChanged();
    });
    connect(m_player, &QMediaPlayer::errorOccurred, this,
            [this](QMediaPlayer::Error, const QString &) {
                m_error = tr("视频解码失败，请切换其他片源");
                emit changed();
                emit notice(m_error, true);
            });
    if (m_playback.playing())
        m_playback.pause();
    m_mediaResource =
        m_networkPreferences ? m_networkPreferences->registerMedia(QUrl(url)) : QUrl();
    m_player->setSource(m_networkPreferences ? m_mediaResource : QUrl(url));
    emit playerChanged();
    m_player->play();
}
void MvDetailContext::toggle()
{
    if (m_player) {
        if (playing())
            m_player->pause();
        else
            m_player->play();
    }
}
void MvDetailContext::seek(qint64 p)
{
    if (m_player)
        m_player->setPosition(qBound<qint64>(0, p, m_player->duration()));
}
void MvDetailContext::setVolume(double v)
{
    m_volume = qBound(0., v, 1.);
    if (m_audio)
        m_audio->setVolume(float(m_volume));
    emit playbackChanged();
}
void MvDetailContext::accept(quint64 id, const QByteArray &bytes)
{
    if (!m_pending.contains(id))
        return;
    auto p = m_pending.take(id);
    if (m_closed || !m_route.active() || p.epoch != m_epoch ||
        p.session != m_api.sessionRevision() || (p.kind == Source && p.selection != m_selection))
        return;
    auto doc = QJsonDocument::fromJson(bytes);
    if (p.kind == Source) {
        const auto url = doc.object().value("url").toString();
        if (url.isEmpty()) {
            m_error = tr("当前视频暂时无法播放");
            emit notice(m_error, true);
        } else
            openPlayer(url);
    } else if (p.kind == Versions) {
        m_versions = doc.array();
        m_version = 0;
        const auto requested = m_route.query().value("videoId").toString();
        for (qsizetype i = 0; i < m_versions.size(); ++i)
            if (m_versions[i].toObject().value("id").toVariant().toString() == requested)
                m_version = int(i);
        applyVersion();
    } else if (p.kind == Detail) {
        const auto rows = doc.array();
        if (!rows.isEmpty()) {
            if (m_versions.isEmpty()) {
                m_versions = rows;
                m_version = 0;
                applyVersion();
            } else {
                const auto value = rows[0].toObject();
                if (value.value("id") == m_detail.value("id")) {
                    for (auto i = value.begin(); i != value.end(); ++i)
                        if (!i.value().isNull() &&
                            !(i.value().isString() && i.value().toString().isEmpty()))
                            m_detail[i.key()] = i.value();
                }
            }
        }
    } else {
        const auto rows = doc.array();
        if (!rows.isEmpty()) {
            const auto v = rows[0].toObject();
            m_detail["privileges"] = v;
            if (m_detail.value("title").toString().isEmpty())
                m_detail["title"] = v.value("name");
            if (m_detail.value("artist").toString().isEmpty())
                m_detail["artist"] = v.value("singer_name");
        }
    }
    m_barrage.setResource(m_hash, m_detail.value("title").toString());
    emit changed();
}
void MvDetailContext::fail(quint64 id, const QByteArray &bytes)
{
    if (!m_pending.contains(id))
        return;
    auto p = m_pending.take(id);
    if (p.epoch != m_epoch || p.session != m_api.sessionRevision() ||
        (p.kind == Source && p.selection != m_selection) || m_closed || !m_route.active())
        return;
    m_error = QJsonDocument::fromJson(bytes).object().value("message").toString();
    if (m_error.isEmpty())
        m_error = tr("当前视频暂时无法播放");
    emit changed();
    emit notice(m_error, true);
}
} // namespace EchoMusic
