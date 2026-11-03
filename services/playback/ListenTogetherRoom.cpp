#include "ListenTogetherController.h"
#include "AccountService.h"
#include "PlaybackService.h"
#include <QDateTime>
#include <QSharedPointer>
#include <algorithm>
namespace EchoMusic
{
void ListenTogetherController::startTimers()
{
    if (!joined() || m_closed)
        return;
    connect(&m_playback, &PlaybackService::changed, this,
            &ListenTogetherController::playbackChanged, Qt::UniqueConnection);
    connect(&m_playback, &PlaybackService::positionChanged, this,
            &ListenTogetherController::positionChanged, Qt::UniqueConnection);
    m_fastTimer.start();
    m_slowTimer.start();
    m_heartbeatTimer.start();
}
void ListenTogetherController::fastPoll()
{
    if (!joined() || m_fastPending)
        return;
    m_fastPending = true;
    auto left = QSharedPointer<int>::create(3);
    const auto done = [this, left] {
        if (!--*left)
            m_fastPending = false;
    };
    loadMembers(done);
    loadMessages(done);
    syncPlayback(false, done);
}
void ListenTogetherController::slowPoll()
{
    if (!joined() || m_slowPending)
        return;
    m_slowPending = true;
    ListenMusicRequest request;
    request.action = ListenMusicAction::Detail;
    request.roomId = m_roomId;
    const auto id = m_api.listenTogetherMusic(request);
    watch(
        id, Scope::Room,
        [this](const QJsonObject &room) {
            m_activeRoom = room.toVariantMap();
            upsertOwned(m_activeRoom);
            emit changed();
        },
        [this](const QJsonObject &error) { reportError(error, false); });
    after(id, [this] { m_slowPending = false; });
    refreshOrders();
}
void ListenTogetherController::heartbeat()
{
    if (!joined() || m_heartbeatPending)
        return;
    m_heartbeatPending = true;
    ListenRoomRequest request;
    request.action = ListenRoomAction::Heartbeat;
    request.roomId = m_roomId;
    const auto id = m_api.listenTogetherRoom(request);
    watch(
        id, Scope::Room, [this](const QJsonObject &) { m_heartbeatFailures = 0; },
        [this](const QJsonObject &) {
            if (++m_heartbeatFailures == 3)
                emit notice(tr("房间连接不稳定，请检查网络"), 1);
        });
    after(id, [this] { m_heartbeatPending = false; });
}
void ListenTogetherController::loadRoomDetail()
{
    if (!joined())
        return;
    ListenMusicRequest request;
    request.action = ListenMusicAction::Detail;
    request.roomId = m_roomId;
    watch(
        m_api.listenTogetherMusic(request), Scope::Room,
        [this](const QJsonObject &room) {
            const bool wasOwner = owner();
            m_activeRoom = room.toVariantMap();
            upsertOwned(m_activeRoom);
            if (wasOwner != owner() && m_initialSongsDone)
                loadSongs(true);
            emit changed();
        },
        [this](const QJsonObject &error) { reportError(error, false); });
}
void ListenTogetherController::loadMembers(std::function<void()> terminal)
{
    if (!joined()) {
        if (terminal)
            terminal();
        return;
    }
    if (m_memberRequest) {
        after(m_memberRequest, terminal);
        return;
    }
    ListenMusicRequest request;
    request.action = ListenMusicAction::Members;
    request.roomId = m_roomId;
    const auto id = m_memberRequest = m_api.listenTogetherMusic(request);
    watch(
        id, Scope::Room,
        [this](const QJsonObject &data) {
            auto items = data.value("items").toArray().toVariantList();
            QHash<QString, QVariantMap> next;
            for (const auto &item : items) {
                const auto row = item.toMap();
                const auto id = row.value("id").toString();
                if (!id.isEmpty())
                    next.insert(id, row);
            }
            if (m_memberSnapshotReady) {
                QVariantList presence;
                const auto add = [&presence](const QString &id, const QVariantMap &who, int type) {
                    const auto at = QDateTime::currentSecsSinceEpoch();
                    presence.append(
                        QVariantMap{{"id", QString("presence:%1:%2:%3").arg(type).arg(id).arg(at)},
                                    {"author", who},
                                    {"sent_at_seconds", at},
                                    {"kind_code", type},
                                    {"text", type == 4001 ? QStringLiteral("加入了房间")
                                                          : QStringLiteral("离开了房间")}});
                };
                for (auto it = next.cbegin(); it != next.cend(); ++it)
                    if (!m_memberSnapshot.contains(it.key()))
                        add(it.key(), it.value(), 4001);
                for (auto it = m_memberSnapshot.cbegin(); it != m_memberSnapshot.cend(); ++it)
                    if (!next.contains(it.key()))
                        add(it.key(), it.value(), 4002);
                mergeMessages(presence);
            }
            m_memberSnapshot = next;
            m_memberSnapshotReady = true;
            const auto append = [&items, &next](const QVariantMap &who) {
                const auto id = who.value("id").toString();
                if (!id.isEmpty() && !next.contains(id)) {
                    items.prepend(who);
                    next.insert(id, who);
                }
            };
            append(m_activeRoom.value("owner").toMap());
            if (m_account)
                append({{"id", QString::number(m_api.currentAccountUserId())},
                        {"name", m_account->nickname()},
                        {"avatar_url", m_account->avatarUrl()}});
            m_members.replace(items);
            emit changed();
        },
        [this](const QJsonObject &error) { reportError(error, false); });
    after(id, [this] { m_memberRequest = 0; });
    after(id, terminal);
}
void ListenTogetherController::mergeMessages(const QVariantList &incoming)
{
    auto rows = m_messages.rows();
    QSet<QString> ids;
    for (const auto &old : rows)
        ids.insert(old.toMap().value("id").toString());
    for (const auto &value : incoming) {
        const auto row = value.toMap();
        const auto id = row.value("id").toString();
        if (!id.isEmpty() && ids.contains(id))
            continue;
        const int type = row.value("kind_code").toInt();
        bool duplicatePresence = false;
        if (type == 4001 || type == 4002)
            for (const auto &old : rows) {
                const auto other = old.toMap();
                duplicatePresence |= other.value("kind_code").toInt() == type &&
                                     other.value("author").toMap().value("id") ==
                                         row.value("author").toMap().value("id") &&
                                     qAbs(other.value("sent_at_seconds").toLongLong() -
                                          row.value("sent_at_seconds").toLongLong()) <= 30;
            }
        if (!duplicatePresence) {
            rows.append(value);
            ids.insert(id);
        }
    }
    std::stable_sort(rows.begin(), rows.end(), [](const QVariant &a, const QVariant &b) {
        return a.toMap().value("sent_at_seconds").toLongLong() <
               b.toMap().value("sent_at_seconds").toLongLong();
    });
    if (rows.size() > 200)
        rows = rows.mid(rows.size() - 200);
    m_messages.replace(rows);
}
void ListenTogetherController::loadMessages(std::function<void()> terminal)
{
    if (!joined()) {
        if (terminal)
            terminal();
        return;
    }
    if (m_messageRequest) {
        after(m_messageRequest, terminal);
        return;
    }
    ListenChatRequest request;
    request.roomId = m_roomId;
    request.maxId = m_lastMessageId;
    const auto id = m_messageRequest = m_api.listenTogetherChat(request);
    watch(
        id, Scope::Room,
        [this](const QJsonObject &data) {
            const auto messages = data.value("items").toArray();
            for (const auto &value : messages) {
                const auto id = value.toObject().value("id").toString();
                if (id.toULongLong() > m_lastMessageId.toULongLong())
                    m_lastMessageId = id;
            }
            mergeMessages(messages.toVariantList());
            emit changed();
        },
        [this](const QJsonObject &error) { reportError(error, false); });
    after(id, [this] { m_messageRequest = 0; });
    after(id, terminal);
}
void ListenTogetherController::sendMessage(const QString &raw)
{
    const auto text = raw.trimmed();
    if (!joined() || m_sendingMessage || sendCooldownSeconds() > 0 || text.isEmpty() ||
        text.size() > 200 || m_activeRoom.value("chat_code").toInt() != 1)
        return;
    ListenChatRequest request;
    request.action = ListenChatAction::Send;
    request.roomId = m_roomId;
    request.text = text;
    if (m_account) {
        request.nickname = m_account->nickname();
        request.avatarUrl = m_account->avatarUrl();
    }
    m_sendingMessage = true;
    emit changed();
    const auto id = m_api.listenTogetherChat(request);
    watch(
        id, Scope::Room,
        [this](const QJsonObject &) {
            emit messageSent();
            loadMessages();
        },
        [this](const QJsonObject &error) { reportError(error); });
    after(id, [this] {
        m_sendingMessage = false;
        emit changed();
    });
}
void ListenTogetherController::setChatEnabled(bool enabled)
{
    if (!owner() || m_updatingChat)
        return;
    m_updatingChat = true;
    emit changed();
    ListenRoomRequest request;
    request.action = ListenRoomAction::UpdateChat;
    request.roomId = m_roomId;
    request.chatEnabled = enabled;
    const auto id = m_api.listenTogetherRoom(request);
    watch(
        id, Scope::Room,
        [this, enabled](const QJsonObject &) {
            m_activeRoom.insert("chat_code", enabled ? 1 : 2);
            emit changed();
        },
        [this](const QJsonObject &error) { reportError(error); });
    after(id, [this] {
        m_updatingChat = false;
        emit changed();
    });
}
void ListenTogetherController::refreshSongs()
{
    loadSongs(true);
}
void ListenTogetherController::loadSongs(bool force)
{
    if (!joined() || m_loadingSongs)
        return;
    if (!force && m_playbackRetry.value("playlist", -1) > m_retryClock.elapsed())
        return;
    cancelMetadata();
    ++m_songGeneration;
    m_loadingSongs = true;
    m_songPages = {};
    m_songPageVersion.clear();
    emit changed();
    if (owner())
        loadSongsPage({}, m_songGeneration);
    else {
        ListenMusicRequest request;
        request.action = ListenMusicAction::RecentPlaylist;
        request.roomId = m_roomId;
        const auto generation = m_songGeneration;
        watch(
            m_api.listenTogetherMusic(request), Scope::Room,
            [this, generation](const QJsonObject &data) { acceptSongPage(data, generation, true); },
            [this, generation](const QJsonObject &error) {
                if (generation != m_songGeneration)
                    return;
                m_loadingSongs = false;
                m_initialSongsDone = true;
                reportError(error, false);
                finishHydration();
                emit changed();
            });
    }
}
void ListenTogetherController::loadSongsPage(const std::optional<ListenRoomAudio> &cursor,
                                             quint64 generation)
{
    ListenMusicRequest request;
    request.action = ListenMusicAction::Playlist;
    request.roomId = m_roomId;
    request.audio = cursor;
    watch(
        m_api.listenTogetherMusic(request), Scope::Room,
        [this, generation](const QJsonObject &data) { acceptSongPage(data, generation, false); },
        [this, generation](const QJsonObject &error) {
            if (generation != m_songGeneration)
                return;
            if (error.value("code").toVariant().toInt() == 30009) {
                if (m_songPages.isEmpty())
                    m_playbackRetry.insert("playlist", m_retryClock.elapsed() + 60000);
            } else
                reportError(error, false);
            m_loadingSongs = false;
            m_initialSongsDone = true;
            finishHydration();
            emit changed();
        });
}
void ListenTogetherController::acceptSongPage(const QJsonObject &data, quint64 generation,
                                              bool recent)
{
    if (generation != m_songGeneration)
        return;
    const auto incoming = data.value("items").toArray();
    const auto version = data.value("version").toString();
    if (!m_songPageVersion.isEmpty() && !version.isEmpty() && version != m_songPageVersion) {
        m_loadingSongs = false;
        loadSongs(true);
        return;
    }
    if (!version.isEmpty())
        m_songPageVersion = version;
    QSet<QString> keys;
    for (const auto &value : m_songPages)
        keys.insert(value.toObject().value("song").toObject().value("hash").toString().toLower());
    int appended = 0;
    for (const auto &value : incoming) {
        const auto key =
            value.toObject().value("song").toObject().value("hash").toString().toLower();
        if (!key.isEmpty() && keys.contains(key))
            continue;
        m_songPages.append(value);
        if (!key.isEmpty())
            keys.insert(key);
        ++appended;
    }
    if (!version.isEmpty())
        m_version = version;
    publishSongs();
    completeSongs();
    applyRemote(m_loadingRoom);
    m_initialSongsDone = true;
    finishHydration();
    emit changed();
    const bool quantityReached = data.value("quantity").toInteger() > 0 &&
                                 m_songPages.size() >= data.value("quantity").toInteger();
    if (!recent && incoming.size() == 50 && appended && !quantityReached &&
        m_songPages.size() < 1000) {
        const auto facts = incoming.last().toObject().value("room").toObject();
        ListenRoomAudio cursor{facts.value("original_hash").toString(),
                               facts.value("original_song_id").toString()};
        if (!cursor.hash.isEmpty() && !cursor.songId.isEmpty()) {
            loadSongsPage(cursor, generation);
            return;
        }
    }
    m_loadingSongs = false;
    emit changed();
}
void ListenTogetherController::publishSongs()
{
    m_tracks.clear();
    m_roomFacts.clear();
    QVariantList rows;
    for (const auto &value : m_songPages) {
        const auto item = value.toObject();
        auto track = PlaybackTrack::fromDomain(item.value("song").toObject());
        const auto facts = item.value("room").toObject();
        m_roomFacts.insert(track.key().toLower(), facts);
        auto row = item.value("song").toObject().toVariantMap();
        row.insert("key", track.key());
        row.insert("artist", track.artist);
        row.insert("room", facts.toVariantMap());
        rows.append(row);
        m_tracks.append(track);
    }
    m_songs.replace(rows);
    updateQueue();
    emit changed();
}
void ListenTogetherController::refreshOrders()
{
    if (!joined() || m_orderRequest)
        return;
    ListenMusicRequest request;
    request.action = ListenMusicAction::SongOrders;
    request.roomId = m_roomId;
    m_loadingOrders = true;
    const auto id = m_orderRequest = m_api.listenTogetherMusic(request);
    emit changed();
    watch(
        id, Scope::Room,
        [this](const QJsonObject &data) {
            QVariantList rows;
            for (const auto &value : data.value("items").toArray()) {
                auto row = value.toObject().toVariantMap();
                const auto item = value.toObject().value("song").toObject();
                const auto track = PlaybackTrack::fromDomain(item.value("song").toObject());
                row.insert("key", track.key() + "/" +
                                      row.value("requester").toMap().value("id").toString());
                row.insert("catalog", item.value("song").toObject().toVariantMap());
                row.insert("artist", track.artist);
                rows.append(row);
            }
            m_orders.replace(rows);
        },
        [this](const QJsonObject &error) { reportError(error, false); });
    after(id, [this] {
        m_orderRequest = 0;
        m_loadingOrders = false;
        emit changed();
    });
}
void ListenTogetherController::addSongs(const QVariantList &songs)
{
    PlaybackTracks tracks;
    for (const auto &value : songs)
        tracks.append(PlaybackTrack::fromDomain(QJsonObject::fromVariantMap(value.toMap())));
    addTracks(tracks);
}
void ListenTogetherController::addTracks(const PlaybackTracks &tracks, const QString &requester)
{
    if (!owner() || m_addingSongs)
        return;
    ListenMusicRequest request;
    request.action = ListenMusicAction::AddSongs;
    request.roomId = m_roomId;
    request.version = m_version;
    request.requesterId = requester;
    request.progress = currentProgress();
    QSet<QString> seen;
    for (const auto &track : tracks) {
        if (!track.songId || track.hash.isEmpty() || seen.contains(track.hash.toLower()))
            continue;
        seen.insert(track.hash.toLower());
        request.audios.append(trackAudio(track));
        if (request.audios.size() == 50)
            break;
    }
    if (request.audios.isEmpty())
        return;
    m_addingSongs = true;
    emit changed();
    const auto id = m_api.listenTogetherMusic(request);
    watch(
        id, Scope::Room,
        [this](const QJsonObject &data) {
            if (!data.value("version").toString().isEmpty())
                m_version = data.value("version").toString();
            loadSongs(true);
            refreshOrders();
            emit notice(tr("已添加到房间"), 0);
            emit songsSubmitted();
        },
        [this](const QJsonObject &error) { reportError(error); });
    after(id, [this] {
        m_addingSongs = false;
        emit changed();
    });
}
void ListenTogetherController::requestSong(const QVariantMap &song)
{
    if (!joined() || m_addingSongs)
        return;
    const auto track = PlaybackTrack::fromDomain(QJsonObject::fromVariantMap(song));
    if (!track.songId || track.hash.isEmpty())
        return;
    if (owner()) {
        addTracks({track});
        return;
    }
    ListenMusicRequest request;
    request.action = ListenMusicAction::OrderSong;
    request.roomId = m_roomId;
    request.audio = trackAudio(track);
    m_addingSongs = true;
    const auto id = m_api.listenTogetherMusic(request);
    emit changed();
    watch(
        id, Scope::Room,
        [this](const QJsonObject &) {
            refreshOrders();
            emit notice(tr("点歌成功，等待房主添加"), 0);
            emit songsSubmitted();
        },
        [this](const QJsonObject &error) { reportError(error); });
    after(id, [this] {
        m_addingSongs = false;
        emit changed();
    });
}
void ListenTogetherController::approveOrder(const QString &key)
{
    if (!owner())
        return;
    for (const auto &value : m_orders.rows()) {
        const auto row = value.toMap();
        if (row.value("key").toString() != key)
            continue;
        addTracks(
            {PlaybackTrack::fromDomain(QJsonObject::fromVariantMap(row.value("catalog").toMap()))},
            row.value("requester").toMap().value("id").toString());
        return;
    }
}
void ListenTogetherController::rejectOrder(const QString &key)
{
    if (!owner() || m_addingSongs)
        return;
    for (const auto &value : m_orders.rows()) {
        const auto row = value.toMap();
        if (row.value("key").toString() != key)
            continue;
        ListenMusicRequest request;
        request.action = ListenMusicAction::RemoveSong;
        request.roomId = m_roomId;
        request.audio = trackAudio(
            PlaybackTrack::fromDomain(QJsonObject::fromVariantMap(row.value("catalog").toMap())));
        request.requesterId = row.value("requester").toMap().value("id").toString();
        m_addingSongs = true;
        const auto id = m_api.listenTogetherMusic(request);
        emit changed();
        watch(
            id, Scope::Room, [this](const QJsonObject &) { refreshOrders(); },
            [this](const QJsonObject &error) { reportError(error); });
        after(id, [this] {
            m_addingSongs = false;
            emit changed();
        });
        return;
    }
}
} // namespace EchoMusic
