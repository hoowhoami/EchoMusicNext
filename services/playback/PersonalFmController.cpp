#include "SettingsKeys.h"
#include "PersonalFmController.h"
#include "PlaybackService.h"
#include "PlaybackFailureController.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <algorithm>
namespace EchoMusic
{
namespace
{
QString identity(const PlaybackTrack &track)
{
    return track.id.isEmpty() ? track.hash.toLower() : track.id;
}
} // namespace
PersonalFmController::PersonalFmController(PlaybackService &playback, ApiClient &api,
                                           QSettings &settings)
    : QObject(&playback), m_playback(playback), m_api(api), m_settings(settings)
{
    m_mode = qBound(0, settings.value(EchoMusic::SettingsKeys::PersonalFm::Mode, 0).toInt(), 3);
    m_pool = qBound(0, settings.value(EchoMusic::SettingsKeys::PersonalFm::SongPool, 0).toInt(), 2);
    connect(&api, &ApiClient::succeeded, this, &PersonalFmController::succeeded);
    connect(&api, &ApiClient::failed, this, [this](quint64 id, int, const QByteArray &bytes) {
        failed(id, QJsonDocument::fromJson(bytes).object().value("message").toString(
                       tr("私人 FM 加载失败")));
    });
    connect(&api, &ApiClient::sessionChanged, this, &PersonalFmController::resetSession);
    connect(&playback, &PlaybackService::changed, this, &PersonalFmController::playbackChanged);
    connect(playback.queueCatalog(), &PlaybackQueueCatalog::transientQueueRemoved, this,
            [this](const QString &key) {
                if (key == queueKey())
                    abandonQueue();
            });
}
PersonalFmController::~PersonalFmController()
{
    shutdown();
}
bool PersonalFmController::active() const
{
    return m_playback.sourceKey() == queueKey();
}
bool PersonalFmController::signedIn() const
{
    return m_api.currentAccountUserId() != 0;
}
bool PersonalFmController::busy() const
{
    const auto pending = m_requests.constFind(m_resetRequest);
    const bool changing =
        pending != m_requests.cend() && (!pending->preserveQueue || m_playAfterReset);
    return changing || m_waiting || (active() && m_playback.loading());
}
QString PersonalFmController::title() const
{
    return QStringList{tr("红心 Radio"), tr("小众 Radio"), tr("速览 Radio"), tr("电台 Radio")}
        .value(m_mode);
}
QString PersonalFmController::subtitle() const
{
    return QStringList{tr("猜你喜欢"), tr("小众推荐"), tr("速览推荐"), tr("电台推荐")}.value(
        m_mode);
}
QString PersonalFmController::poolLabel() const
{
    return QStringList{tr("根据口味"), tr("根据风格"), tr("探索")}.value(m_pool);
}
PlaybackQueueModel *PersonalFmController::model() const
{
    return m_playback.queueCatalog()->modelForKey(queueKey());
}
const PlaybackTrack *PersonalFmController::displayTrack() const
{
    auto *queue = model();
    if (queue) {
        if (const auto *track = queue->at(queue->currentIndex()))
            return track;
        if (const auto *track = queue->at(queue->rowCount() - 1))
            return track;
    }
    return m_buffer.isEmpty() ? nullptr : &m_buffer.first();
}
QVariantMap PersonalFmController::presentation(const PlaybackTrack &track)
{
    const auto metadata = track.catalogMetadata;
    const auto fm = metadata.value("personal_fm").toObject();
    QString quality;
    for (const auto &value : metadata.value("related_goods_extension").toArray()) {
        const auto key = value.toObject().value("quality").toString();
        const auto label = key == "320"    ? QStringLiteral("HQ")
                           : key == "flac" ? QStringLiteral("SQ")
                           : key == "high" ? QStringLiteral("Hi-Res")
                                           : QString();
        if (!label.isEmpty())
            quality = label;
    }
    return {
        {"id", track.id},
        {"title", track.title},
        {"artist", track.artist},
        {"album", track.album},
        {"cover", track.cover},
        {"duration", track.durationMs
                         ? QString::number(track.durationMs / 60000) + ":" +
                               QString::number(track.durationMs / 1000 % 60).rightJustified(2, '0')
                         : QString()},
        {"reason", metadata.value("recommendation").toObject().value("headline").toString()},
        {"language", metadata.value("language").toString()},
        {"similarity", fm.value("similarity").toString()},
        {"quality", quality}};
}
QVariantMap PersonalFmController::current() const
{
    const auto *track = displayTrack();
    return track ? presentation(*track) : QVariantMap{};
}
QVariantList PersonalFmController::sideTracks() const
{
    QVariantList result;
    QSet<QString> seen;
    if (const auto *track = displayTrack())
        seen.insert(identity(*track));
    const auto add = [&](const PlaybackTrack &track) {
        const auto key = identity(track);
        if (seen.contains(key) || result.size() >= 3)
            return;
        seen.insert(key);
        result.append(presentation(track));
    };
    for (const auto &track : m_buffer) {
        if (result.size() >= 3)
            break;
        add(track);
    }
    if (const auto *queue = model())
        for (auto it = queue->tracks().crbegin();
             it != queue->tracks().crend() && result.size() < 3; ++it)
            add(*it);
    return result;
}
bool PersonalFmController::currentPlaying() const
{
    const auto *track = displayTrack();
    return track && active() && m_playback.trackId() == track->id && m_playback.playing();
}
void PersonalFmController::savePreferences()
{
    m_settings.setValue(EchoMusic::SettingsKeys::PersonalFm::Mode, m_mode);
    m_settings.setValue(EchoMusic::SettingsKeys::PersonalFm::SongPool, m_pool);
}
void PersonalFmController::setPageActive(bool visible, bool networkEnabled)
{
    m_pageActive = visible;
    m_networkEnabled = networkEnabled;
    if (!visible) {
        if (const auto pending = m_requests.constFind(m_resetRequest);
            pending != m_requests.cend()) {
            m_mode = pending->previousMode;
            m_pool = pending->previousPool;
            savePreferences();
        }
        cancel(m_resetRequest);
        m_playAfterReset = false;
    } else if (signedIn() && networkEnabled &&
               (!displayTrack() || (m_resetPending && m_buffer.isEmpty())) && !m_resetRequest) {
        resetPreview(m_mode, m_pool, ApiClient::FmAction::Login, true);
    }
    emit changed();
}
void PersonalFmController::setMode(int value)
{
    if (value < 0 || value > 3 || value == m_mode || !signedIn() || busy())
        return;
    resetPreview(value, m_pool, ApiClient::FmAction::Login, false);
}
void PersonalFmController::setSongPool(int value)
{
    if (value < 0 || value > 2 || value == m_pool || !signedIn() || busy())
        return;
    resetPreview(m_mode, value, ApiClient::FmAction::ChangeSongPool, false);
}
ApiClient::FmRequest PersonalFmController::parameters(ApiClient::FmAction action,
                                                      const PlaybackTrack *track) const
{
    ApiClient::FmRequest query;
    query.mode = ApiClient::FmMode(m_mode);
    query.pool = quint32(m_pool);
    query.action = action;
    query.remaining = quint32(m_buffer.size());
    if (track) {
        query.hash = track->hash;
        const auto audio = track->catalogMetadata.value("audio_id");
        if (audio.toInteger() > 0)
            query.audioId = quint64(audio.toInteger());
        else if (track->songId)
            query.audioId = track->songId; // Source resolveSongNumericId falls back to MixSongID.
        query.mark = track->catalogMetadata.value("personal_fm")
                         .toObject()
                         .value("mark")
                         .toString()
                         .trimmed();
    }
    return query;
}
quint64 PersonalFmController::request(ApiClient::FmRequest query, Pending pending)
{
    if (m_closed || !signedIn())
        return 0;
    const auto id = m_api.personalFm(query);
    if (!id) {
        m_error = tr("私人 FM 请求未能发出");
        emit changed();
        return 0;
    }
    pending.epoch = m_epoch;
    pending.session = m_api.sessionRevision();
    auto *deadline = new QTimer(this);
    deadline->setSingleShot(true);
    pending.deadline = deadline;
    m_requests.insert(id, pending);
    connect(deadline, &QTimer::timeout, this, [this, id] {
        m_api.cancel(id);
        failed(id, tr("私人 FM 请求超时"));
    });
    deadline->start(10000);
    return id;
}
void PersonalFmController::resetPreview(int mode, int pool, ApiClient::FmAction action,
                                        bool preserveQueue)
{
    if (m_closed || !signedIn())
        return;
    // A reset revokes all old recommendation responses, including feedback.
    const auto ids = m_requests.keys();
    for (auto id : ids)
        cancel(id);
    ++m_epoch;
    if (active())
        m_playback.transition()->clear();
    m_refillAfter.invalidate();
    Pending pending;
    pending.reset = true;
    pending.pageBound = m_pageActive;
    pending.preserveQueue = preserveQueue;
    pending.previousMode = m_mode;
    pending.previousPool = m_pool;
    pending.resume = active() && !preserveQueue;
    pending.playbackSequence = m_playback.m_sequence;
    pending.source = m_playback.sourceKey();
    const auto currentTrack =
        displayTrack() ? std::optional<PlaybackTrack>(*displayTrack()) : std::nullopt;
    m_mode = mode;
    m_pool = pool;
    savePreferences();
    auto query = parameters(action, action == ApiClient::FmAction::ChangeSongPool && currentTrack
                                        ? &*currentTrack
                                        : nullptr);
    query.remaining = 0;
    m_error.clear();
    m_playAfterReset = false;
    m_resetRequest = request(query, pending);
    emit changed();
}
void PersonalFmController::cancel(quint64 id)
{
    if (!id)
        return;
    const auto found = m_requests.find(id);
    if (found == m_requests.end())
        return;
    if (found->deadline) {
        found->deadline->stop();
        found->deadline->deleteLater();
    }
    m_requests.erase(found);
    m_api.cancel(id);
    if (m_resetRequest == id)
        m_resetRequest = 0;
    if (m_refillRequest == id)
        m_refillRequest = 0;
}
void PersonalFmController::resetSession()
{
    abandonQueue(false);
    if (m_pageActive && m_networkEnabled && signedIn())
        resetPreview(m_mode, m_pool, ApiClient::FmAction::Login, true);
}
void PersonalFmController::abandonQueue(bool preserveReports)
{
    const auto ids = m_requests.keys();
    for (auto id : ids) {
        const auto pending = m_requests.value(id);
        if (preserveReports && !pending.reset && !pending.refill) {
            // The advance report was already sent. Detach its stale response
            // without aborting the actual feedback when the FM queue is removed.
            if (pending.deadline) {
                pending.deadline->stop();
                pending.deadline->deleteLater();
            }
            m_requests.remove(id);
        } else
            cancel(id);
    }
    ++m_epoch;
    m_buffer.clear();
    m_pendingTrack.reset();
    m_waiting = false;
    m_playAfterReset = false;
    m_committed = m_reported = false;
    m_resetPending = true;
    m_error.clear();
    m_refillAfter.invalidate();
    emit changed();
}
void PersonalFmController::merge(const PlaybackTracks &tracks)
{
    QSet<QString> seen;
    for (const auto &track : m_buffer)
        seen.insert(identity(track));
    for (const auto &track : tracks)
        if (!seen.contains(identity(track))) {
            seen.insert(identity(track));
            m_buffer.append(track);
        }
}
void PersonalFmController::succeeded(quint64 id, const QByteArray &bytes)
{
    const auto found = m_requests.constFind(id);
    if (found == m_requests.cend())
        return;
    const auto pending = *found;
    cancel(id);
    if (pending.epoch != m_epoch || pending.session != m_api.sessionRevision())
        return;
    const auto root = QJsonDocument::fromJson(bytes).object();
    PlaybackTracks tracks;
    for (const auto &value : root.value("songs").toArray())
        tracks.append(PlaybackTrack::fromDomain(value.toObject()));
    if (pending.reset) {
        m_buffer.clear();
        merge(tracks);
        if (!pending.preserveQueue) {
            m_resetPending = false;
            if (model())
                model()->replace({}, -1);
        }
        const bool resume = (pending.resume || m_playAfterReset) &&
                            (!pending.pageBound || m_pageActive) &&
                            pending.playbackSequence == m_playback.m_sequence &&
                            pending.source == m_playback.sourceKey();
        m_playAfterReset = false;
        m_error.clear();
        emit changed();
        if (resume)
            play();
    } else {
        merge(tracks);
        if (pending.refill)
            m_refillAfter.start();
        emit changed();
        if (m_waiting && m_waitSequence == m_playback.m_sequence &&
            m_waitSource == m_playback.sourceKey()) {
            if (decision()) {
                m_waiting = false;
                advance();
            } else if (m_requests.isEmpty()) {
                m_waiting = false;
                m_error = tr("暂时没有可播放的推荐内容");
                m_playback.stop();
                emit changed();
            }
        }
    }
}
void PersonalFmController::failed(quint64 id, const QString &message)
{
    const auto found = m_requests.constFind(id);
    if (found == m_requests.cend())
        return;
    const auto pending = *found;
    cancel(id);
    if (pending.epoch != m_epoch || pending.session != m_api.sessionRevision())
        return;
    if (pending.reset) {
        m_mode = pending.previousMode;
        m_pool = pending.previousPool;
        savePreferences();
        m_playAfterReset = false;
    }
    if (pending.refill)
        m_refillAfter.start();
    m_error = message;
    if (m_waiting && m_requests.isEmpty()) {
        m_waiting = false;
        m_playback.stop();
    }
    emit changed();
}
void PersonalFmController::retry()
{
    if (!m_resetRequest)
        resetPreview(m_mode, m_pool, ApiClient::FmAction::Login, !active());
}
std::optional<PersonalFmController::Decision> PersonalFmController::decision() const
{
    // Queued-next items in retained ordinary queues take priority over FM recommendations.
    if (active()) {
        const auto key = m_playback.queueCatalog()->priorityKey(queueKey(), m_playback.trackId());
        auto *queue = m_playback.queueCatalog()->modelForKey(key);
        const auto index = queue ? queue->peekNextPriority(m_playback.trackId()) : -1;
        if (const auto *track = queue ? queue->at(index) : nullptr)
            return Decision{*track, key, m_playback.queueCatalog()->titleForKey(key), index};
    }
    const auto *queue = model();
    if (queue && active()) {
        const auto priority = queue->peekNextPriority(m_playback.trackId());
        if (const auto *track = queue->at(priority))
            return Decision{*track, queueKey(), title(), priority};
        for (int index = queue->currentIndex() + 1; index < queue->rowCount(); ++index) {
            const auto *track = queue->at(index);
            if (track && track->playable() && track->id != m_playback.trackId())
                return Decision{*track, queueKey(), title(), index};
        }
    }
    for (const auto &track : m_buffer)
        if (track.playable() && (!active() || track.id != m_playback.trackId()))
            return Decision{track, queueKey(), title(), -1};
    return std::nullopt;
}
std::optional<PlaybackTrack> PersonalFmController::nextTrack() const
{
    const auto next = decision();
    return next ? std::optional<PlaybackTrack>(next->track) : std::nullopt;
}
void PersonalFmController::play(const QString &id)
{
    if (m_closed || !signedIn())
        return;
    if (m_resetRequest) {
        m_playAfterReset = true;
        emit changed();
        return;
    }
    if (m_resetPending) {
        if (m_buffer.isEmpty()) {
            resetPreview(m_mode, m_pool, ApiClient::FmAction::Login, true);
            m_playAfterReset = true;
            emit changed();
            return;
        }
        if (model())
            model()->replace({}, -1);
        m_resetPending = false;
    }
    const auto *shown = displayTrack();
    if (id.isEmpty() && shown && active() && m_playback.trackId() == shown->id &&
        m_playback.m_loaded) {
        m_playback.toggle();
        return;
    }
    std::optional<PlaybackTrack> selected;
    if (id.isEmpty() && shown)
        selected = *shown;
    else {
        for (const auto &track : m_buffer)
            if (track.id == id)
                selected = track;
        if (const auto *queue = model())
            for (const auto &track : queue->tracks())
                if (track.id == id)
                    selected = track;
    }
    if (selected && selected->playable()) {
        if (active() && m_playback.trackId() == selected->id && m_playback.m_loaded) {
            if (!m_playback.playing())
                m_playback.play();
            return;
        }
        startCandidate(*selected);
    } else if (id.isEmpty()) {
        resetPreview(m_mode, m_pool, ApiClient::FmAction::Login, true);
        m_playAfterReset = true;
    }
}
void PersonalFmController::startCandidate(const PlaybackTrack &track, bool preserveFailureChain)
{
    m_starting = true;
    if (m_playback.sourceKey() != queueKey()) {
        PlaybackTracks history;
        int current = -1;
        if (const auto *queue = model()) {
            history = queue->tracks();
            current = queue->currentIndex();
        }
        m_playback.finishHistory();
        m_playback.m_sourceKey = queueKey();
        m_playback.m_queueTitle = title();
        m_playback.queueCatalog()->activate(queueKey(), title(), history, current);
    }
    m_pendingTrack = track;
    m_committed = false;
    m_reported = false;
    m_error.clear();
    m_waiting = false;
    m_playback.startTrack(track, -1, preserveFailureChain);
    m_loadSequence = m_playback.m_sequence;
    m_starting = false;
    emit changed();
}
void PersonalFmController::commit(const PlaybackTrack &track)
{
    m_committing = true;
    auto *queue = model();
    if (!queue) {
        m_committing = false;
        return;
    }
    queue->append({track}, false);
    for (int row = 0; row < queue->rowCount(); ++row)
        if (identity(*queue->at(row)) == identity(track)) {
            queue->setCurrent(row);
            break;
        }
    m_buffer.removeIf(
        [&](const auto &candidate) { return identity(candidate) == identity(track); });
    m_pendingTrack.reset();
    m_committed = true;
    m_resetPending = false;
    m_reported = false;
    m_committing = false;
    refill();
    emit changed();
}
void PersonalFmController::playbackChanged()
{
    if (m_closed || m_starting || m_committing)
        return;
    if (m_pendingTrack) {
        if (!active() || m_loadSequence != m_playback.m_sequence)
            m_pendingTrack.reset();
        else if (m_playback.m_loaded && !m_playback.loading() &&
                 m_playback.errorMessage().isEmpty())
            commit(m_playback.currentTrack());
        else if (!m_playback.loading() && !m_playback.errorMessage().isEmpty()) {
            const auto failedKey = identity(*m_pendingTrack);
            m_buffer.removeIf([&](const auto &track) { return identity(track) == failedKey; });
            if (auto *queue = model())
                for (int row = queue->rowCount() - 1; row >= 0; --row)
                    if (row != queue->currentIndex() && identity(*queue->at(row)) == failedKey)
                        queue->remove(row);
            m_pendingTrack.reset();
        }
    }
    if (m_waiting &&
        (m_waitSequence != m_playback.m_sequence || m_waitSource != m_playback.sourceKey()))
        m_waiting = false;
    if (!active())
        m_committed = false;
    emit changed();
}
void PersonalFmController::refill()
{
    if (m_closed || !active() || m_refillRequest || m_resetRequest ||
        (m_refillAfter.isValid() && m_refillAfter.elapsed() < 5000))
        return;
    if (std::count_if(m_buffer.cbegin(), m_buffer.cend(),
                      [](const auto &track) { return track.playable(); }) > 4)
        return;
    Pending pending;
    pending.refill = true;
    m_refillRequest = request(parameters(ApiClient::FmAction::Play), pending);
}
void PersonalFmController::reportAdvance(bool disliked, bool natural)
{
    if (!m_committed || m_reported)
        return;
    m_reported = true;
    auto query = parameters(disliked ? ApiClient::FmAction::Garbage : ApiClient::FmAction::Play,
                            &m_playback.currentTrack());
    query.playSeconds = quint64(qMax<qint64>(0, m_playback.positionMs()) / 1000);
    query.overplay =
        !disliked && (natural || (m_playback.durationMs() > 0 &&
                                  m_playback.positionMs() >= m_playback.durationMs() - 2000));
    request(query, {});
}
void PersonalFmController::advance(bool natural, bool disliked, bool preserveFailureChain)
{
    if (!active() || m_closed)
        return;
    const auto next = decision();
    reportAdvance(disliked, natural);
    m_playback.queueCatalog()->takePriority(queueKey(), m_playback.trackId());
    if (disliked) {
        m_playback.stop();
        if (auto *queue = model()) {
            const auto row = queue->currentIndex();
            queue->setCurrent(-1);
            queue->remove(row);
        }
    }
    if (next) {
        if (next->source != queueKey()) {
            auto *queue = m_playback.queueCatalog()->modelForKey(next->source);
            const auto tracks = queue->tracks();
            const auto remaining = queue->nextKeys();
            m_playback.playTracks(tracks, next->index, next->title, next->source);
            m_playback.queue()->restoreNext(remaining);
        } else {
            if (auto *queue = model();
                queue && queue->peekNextPriority(m_playback.trackId()) == next->index)
                queue->takeNextPriority(m_playback.trackId());
            startCandidate(next->track, preserveFailureChain);
        }
    } else {
        m_waiting = true;
        m_waitSequence = m_playback.m_sequence;
        m_waitSource = m_playback.sourceKey();
        // Feedback may itself provide the successor. Replenishment is single-flight
        // and cooldown based, never a timer which repeatedly fetches empty feeds.
        refill();
        if (m_requests.isEmpty()) {
            m_waiting = false;
            m_error = tr("暂时没有可播放的推荐内容");
            m_playback.stop();
        }
        emit changed();
    }
}
QString PersonalFmController::transitionKey() const
{
    const auto next = decision();
    return QString::number(m_epoch) + "/" +
           (next ? next->source + "/" + next->track.key() : QString());
}
void PersonalFmController::adoptTransition(const PlaybackTrack &track)
{
    const auto next = decision();
    reportAdvance(false, true);
    m_playback.queueCatalog()->takePriority(queueKey(), m_playback.trackId());
    if (next && next->source != queueKey()) {
        auto *queue = m_playback.queueCatalog()->modelForKey(next->source);
        const auto tracks = queue->tracks();
        const auto remaining = queue->nextKeys();
        m_playback.m_sourceKey = next->source;
        m_playback.m_queueTitle = next->title;
        m_playback.queueCatalog()
            ->activate(next->source, next->title, tracks, next->index)
            ->restoreNext(remaining);
        m_committed = false;
    } else {
        if (auto *queue = model();
            next && queue && queue->peekNextPriority(m_playback.trackId()) == next->index)
            queue->takeNextPriority(m_playback.trackId());
        commit(track);
    }
}
void PersonalFmController::favoriteFeedback(const PlaybackTrack &track, bool favorite)
{
    // Report only the FM queue's current row, even after another queue takes
    // playback. Preview-only and unrelated rows never send feedback.
    const auto *queue = model();
    const auto *current = queue ? queue->at(queue->currentIndex()) : nullptr;
    if (!current || identity(track) != identity(*current))
        return;
    auto query = parameters(
        favorite ? ApiClient::FmAction::ClickRed : ApiClient::FmAction::CancelRed, &track);
    if (query.mark.isEmpty())
        query.mark = current->catalogMetadata.value("personal_fm")
                         .toObject()
                         .value("mark")
                         .toString()
                         .trimmed();
    request(query, {});
}

void PersonalFmController::dislike()
{
    if (busy() || !displayTrack())
        return;
    if (!active() || !m_playback.hasTrack())
        resetPreview(m_mode, m_pool, ApiClient::FmAction::Login, false);
    else
        advance(false, true);
}
void PersonalFmController::shutdown()
{
    if (m_closed)
        return;
    m_closed = true;
    const auto ids = m_requests.keys();
    for (auto id : ids)
        cancel(id);
    m_buffer.clear();
    m_pendingTrack.reset();
}
} // namespace EchoMusic
