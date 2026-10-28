#include "ListenTogetherController.h"
#include "AccountService.h"
#include "SongMetadataService.h"
#include "PlaybackService.h"
#include <QJsonDocument>
#include <QRegularExpression>
#include <algorithm>
namespace EchoMusic
{
ListenTogetherController::ListenTogetherController(PlaybackService &playback, ApiClient &api)
    : QObject(&playback), m_playback(playback), m_api(api)
{
    m_retryClock.start();
    connect(&api, &ApiClient::succeeded, this, &ListenTogetherController::succeeded);
    connect(&api, &ApiClient::failed, this,
            [this](quint64 id, int, const QByteArray &body) { failed(id, body); });
    connect(&api, &ApiClient::sessionChanged, this, &ListenTogetherController::accountChanged);
    m_fastTimer.setInterval(5000);
    m_slowTimer.setInterval(15000);
    m_heartbeatTimer.setInterval(55000);
    connect(&m_fastTimer, &QTimer::timeout, this, &ListenTogetherController::fastPoll);
    connect(&m_slowTimer, &QTimer::timeout, this, &ListenTogetherController::slowPoll);
    connect(&m_heartbeatTimer, &QTimer::timeout, this, &ListenTogetherController::heartbeat);
}
ListenTogetherController::~ListenTogetherController()
{
    shutdown();
}
void ListenTogetherController::attachAccount(AccountService &account)
{
    m_account = &account;
}
void ListenTogetherController::attachMetadata(SongMetadataService &metadata)
{
    m_metadata = &metadata;
    connect(&metadata, &SongMetadataService::completed, this,
            [this](quint64 ticket, const QJsonArray &values) {
                if (!m_metadataRequests.contains(ticket))
                    return;
                const auto generation = m_metadataRequests.take(ticket);
                if (!joined() || generation != m_songGeneration)
                    return;
                QHash<QString, QJsonObject> facts;
                for (const auto &value : values) {
                    const auto p = value.toObject();
                    facts.insert(p.value("id").toString(), p);
                }
                for (int i = 0; i < m_songPages.size(); ++i) {
                    auto item = m_songPages[i].toObject();
                    const auto song = item.value("song").toObject();
                    const auto id = song.value("id").toString();
                    if (facts.contains(id)) {
                        item.insert("song", SongMetadataService::completeSong(song, facts[id]));
                        m_songPages[i] = item;
                    }
                }
                publishSongs();
            });
}
void ListenTogetherController::cancelMetadata()
{
    if (m_metadata)
        m_metadata->unwatch(this);
    if (m_metadata)
        for (auto ticket : m_metadataRequests.keys())
            m_metadata->cancel(ticket);
    m_metadataRequests.clear();
}
void ListenTogetherController::completeSongs()
{
    if (!m_metadata)
        return;
    QList<SongMetadataService::Observation> songs;
    for (int i = 0; i < m_songPages.size(); ++i)
        songs.append({QString::number(i), m_songPages[i].toObject().value("song").toObject()});
    m_metadata->watch(
        this, songs,
        [this](const QStringList &rows) {
            if (!m_metadata || !joined())
                return;
            bool changed = false;
            for (const auto &key : rows) {
                const auto row = key.toInt();
                if (row < 0 || row >= m_songPages.size())
                    continue;
                auto entry = m_songPages[row].toObject();
                const auto before = entry["song"].toObject();
                const auto song = m_metadata->complete(before);
                if (song != before) {
                    entry["song"] = song;
                    m_songPages[row] = entry;
                    changed = true;
                }
            }
            if (changed)
                publishSongs();
        },
        joined());
    for (int i = 0; i < m_songPages.size(); ++i) {
        auto entry = m_songPages[i].toObject();
        entry["song"] = m_metadata->complete(entry["song"].toObject());
        m_songPages[i] = entry;
    }
}
void ListenTogetherController::attachRepository(PlaybackRepository &repository)
{
    m_repository = &repository;
    connect(&repository, &PlaybackRepository::roomsLoaded, this,
            [this](quint64 account, quint64 revision, const QJsonArray &rooms,
                   const QStringList &dead) {
                if (m_closed || account != m_api.currentAccountUserId() ||
                    revision != m_api.sessionRevision())
                    return;
                for (const auto &id : dead) {
                    if (!m_dissolved.contains(id))
                        m_dissolvedOrder.append(id);
                    m_dissolved.insert(id);
                }
                if (!m_mineComplete) {
                    for (const auto &value : rooms) {
                        const auto row = value.toObject().toVariantMap();
                        const auto id = row.value("id").toString();
                        bool exists = false;
                        for (const auto &old : m_ownedRooms)
                            exists |= old.toMap().value("id").toString() == id;
                        if (!exists && !m_dissolved.contains(id))
                            m_ownedRooms.append(row);
                    }
                }
                updateRoomsProjection();
                if (m_indexDirty)
                    saveRoomIndex();
            });
    if (signedIn())
        repository.loadRooms(m_api.currentAccountUserId(), m_api.sessionRevision());
}
QString ListenTogetherController::nickname() const
{
    return m_account && !m_account->nickname().isEmpty() ? m_account->nickname() : tr("我");
}
int ListenTogetherController::onlineCount() const
{
    return joined()
               ? qMax(1, qMax(m_members.rowCount(), m_activeRoom.value("member_count").toInt()))
               : 0;
}
bool ListenTogetherController::owner() const
{
    return joined() && m_activeRoom.value("owner").toMap().value("id").toString() ==
                           QString::number(m_api.currentAccountUserId());
}
bool ListenTogetherController::playbackDetached() const
{
    return joined() && m_detached;
}
QVariantMap ListenTogetherController::currentSong() const
{
    const int i = remoteSongIndex();
    return i >= 0 ? m_songs.item(i) : QVariantMap{};
}
void ListenTogetherController::watch(quint64 id, Scope scope, Success done, Failure error)
{
    if (!id) {
        if (error)
            error({{"message", tr("一起听请求未能启动")}});
        return;
    }
    m_pending.insert(id, {m_epoch, m_api.sessionRevision(),
                          scope == Scope::Preview ? m_previewRevision : m_pageRevision, scope,
                          m_roomId, std::move(done), std::move(error)});
}
bool ListenTogetherController::current(const Pending &p) const
{
    if (m_closed || p.session != m_api.sessionRevision())
        return false;
    switch (p.scope) {
    case Scope::Page:
        return m_pageActive && p.view == m_pageRevision;
    case Scope::Preview:
        return m_pageActive && p.view == m_previewRevision;
    case Scope::Room:
        return joined() && p.epoch == m_epoch && p.room == m_roomId;
    case Scope::Account:
        return p.epoch == m_epoch;
    }
    return false;
}
void ListenTogetherController::after(quint64 id, std::function<void()> terminal)
{
    if (!terminal)
        return;
    if (!id || !m_pending.contains(id)) {
        terminal();
        return;
    }
    m_terminals[id].append(std::move(terminal));
}
void ListenTogetherController::succeeded(quint64 id, const QByteArray &bytes)
{
    if (!m_pending.contains(id))
        return;
    const auto pending = m_pending.take(id);
    const auto terminals = m_terminals.take(id);
    if (!current(pending))
        return;
    const auto json = QJsonDocument::fromJson(bytes);
    if (json.isObject()) {
        if (pending.done)
            pending.done(json.object());
    } else if (pending.failed)
        pending.failed({{"message", tr("一起听响应无法读取")}});
    if (current(pending))
        for (const auto &fn : terminals)
            fn();
}
void ListenTogetherController::failed(quint64 id, const QByteArray &bytes)
{
    if (!m_pending.contains(id))
        return;
    const auto pending = m_pending.take(id);
    const auto terminals = m_terminals.take(id);
    if (!current(pending))
        return;
    auto error = QJsonDocument::fromJson(bytes).object();
    if (pending.scope == Scope::Room && dissolved(error)) {
        const auto room = m_roomId;
        clearSession();
        rememberDissolved(room);
        emit notice(tr("房间已解散"), 1);
        return;
    }
    if (pending.failed)
        pending.failed(error);
    else
        reportError(error);
    if (current(pending))
        for (const auto &fn : terminals)
            fn();
}
void ListenTogetherController::cancel(Scope scope)
{
    const auto ids = m_pending.keys();
    for (auto id : ids)
        if (m_pending.value(id).scope == scope) {
            m_pending.remove(id);
            m_terminals.remove(id);
            m_api.cancel(id);
        }
}
void ListenTogetherController::cancelAll()
{
    const auto ids = m_pending.keys();
    m_pending.clear();
    m_terminals.clear();
    for (auto id : ids)
        m_api.cancel(id);
}
bool ListenTogetherController::requireLogin()
{
    if (signedIn() && !m_closed)
        return true;
    emit loginRequested();
    return false;
}
bool ListenTogetherController::dissolved(const QJsonObject &error) const
{
    const int code = error.value("code").toVariant().toInt();
    return code == 55006 || code == 20005 ||
           (code == 20002 && QRegularExpression(QStringLiteral("(?:群组|房间)(?:不存在|已解散)"))
                                 .match(error.value("message").toString())
                                 .hasMatch());
}
void ListenTogetherController::reportError(const QJsonObject &error, bool toast)
{
    m_error = error.value("message").toString(tr("一起听操作失败"));
    if (error.value("code").toVariant().toInt() == 51002)
        m_error = tr("请先登录后再使用一起听");
    if (dissolved(error))
        m_error = tr("房间不存在或已解散，无法加入");
    emit changed();
    if (toast)
        emit notice(m_error, 2);
}
void ListenTogetherController::accountChanged()
{
    ++m_epoch;
    ++m_pageRevision;
    ++m_previewRevision;
    cancelAll();
    clearSession();
    closePreview();
    m_ownedRooms.clear();
    m_publicRooms.clear();
    m_dissolved.clear();
    m_dissolvedOrder.clear();
    m_mineComplete = m_indexDirty = m_loadingRooms = false;
    m_savedIndex.clear();
    m_page = 0;
    m_roomsEnded = false;
    m_error.clear();
    updateRoomsProjection();
    if (m_repository && signedIn())
        m_repository->loadRooms(m_api.currentAccountUserId(), m_api.sessionRevision());
    if (m_pageActive && m_networkEnabled && signedIn())
        setPageActive(true, m_networkEnabled, m_recoverOnEntry);
    emit changed();
}
void ListenTogetherController::openSharedRoom(const QString &id, bool network)
{
    setPageActive(true, network, false);
    m_scope = 0;
    m_search.clear();
    updateRoomsProjection();
    if (network && signedIn() && !m_closed) {
        loadRooms();
        inspectRoom(id);
    }
}
void ListenTogetherController::setPageActive(bool active, bool network, bool recoverSession)
{
    m_pageActive = active;
    m_networkEnabled = network;
    if (active)
        m_recoverOnEntry = recoverSession;
    ++m_pageRevision;
    cancel(Scope::Page);
    m_loadingRooms = false;
    if (!active) {
        closePreview();
        emit changed();
        return;
    }
    if (!network || !signedIn() || m_closed || !recoverSession)
        return;
    if (joined()) {
        if (m_detached)
            resumeRoomPlayback();
        return;
    }
    if (m_phase == Joining || m_phase == Creating || m_phase == Leaving)
        return;
    recover(
        [this](const QJsonObject &session) {
            if (!m_pageActive)
                return;
            if (!session.value("room_id").toString().isEmpty())
                hydrate(session.value("room_id").toString(), session.toVariantMap());
            else
                loadRooms();
        },
        [this](const QJsonObject &error) {
            reportError(error, false);
            loadRooms();
        });
}
void ListenTogetherController::shutdown()
{
    if (m_closed)
        return;
    cancelMetadata();
    disconnect(&m_playback, &PlaybackService::changed, this,
               &ListenTogetherController::playbackChanged);
    disconnect(&m_playback, &PlaybackService::positionChanged, this,
               &ListenTogetherController::positionChanged);
    m_closed = true;
    ++m_epoch;
    cancelAll();
    m_fastTimer.stop();
    m_slowTimer.stop();
    m_heartbeatTimer.stop();
    // Membership is recovered from get_status next launch; shutdown must not
    // send an unsolicited leave or persist a stale local membership snapshot.
}
void ListenTogetherController::setRoomScope(int scope)
{
    scope = qBound(0, scope, 1);
    if (scope == m_scope)
        return;
    m_scope = scope;
    updateRoomsProjection();
    if (m_pageActive)
        loadRooms();
    emit changed();
}
void ListenTogetherController::setSearch(const QString &text)
{
    if (m_search == text)
        return;
    m_search = text;
    updateRoomsProjection();
    emit changed();
}
void ListenTogetherController::updateRoomsProjection()
{
    QVariantList rows;
    for (const auto &row : m_scope ? m_ownedRooms : m_publicRooms) {
        const auto room = row.toMap();
        if (m_dissolved.contains(room.value("id").toString()))
            continue;
        const auto owner = room.value("owner").toMap();
        const auto song = room.value("current_song").toMap().value("song").toMap();
        QString searchable = room.value("name").toString() + " " + owner.value("name").toString() +
                             " " + song.value("title").toString() + " " +
                             room.value("channel_name").toString();
        for (const auto &artist : song.value("artists").toList())
            searchable += " " + artist.toMap().value("name").toString();
        if (!m_search.trimmed().isEmpty() &&
            !searchable.contains(m_search.trimmed(), Qt::CaseInsensitive))
            continue;
        rows.append(row);
    }
    m_rooms.replace(rows);
    emit changed();
}
QVariantMap ListenTogetherController::findRoom(const QString &id) const
{
    if (m_previewRoom.value("id").toString() == id)
        return m_previewRoom;
    if (m_roomId == id)
        return m_activeRoom;
    for (const auto &list : {m_publicRooms, m_ownedRooms})
        for (const auto &value : list)
            if (value.toMap().value("id").toString() == id)
                return value.toMap();
    return {};
}
void ListenTogetherController::upsertOwned(const QVariantMap &room)
{
    const auto id = room.value("id").toString();
    if (id.isEmpty() || room.value("owner").toMap().value("id").toString() !=
                            QString::number(m_api.currentAccountUserId()))
        return;
    for (int i = 0; i < m_ownedRooms.size(); ++i)
        if (m_ownedRooms[i].toMap().value("id").toString() == id) {
            m_ownedRooms[i] = room;
            saveRoomIndex();
            updateRoomsProjection();
            return;
        }
    m_ownedRooms.prepend(room);
    saveRoomIndex();
    updateRoomsProjection();
}
void ListenTogetherController::rememberDissolved(const QString &id)
{
    if (id.isEmpty())
        return;
    m_dissolved.insert(id);
    m_dissolvedOrder.removeAll(id);
    m_dissolvedOrder.append(id);
    while (m_dissolvedOrder.size() > 200)
        m_dissolved.remove(m_dissolvedOrder.takeFirst());
    m_ownedRooms.erase(std::remove_if(m_ownedRooms.begin(), m_ownedRooms.end(),
                                      [&id](const QVariant &row) {
                                          return row.toMap().value("id").toString() == id;
                                      }),
                       m_ownedRooms.end());
    saveRoomIndex();
    updateRoomsProjection();
}
void ListenTogetherController::saveRoomIndex()
{
    m_indexDirty = true;
    if (!m_repository || !signedIn())
        return;
    QJsonArray rows;
    for (const auto &value : m_ownedRooms) {
        const auto room = QJsonObject::fromVariantMap(value.toMap());
        QJsonObject clean;
        for (auto key : {"id", "name", "owner", "member_count", "capacity", "privacy_code",
                         "chat_code", "active_code", "background_url"})
            clean.insert(key, room.value(key));
        // Display-only profile fields; never persist opaque provider extensions.
        const auto profile = room.value("owner").toObject();
        clean.insert("owner", QJsonObject{{"id", profile.value("id")},
                                          {"name", profile.value("name")},
                                          {"avatar_url", profile.value("avatar_url")}});
        rows.append(clean);
    }
    const auto snapshot =
        QJsonDocument(QJsonObject{{"account", QString::number(m_api.currentAccountUserId())},
                                  {"rooms", rows},
                                  {"dissolved", QJsonArray::fromStringList(m_dissolvedOrder)}})
            .toJson(QJsonDocument::Compact);
    if (snapshot == m_savedIndex)
        return;
    m_savedIndex = snapshot;
    m_repository->saveRooms(m_api.currentAccountUserId(), rows, m_dissolvedOrder);
}
} // namespace EchoMusic
