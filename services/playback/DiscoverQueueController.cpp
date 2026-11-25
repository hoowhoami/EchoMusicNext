#include "DiscoverQueueController.h"
#include "PlaybackService.h"
#include <QJsonDocument>
#include <utility>
namespace EchoMusic
{
DiscoverQueueController::DiscoverQueueController(PlaybackService &playback, ApiClient &api)
    : QObject(&playback), m_playback(playback), m_api(api)
{
    connect(&api, &ApiClient::succeeded, this, &DiscoverQueueController::succeeded);
    connect(&api, &ApiClient::failed, this, [this](quint64 id, int, const QByteArray &) {
        if (id == m_request)
            finish(false);
    });
    connect(&api, &ApiClient::sessionChanged, this, [this] {
        cancel();
        m_seenTrack.clear();
    });
    connect(&playback, &PlaybackService::changed, this, &DiscoverQueueController::changed);
}
DiscoverQueueController::~DiscoverQueueController()
{
    shutdown();
}
QJsonArray DiscoverQueueController::songs(const QJsonObject &domain)
{
    QJsonArray result;
    const auto items = domain["items"].toArray();
    if (items.isEmpty())
        return domain["songs"].toArray();
    for (const auto &value : items) {
        const auto item = value.toObject();
        const auto type = item["item_type"].toString();
        if (!type.isEmpty() && type != "song")
            continue;
        auto song = item.contains("song") ? item["song"].toObject() : item["song_info"].toObject();
        if (song.isEmpty())
            continue;
        if (song["alg_path"].isUndefined() && item["alg_path"].isString())
            song["alg_path"] = item["alg_path"];
        if (song["item_id"].isUndefined() && !item["item_id"].isUndefined())
            song["item_id"] = item["item_id"];
        result.append(song);
    }
    return result;
}
void DiscoverQueueController::bindFeed(const QString &key, QObject *owner,
                                       std::function<bool()> request)
{
    if (m_feedOwner != owner || m_feedKey != key) {
        cancelWait();
        m_feedBusy = false;
    }
    m_feedKey = key;
    m_feedOwner = owner;
    m_feedRequest = std::move(request);
}
void DiscoverQueueController::unbindFeed(QObject *owner)
{
    if (m_feedOwner != owner)
        return;
    m_feedOwner.clear();
    m_feedRequest = {};
    m_feedKey.clear();
    m_feedBusy = false;
    cancelWait();
}
bool DiscoverQueueController::handlesCurrent() const
{
    return m_playback.sourceKey() == queueKey() ||
           (m_feedOwner && m_feedRequest && m_playback.sourceKey() == m_feedKey);
}
void DiscoverQueueController::completeFeed(QObject *owner, bool added, const QString &error)
{
    if (m_feedOwner != owner || !handlesCurrent())
        return;
    m_feedBusy = false;
    m_seenRevision = m_playback.queue()->revision();
    finish(added, error);
}
void DiscoverQueueController::changed()
{
    if (m_closed || m_merging)
        return;
    if (m_feedOwner && m_playback.sourceKey() != m_feedKey)
        unbindFeed(m_feedOwner);
    if (!handlesCurrent()) {
        cancel();
        m_seenTrack.clear();
        return;
    }
    const auto key = m_playback.currentTrack().key();
    const auto revision = m_playback.queue()->revision();
    if (m_request && (m_queue != m_playback.queue() || m_revision != revision))
        cancel();
    if (m_seenTrack == key && m_seenRevision == revision)
        return;
    m_seenTrack = key;
    m_seenRevision = revision;
    replenish();
}
bool DiscoverQueueController::replenish()
{
    if (m_closed || !handlesCurrent())
        return false;
    auto *queue = m_playback.queue();
    int current = -1, remaining = 0;
    for (int i = 0; i < queue->rowCount(); ++i)
        if (queue->at(i)->id == m_playback.trackId())
            current = i;
    if (current < 0)
        return false;
    for (int i = current + 1; i < queue->rowCount(); ++i)
        remaining += queue->at(i)->playable();
    if (remaining > 2)
        return false;
    if (busy())
        return true;
    if (m_feedOwner && m_playback.sourceKey() == m_feedKey) {
        // The callback may finish synchronously (e.g. an exhausted/error source).
        const auto request = m_feedRequest;
        m_feedBusy = true;
        if (!request())
            m_feedBusy = false;
        return m_feedBusy;
    }
    m_session = m_api.sessionRevision();
    m_queue = queue;
    m_revision = queue->revision();
    m_attempt = 0;
    request();
    return busy();
}
void DiscoverQueueController::request()
{
    ++m_attempt;
    m_request = m_api.discoverRecommendations(4);
    if (!m_request)
        finish(false);
}
void DiscoverQueueController::advance(bool automatic, bool preserveFailureChain)
{
    m_waiting = true;
    m_waitSequence = m_playback.m_sequence;
    m_automatic = automatic;
    m_preserveFailure = preserveFailureChain;
    if (!replenish())
        finish(false);
}
void DiscoverQueueController::succeeded(quint64 id, const QByteArray &bytes)
{
    if (id != m_request)
        return;
    if (m_closed || m_session != m_api.sessionRevision() || m_playback.sourceKey() != queueKey() ||
        m_queue != m_playback.queue() || m_revision != m_playback.queue()->revision()) {
        cancel();
        return;
    }
    PlaybackTracks incoming;
    for (const auto &song : songs(QJsonDocument::fromJson(bytes).object())) {
        auto track = PlaybackTrack::fromDomain(song.toObject());
        if (track.playable())
            incoming.append(std::move(track));
    }
    auto *queue = m_playback.queue();
    const auto count = queue->rowCount();
    m_merging = true;
    queue->append(incoming, false);
    m_merging = false;
    m_revision = m_seenRevision = queue->revision();
    const bool added = queue->rowCount() > count;
    if (!added && m_attempt < 3) {
        request();
        return;
    }
    finish(added);
}
void DiscoverQueueController::finish(bool added, const QString &error)
{
    m_request = 0;
    const bool advance = std::exchange(m_waiting, false) &&
                         m_waitSequence == m_playback.m_sequence && handlesCurrent();
    emit completed(added);
    if (!advance || m_waitSequence != m_playback.m_sequence || !handlesCurrent())
        return;
    if (added) {
        const auto next = m_playback.transition()->nextIndex(m_automatic);
        if (next >= 0) {
            m_playback.transition()->consumeNext(next, m_automatic);
            m_playback.startIndex(next, m_preserveFailure);
            return;
        }
    }
    m_playback.pause();
    emit notice(error.isEmpty() ? tr("暂时没有新的推荐歌曲，请稍后重试") : error);
}
void DiscoverQueueController::cancel()
{
    if (const auto id = std::exchange(m_request, 0))
        m_api.cancel(id);
    m_waiting = false;
    m_feedBusy = false;
}
void DiscoverQueueController::shutdown()
{
    m_closed = true;
    cancel();
}
} // namespace EchoMusic
