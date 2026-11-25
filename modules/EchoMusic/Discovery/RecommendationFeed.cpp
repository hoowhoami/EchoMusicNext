#include "RecommendationFeed.h"
#include "RouteController.h"
#include "PlaybackService.h"
#include <QJsonDocument>
#include <QQmlEngine>
#include <QTimer>
#include <QRandomGenerator>
#include <QScopedValueRollback>
#include <utility>
#include <algorithm>
namespace EchoMusic
{
RecommendationFeed::RecommendationFeed(ApiClient &api, PlaybackService &playback, quint32 cardId,
                                       QString tag, QString title, QString subtitle,
                                       QObject *parent)
    : QObject(parent), m_api(api), m_playback(playback), m_actions(m_collection, playback, this),
      m_cardId(cardId), m_tag(std::move(tag)), m_title(std::move(title)),
      m_subtitle(std::move(subtitle))
{
    QQmlEngine::setObjectOwnership(this, QQmlEngine::CppOwnership);
    m_collection.enableSessionStorage();
    m_collection.setMetadataActive(false);
    m_actions.setQueueContext(m_title, queueKey());
    connect(&api, &ApiClient::succeeded, this, &RecommendationFeed::accept);
    connect(&api, &ApiClient::failed, this, [this](quint64 id, int, const QByteArray &error) {
        fail(id, ApiClient::errorMessage(error, tr("推荐歌曲加载失败")));
    });
    connect(&api, &ApiClient::sessionChanged, this, [this] {
        cancel();
        m_collection.clear();
        m_page = 1;
        m_hasMore = true;
        m_error.clear();
        m_status = 0;
        emit changed();
        updateActivity();
    });
    connect(&playback, &PlaybackService::changed, this, &RecommendationFeed::updateActivity);
    connect(&m_collection, &SongCatalogModel::storageReadyChanged, this, [this] {
        if (m_pendingRefresh && m_collection.storageReady()) {
            m_pendingRefresh = false;
            request(true);
        }
        emit changed();
        updateActivity();
    });
    connect(&m_collection, &SongCatalogModel::readFailed, this, [this](const QString &error) {
        m_error = error;
        m_status = m_collection.totalCount() ? 2 : 3;
        emit changed();
    });
    connect(&m_collection, &SongCatalogModel::cacheInvalidated, this, [this] {
        cancel();
        m_collection.clear();
        m_page = 1;
        m_hasMore = true;
        m_error.clear();
        m_status = 0;
        emit changed();
        updateActivity();
    });
}
RecommendationFeed::~RecommendationFeed()
{
    shutdown();
}
QString RecommendationFeed::key(quint32 cardId, const QString &tag)
{
    return QStringLiteral("queue:recommendation-stream:%1:%2").arg(cardId).arg(tag);
}
bool RecommendationFeed::viewed() const
{
    for (const auto &view : m_views)
        if (view && view->presented())
            return true;
    return false;
}
bool RecommendationFeed::current() const
{
    return m_playback.sourceKey() == queueKey();
}
bool RecommendationFeed::inUse() const
{
    return viewed() || current() || preparingPlay();
}
void RecommendationFeed::addViewer(RouteContext *view)
{
    if (!view || m_views.contains(view))
        return;
    m_views.append(view);
    connect(view, &RouteContext::presentationChanged, this, &RecommendationFeed::updateActivity);
    connect(view, &QObject::destroyed, this, [this] {
        m_views.removeIf([](const auto &item) { return !item; });
        updateActivity();
    });
    updateActivity();
}
void RecommendationFeed::setNetworkEnabled(bool enabled)
{
    m_network = enabled;
    updateActivity();
}
void RecommendationFeed::updateActivity()
{
    if (m_closed || m_updating)
        return;
    QScopedValueRollback<bool> guard(m_updating, true);
    const bool active = viewed() || (current() && (m_playback.playing() || m_playback.loading()));
    m_collection.setMetadataActive(m_network && active);
    if (!m_network || (!active && !preparingPlay()))
        cancel();
    if (current()) {
        if (m_request && !m_queueAtRequest) {
            m_queueAtRequest = m_playback.queue();
            m_queueRevision = m_queueAtRequest->revision();
        }
        m_playback.discoverQueue()->bindFeed(queueKey(), this, [this] { return request(); });
        if (m_network && (m_playback.playing() || m_playback.loading()))
            m_playback.discoverQueue()->replenish();
    } else
        m_playback.discoverQueue()->unbindFeed(this);
    if (viewed() && m_status == 0 && !m_request)
        request();
}
bool RecommendationFeed::request(bool refresh)
{
    if (m_closed || !m_network || (!viewed() && !current() && !preparingPlay()))
        return false;
    if (m_request || m_pendingRefresh)
        return true;
    if ((!refresh && (!m_hasMore || !m_error.isEmpty())) || !m_collection.storageReady())
        return false;
    m_refresh = refresh;
    m_error.clear();
    m_session = m_api.sessionRevision();
    m_queueAtRequest = current() ? m_playback.queue() : nullptr;
    m_queueRevision = m_queueAtRequest ? m_queueAtRequest->revision() : 0;
    m_request = m_api.recommendationFeed(m_cardId, m_tag, refresh ? 1 : m_page, 30);
    m_status = m_collection.totalCount() ? 2 : 1;
    if (!m_request) {
        m_error = tr("推荐请求未能启动");
        m_status = m_collection.totalCount() ? 2 : 3;
        m_playAfterLoad = false;
    }
    emit changed();
    return m_request != 0;
}
void RecommendationFeed::loadMore()
{
    request();
}
void RecommendationFeed::refresh()
{
    cancel();
    m_error.clear();
    m_hasMore = true;
    if (!m_collection.storageReady()) {
        m_pendingRefresh = true;
        emit changed();
    } else
        request(true);
}
void RecommendationFeed::retry()
{
    if (m_collection.retryStorage()) {
        m_error.clear();
        emit changed();
        return;
    }
    m_error.clear();
    request(m_refresh);
}
void RecommendationFeed::cancel()
{
    const bool playChanged = preparingPlay() || m_pendingRefresh;
    delete m_playRead.data();
    m_playAfterLoad = false;
    m_pendingRefresh = false;
    if (const auto id = std::exchange(m_request, 0)) {
        m_api.cancel(id);
        m_playback.discoverQueue()->unbindFeed(this);
        m_status = m_collection.totalCount() ? 2 : 0;
        emit changed();
    } else if (playChanged)
        emit changed();
}
void RecommendationFeed::accept(quint64 id, const QByteArray &bytes)
{
    if (!id || id != m_request)
        return;
    m_request = 0;
    if (m_closed || m_session != m_api.sessionRevision())
        return;
    const auto raw = QJsonDocument::fromJson(bytes).object()["songs"].toArray();
    QJsonArray rows;
    QSet<QString> seen;
    PlaybackTracks incoming;
    for (const auto &value : raw) {
        const auto song = value.toObject();
        const auto songId = song["id"].toString();
        if (songId.isEmpty() || seen.contains(songId) ||
            (!m_refresh && m_collection.sourceIndexOf(songId) >= 0))
            continue;
        seen.insert(songId);
        rows.append(song);
        auto track = PlaybackTrack::fromDomain(song);
        if (track.playable())
            incoming.append(std::move(track));
    }
    if (m_refresh)
        m_collection.replaceSongs(rows, true);
    else
        m_collection.appendSongs(rows);
    if (m_cardId == 3107)
        m_page = (m_refresh ? 1 : m_page) + 1;
    m_hasMore = !rows.isEmpty(); // A repeat-only batch stops automatic request loops.
    m_error.clear();
    m_status = 2;
    bool queued = false;
    if (current() && m_queueAtRequest && m_queueAtRequest == m_playback.queue() &&
        m_queueRevision == m_queueAtRequest->revision()) {
        const auto before = m_queueAtRequest->rowCount();
        m_playback.queueCatalog()->appendTo(queueKey(), incoming);
        queued = m_queueAtRequest->rowCount() > before;
    }
    emit changed();
    if (m_playAfterLoad)
        playLoaded();
    m_playback.discoverQueue()->completeFeed(this, queued);
    m_collection.finishCacheRecovery();
}
void RecommendationFeed::fail(quint64 id, const QString &error)
{
    if (!id || id != m_request)
        return;
    m_request = 0;
    if (m_closed || m_session != m_api.sessionRevision())
        return;
    m_error = error;
    m_status = m_collection.totalCount() ? 2 : 3;
    m_playAfterLoad = false;
    emit changed();
    m_playback.discoverQueue()->completeFeed(this, false, error);
    m_collection.finishCacheRecovery();
}
void RecommendationFeed::cancelPlay()
{
    if (!preparingPlay())
        return;
    delete m_playRead.data();
    m_playAfterLoad = false;
    emit changed();
    updateActivity();
}
void RecommendationFeed::playLoaded()
{
    m_playAfterLoad = false;
    if (m_playSequence != m_playback.clock().value("trackSeq").toULongLong()) {
        emit changed();
        return;
    }
    delete m_playRead.data();
    m_playRead = new QObject(this);
    const auto revision = m_collection.commandRevision();
    const QPointer<QObject> reader = m_playRead;
    connect(&m_collection, &SongCatalogModel::changed, m_playRead, [this, revision, reader] {
        if (reader == m_playRead && revision != m_collection.commandRevision())
            cancelPlay();
    });
    connect(&m_collection, &SongCatalogModel::readFailed, m_playRead, [this, reader] {
        if (reader == m_playRead)
            cancelPlay();
    });
    QStringList ids;
    for (int i = 0; i < m_collection.totalCount(); ++i)
        ids.append(m_collection.sourceData(i, SongCatalogModel::ResourceId).toString());
    const auto selected = m_startId;
    m_collection.requestRows(ids, reader, [this, reader, selected](PlaybackTracks tracks) {
        if (!reader || reader != m_playRead)
            return;
        reader->deleteLater();
        m_playRead.clear();
        if (m_playSequence == m_playback.clock().value("trackSeq").toULongLong() &&
            !tracks.isEmpty()) {
            int start = m_playback.playMode() == PlaybackService::Shuffle
                            ? QRandomGenerator::global()->bounded(int(tracks.size()))
                            : 0;
            if (!selected.isEmpty()) {
                start = -1;
                for (int i = 0; i < tracks.size(); ++i)
                    if (tracks[i].id == selected) {
                        start = i;
                        break;
                    }
                if (start < 0) {
                    emit changed();
                    return;
                }
            }
            m_playback.playTracks(tracks, start, m_title, queueKey());
        }
        emit changed();
    });
    emit changed();
}
void RecommendationFeed::playPreview(SongCatalogModel *source, int first, int count, int songOffset)
{
    if (current() && songOffset < 0) {
        if (!m_playback.loading())
            m_playback.toggle();
        return;
    }
    delete m_playRead.data();
    m_playSequence = m_playback.clock().value("trackSeq").toULongLong();
    m_startId.clear();
    if (m_collection.totalCount() && songOffset < 0) {
        playLoaded();
        return;
    }
    QStringList ids;
    if (source)
        for (int i = std::max(0, first);
             i < std::min(source->totalCount(), first + std::min(count, 6)); ++i)
            ids.append(source->sourceData(i, SongCatalogModel::ResourceId).toString());
    if (ids.isEmpty()) {
        m_playAfterLoad = true;
        if (!request()) {
            m_playAfterLoad = false;
            emit changed();
        }
        return;
    }
    m_playRead = new QObject(this);
    const auto revision = source->commandRevision();
    const QPointer<QObject> reader = m_playRead;
    connect(source, &SongCatalogModel::changed, m_playRead, [this, source, revision, reader] {
        if (reader == m_playRead && source->commandRevision() != revision) {
            delete m_playRead.data();
            emit changed();
        }
    });
    connect(source, &SongCatalogModel::readFailed, m_playRead,
            [this, reader](const QString &error) {
                if (reader != m_playRead)
                    return;
                delete m_playRead.data();
                m_error = error;
                emit changed();
            });
    emit changed();
    source->requestRows(ids, m_playRead, [this, songOffset](PlaybackTracks tracks) {
        m_playRead->deleteLater();
        m_playRead.clear();
        if (m_playSequence != m_playback.clock().value("trackSeq").toULongLong()) {
            emit changed();
            return;
        }
        if (songOffset >= 0) {
            if (songOffset >= tracks.size()) {
                emit changed();
                return;
            }
            m_startId = tracks[songOffset].id;
            if (current() && m_playback.trackId() == m_startId) {
                if (!m_playback.loading())
                    m_playback.toggle();
                emit changed();
                return;
            }
        }
        QJsonArray seed;
        QSet<QString> added;
        for (const auto &track : tracks)
            if (m_collection.sourceIndexOf(track.id) < 0 && !added.contains(track.id)) {
                added.insert(track.id);
                seed.append(track.toCatalog());
            }
        // Only the six explicitly played previews seed the new appendable
        // source. No complete recommendation catalog is copied on page entry.
        if (m_collection.totalCount())
            m_collection.appendSongs(seed);
        else
            m_collection.replaceSongs(seed);
        m_status = 2;
        playLoaded();
    });
}
void RecommendationFeed::shutdown()
{
    if (m_closed)
        return;
    m_closed = true;
    cancel();
    delete m_playRead.data();
    m_playAfterLoad = false;
    m_collection.setMetadataActive(false);
    m_playback.discoverQueue()->unbindFeed(this);
}
} // namespace EchoMusic
