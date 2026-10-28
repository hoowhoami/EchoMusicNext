#include "ListenTogetherUi.h"
#include "PlaybackService.h"
#include "ListenTogetherSeed.h"
#include "RouteController.h"
#include <QJsonDocument>
#include <QQmlEngine>
#include <algorithm>
namespace EchoMusic
{
ListenSongPicker::ListenSongPicker(ApiClient &api, PlaybackService &player,
                                   UserPlaylistModel &library, SongMetadataService &metadata,
                                   ListenTogetherController &room, QObject *owner)
    : QObject(owner), m_api(api), m_playback(player), m_library(library), m_metadata(metadata),
      m_room(room)
{
    m_songs.attachMetadata(&metadata);
    connect(&api, &ApiClient::succeeded, this, &ListenSongPicker::accept);
    connect(&api, &ApiClient::failed, this, [this](quint64 id, int, const QByteArray &bytes) {
        if (!m_request || id != m_request || m_session != m_api.sessionRevision())
            return;
        m_request = 0;
        m_loading = false;
        m_error = QJsonDocument::fromJson(bytes).object().value("message").toString();
        emit changed();
    });
    connect(&api, &ApiClient::sessionChanged, this, [this] {
        cancel();
        m_songs.clear();
        m_playlists.clear();
    });
    connect(&library, &UserPlaylistModel::changed, this, [this] {
        projectPlaylists();
        ensureLibrary();
    });
    connect(&metadata, &SongMetadataService::completed, this,
            [this](quint64 ticket, const QJsonArray &values) {
                if (m_metadataRequests.remove(ticket) && m_session == m_api.sessionRevision())
                    m_songs.applyPresentations(values);
            });
    connect(&player, &PlaybackService::historyLoaded, this, [this](const QJsonArray &entries) {
        if (m_source != 0 || m_session != m_api.sessionRevision())
            return;
        QJsonArray songs;
        for (const auto &entry : entries) {
            auto snapshot = entry.toObject().value("track").toObject();
            // Storage snapshots use display field names; retain all catalog facts.
            PlaybackTrack t = PlaybackTrack::fromDomain(snapshot);
            t.artist = snapshot.value("artist").toString();
            t.album = snapshot.value("album").toString();
            t.cover = snapshot.value("cover").toString();
            songs.append(t.toCatalog());
        }
        m_songs.replaceSongs(songs);
        completeSongs();
        m_loading = false;
        emit changed();
    });
    connect(&player, &PlaybackService::historyFailed, this, [this](const QString &error) {
        if (m_source == 0) {
            m_loading = false;
            m_error = error;
            emit changed();
        }
    });
    connect(&room, &ListenTogetherController::songsSubmitted, this, [this] {
        if (m_submitting) {
            m_submitting = false;
            emit submitted();
        }
    });
    connect(&room, &ListenTogetherController::changed, this, [this] {
        if (!m_room.addingSongs())
            m_submitting = false;
    });
    setSource(0);
}
ListenSongPicker::~ListenSongPicker()
{
    cancel();
}
void ListenSongPicker::cancel()
{
    const auto id = std::exchange(m_request, 0);
    if (id)
        m_api.cancel(id);
    m_loading = m_catalogLoading = false;
    for (auto ticket : m_metadataRequests)
        m_metadata.cancel(ticket);
    m_metadataRequests.clear();
}
void ListenSongPicker::completeSongs()
{
    const auto ids = m_songs.incompletePresentationIds();
    if (!ids.isEmpty()) {
        const auto ticket = m_metadata.request(ids);
        if (ticket)
            m_metadataRequests.insert(ticket);
    }
}
void ListenSongPicker::ensureLibrary()
{
    if (m_source < 1 || m_source > 3)
        return;
    const bool more = m_library.canFetchMore({});
    m_catalogLoading = more || m_library.state() == UserPlaylistModel::State::Loading;
    if (more)
        m_library.fetchMore({});
    else if (!m_catalogLoading && m_playlistId.isEmpty() && m_playlists.rowCount())
        selectPlaylist(m_playlists.item(0).value("id").toString());
    emit changed();
}
void ListenSongPicker::setSource(int source)
{
    if (source == m_source || source < 0 || source > 4)
        return;
    cancel();
    m_source = source;
    m_session = m_api.sessionRevision();
    m_libraryRow = -1;
    m_playlistId.clear();
    m_entries = {};
    m_songs.clear();
    m_error.clear();
    projectPlaylists();
    if (source == 0) {
        m_loading = true;
        m_playback.requestHistory();
    } else if (source >= 1 && source <= 3)
        ensureLibrary();
    emit changed();
}
void ListenSongPicker::projectPlaylists()
{
    QVariantList rows;
    for (int i = 0; i < m_library.rowCount(); ++i) {
        const auto ix = m_library.index(i);
        const auto creator = m_library.data(ix, UserPlaylistModel::CreatorIdRole).toULongLong();
        const bool supported = m_library.data(ix, UserPlaylistModel::SourceCodeRole).toUInt() != 2;
        const bool eligible =
            supported &&
            (m_source == 2   ? creator == m_api.currentAccountUserId()
             : m_source == 3 ? creator != m_api.currentAccountUserId() && i != m_library.likedRow()
                             : m_source == 1 && i == m_library.likedRow());
        if (!eligible)
            continue;
        rows.append(
            QVariantMap{{"id", m_library.data(ix, UserPlaylistModel::CloudListIdRole).toString()},
                        {"name", m_library.data(ix, UserPlaylistModel::TitleRole)},
                        {"cover_url", m_library.data(ix, UserPlaylistModel::CoverUrlRole)},
                        {"row", i}});
    }
    m_playlists.replace(rows);
    emit changed();
}
void ListenSongPicker::selectPlaylist(const QString &id)
{
    cancel();
    m_playlistId = id;
    m_libraryRow = -1;
    m_page = 1;
    m_entries = {};
    m_songs.clear();
    for (const auto &value : m_playlists.rows())
        if (value.toMap().value("id").toString() == id)
            m_libraryRow = value.toMap().value("row").toInt();
    if (m_libraryRow >= 0)
        page();
    emit changed();
}
void ListenSongPicker::page()
{
    m_pageSize = m_library.cloudIdAt(m_libraryRow) ? 300 : 50;
    m_request = m_library.requestTracksAt(m_libraryRow, m_page, m_pageSize);
    m_loading = m_request != 0;
    if (!m_request)
        m_error = tr("当前歌单暂时无法加载");
    emit changed();
}
void ListenSongPicker::invalidateSearch()
{
    if (m_source == 4) {
        cancel();
        emit changed();
    }
}
void ListenSongPicker::search(const QString &text)
{
    if (m_source != 4 || text.trimmed().isEmpty())
        return;
    cancel();
    m_error.clear();
    m_request = m_api.search(ApiClient::SearchKind::Song, text.trimmed(), 1, 50);
    m_loading = m_request != 0;
    emit changed();
}
void ListenSongPicker::accept(quint64 id, const QByteArray &bytes)
{
    if (!m_request || id != m_request || m_session != m_api.sessionRevision())
        return;
    m_request = 0;
    const auto data = QJsonDocument::fromJson(bytes).object();
    QJsonArray songs;
    if (m_source == 4)
        songs = data.value("page").toObject().value("items").toArray();
    else {
        const auto items = data.value("items").toArray();
        for (const auto &item : items)
            m_entries.append(item);
        auto rows = m_entries.toVariantList();
        if (m_library.cloudIdAt(m_libraryRow))
            std::stable_sort(rows.begin(), rows.end(), [](const QVariant &a, const QVariant &b) {
                return a.toMap().value("sort_index").toLongLong() <
                       b.toMap().value("sort_index").toLongLong();
            });
        QSet<QString> seen;
        for (const auto &item : rows) {
            const auto row = QJsonObject::fromVariantMap(item.toMap());
            auto song = row.value("song").toObject();
            song.insert("metadata", row.value("metadata"));
            const auto key = song.value("id").toString();
            if (seen.contains(key))
                continue;
            seen.insert(key);
            songs.append(song);
        }
        m_songs.replaceSongs(songs, true);
        if (items.size() >= m_pageSize && m_page < 50) {
            ++m_page;
            page();
            return;
        }
    }
    m_songs.replaceSongs(songs, true);
    completeSongs();
    m_loading = false;
    emit changed();
}
void ListenSongPicker::submit()
{
    const auto selected = m_songs.tracks(true);
    if (selected.isEmpty() || m_room.addingSongs())
        return;
    m_submitting = true;
    if (m_room.owner()) {
        QVariantList values;
        for (const auto &track : selected)
            values.append(track.toCatalog().toVariantMap());
        m_room.addSongs(values);
    } else if (!selected.isEmpty())
        m_room.requestSong(selected.first().toCatalog().toVariantMap());
}
void ListenSongPicker::chooseSong(const QString &id)
{
    if (m_room.addingSongs())
        return;
    const auto candidate = m_songs.track(id);
    if (!candidate || candidate->hash.isEmpty() || !candidate->songId) {
        m_error = tr("歌曲信息不完整，暂时无法点播");
        emit changed();
        return;
    }
    if (!m_room.owner()) {
        const auto track = m_songs.track(id);
        if (track) {
            m_submitting = true;
            m_room.requestSong(track->toCatalog().toVariantMap());
        }
        return;
    }
    if (!m_songs.selectedIds().contains(id) && m_songs.selectedCount() >= 50) {
        m_error = tr("每次最多添加 50 首歌曲");
        emit changed();
        return;
    }
    m_songs.toggleSelected(id);
}
ListenTogetherUi::ListenTogetherUi(ApiClient &api, PlaybackService &playback,
                                   UserPlaylistModel &library, SongMetadataService &metadata,
                                   RouteController &router, bool network, QObject *parent)
    : QObject(parent), m_api(api), m_playback(playback), m_library(library), m_metadata(metadata)
{
    connect(&router, &RouteController::pageActivated, this, [this, network](RouteContext *context) {
        const bool active = context->name() == "listen-together";
        const auto roomId = context->query().value("roomId").toString().trimmed();
        if (active && !roomId.isEmpty()) {
            if (context->query().value("roomType", "0").toString() == "0")
                room()->openSharedRoom(roomId, network);
            else {
                room()->setPageActive(true, network, false);
                emit room() -> notice(tr("自习室已下线，一起听目前仅支持众乐房"), 2);
            }
        } else
            room()->setPageActive(active, network);
    });
}
ListenTogetherController *ListenTogetherUi::room()
{
    return m_playback.listenTogether();
}
int ListenTogetherUi::creationSongCount() const
{
    return listenTogetherSeed(m_playback.queue()->tracks(), m_playback.currentTrack()).size();
}
ListenSongPicker *ListenTogetherUi::createPicker(QObject *owner)
{
    if (!owner || !room()->joined())
        return nullptr;
    auto *picker = new ListenSongPicker(m_api, m_playback, m_library, m_metadata, *room(), owner);
    QQmlEngine::setObjectOwnership(picker, QQmlEngine::CppOwnership);
    return picker;
}
} // namespace EchoMusic

namespace EchoMusic
{
void ListenTogetherUi::registerRoutes(RouteController &router)
{
    router.registerPage({"listen-together",
                         "/main/listen-together",
                         QUrl("qrc:/qt/qml/EchoMusic/ListenTogether/ListenTogetherPage.qml"),
                         {},
                         true,
                         true,
                         false,
                         {{"service", QVariant::fromValue(this)}}});
}
} // namespace EchoMusic
