#include "EchoPluginBusiness_p.h"
namespace EchoMusic::PluginBusiness
{

ApiClient::CommentQuery parseCommentQuery(const QVariantMap &values, bool &valid)
{
    Arguments a(values);
    ApiClient::CommentQuery out;
    out.resource = a.enumeration<ApiClient::CommentResource>(
        "resource", {"music", "album", "playlist"}, "music");
    out.section = a.enumeration<ApiClient::CommentSection>(
        "section", {"all", "category", "hotWord", "floor"}, "all");
    out.id = a.text("id", "");
    out.threadId = a.text("threadId", "");
    out.hotWord = a.text("hotWord", "");
    out.code = a.text("code", "");
    out.mixSongId = a.number("mixSongId", 0);
    out.categoryId = a.number("categoryId", 0);
    out.page = a.number("page", 1);
    out.pageSize = a.number("pageSize", 30);
    valid = a.done() && valid;
    return out;
}
ApiClient::CommentWrite parseCommentWrite(const QVariantMap &values, bool &valid)
{
    Arguments a(values);
    ApiClient::CommentWrite out;
    out.resource = a.enumeration<ApiClient::CommentResource>(
        "resource", {"music", "album", "playlist"}, "music");
    out.id = a.text("id", "");
    out.name = a.text("name", "");
    out.content = a.text("content", "");
    out.commentId = a.text("commentId", "");
    out.threadId = a.text("threadId", "");
    out.parentId = a.text("parentId", "");
    out.replyUserName = a.text("replyUserName", "");
    out.replyContent = a.text("replyContent", "");
    out.code = a.text("code", "");
    out.mixSongId = a.number("mixSongId", 0);
    valid = a.done() && valid;
    return out;
}
ApiClient::ProfilePatch parseProfilePatch(const QVariantMap &values, bool &valid)
{
    Arguments a(values);
    ApiClient::ProfilePatch out;
    out.nickname =
        (a.values.contains("nickname") ? std::optional<QString>(a.text("nickname", ""))
                                       : (a.used.insert("nickname"), std::optional<QString>()));
    out.birthday =
        (a.values.contains("birthday") ? std::optional<QString>(a.text("birthday", ""))
                                       : (a.used.insert("birthday"), std::optional<QString>()));
    out.signature =
        (a.values.contains("signature") ? std::optional<QString>(a.text("signature", ""))
                                        : (a.used.insert("signature"), std::optional<QString>()));
    out.province =
        (a.values.contains("province") ? std::optional<QString>(a.text("province", ""))
                                       : (a.used.insert("province"), std::optional<QString>()));
    out.city = (a.values.contains("city") ? std::optional<QString>(a.text("city", ""))
                                          : (a.used.insert("city"), std::optional<QString>()));
    out.gender =
        (a.values.contains("gender") ? std::optional<quint64>(a.number("gender", 0))
                                     : (a.used.insert("gender"), std::optional<quint64>()));
    valid = a.done() && valid;
    return out;
}
ApiClient::PreferencePatch parsePreferencePatch(const QVariantMap &values, bool &valid)
{
    Arguments a(values);
    ApiClient::PreferencePatch out;
    out.mode = (a.values.contains("mode") ? std::optional<QString>(a.text("mode", ""))
                                          : (a.used.insert("mode"), std::optional<QString>()));
    out.gender =
        (a.values.contains("gender") ? std::optional<QString>(a.text("gender", ""))
                                     : (a.used.insert("gender"), std::optional<QString>()));
    out.age = (a.values.contains("age") ? std::optional<QString>(a.text("age", ""))
                                        : (a.used.insert("age"), std::optional<QString>()));
    out.languages =
        (a.values.contains("languages") ? std::optional<QString>(a.text("languages", ""))
                                        : (a.used.insert("languages"), std::optional<QString>()));
    out.styles =
        (a.values.contains("styles") ? std::optional<QString>(a.text("styles", ""))
                                     : (a.used.insert("styles"), std::optional<QString>()));
    out.recommendation = (a.values.contains("recommendation")
                              ? std::optional<QString>(a.text("recommendation", ""))
                              : (a.used.insert("recommendation"), std::optional<QString>()));
    valid = a.done() && valid;
    return out;
}
ApiClient::LoginDeviceTarget parseLoginDeviceTarget(const QVariantMap &values, bool &valid)
{
    Arguments a(values);
    ApiClient::LoginDeviceTarget out;
    if (!values.contains("mid"))
        a.valid = false;
    out.mid = a.text("mid", "");
    if (!values.contains("transportFingerprint"))
        a.valid = false;
    out.transportFingerprint = a.text("transportFingerprint", "");
    if (!values.contains("activeAtSeconds"))
        a.valid = false;
    out.activeAtSeconds = a.number("activeAtSeconds", 0);
    if (!values.contains("appId"))
        a.valid = false;
    out.appId = a.number("appId", 0);
    if (!values.contains("clientVersion"))
        a.valid = false;
    out.clientVersion = a.number("clientVersion", 0);
    valid = a.done() && valid;
    return out;
}
ApiClient::CloudDeleteTarget parseCloudDeleteTarget(const QVariantMap &values, bool &valid)
{
    Arguments a(values);
    ApiClient::CloudDeleteTarget out;
    if (!values.contains("kvId"))
        a.valid = false;
    out.kvId = a.number("kvId", 0);
    out.albumAudioId = a.number("albumAudioId", 0);
    valid = a.done() && valid;
    return out;
}
ApiClient::PlaylistTrackInput parsePlaylistTrackInput(const QVariantMap &values, bool &valid)
{
    Arguments a(values);
    ApiClient::PlaylistTrackInput out;
    if (!values.contains("title"))
        a.valid = false;
    out.title = a.text("title", "");
    if (!values.contains("hash"))
        a.valid = false;
    out.hash = a.text("hash", "");
    if (!values.contains("albumId"))
        a.valid = false;
    out.albumId = a.number("albumId", 0);
    if (!values.contains("songId"))
        a.valid = false;
    out.songId = a.number("songId", 0);
    valid = a.done() && valid;
    return out;
}
ApiClient::PlaylistTrackPosition parsePlaylistTrackPosition(const QVariantMap &values, bool &valid)
{
    Arguments a(values);
    ApiClient::PlaylistTrackPosition out;
    if (!values.contains("fileId"))
        a.valid = false;
    out.fileId = ApiClient::PlaylistTrackFileId{a.number("fileId", 0)};
    if (!values.contains("position"))
        a.valid = false;
    out.position = a.number("position", 0);
    valid = a.done() && valid;
    return out;
}
ApiClient::PlaylistPosition parsePlaylistPosition(const QVariantMap &values, bool &valid)
{
    Arguments a(values);
    ApiClient::PlaylistPosition out;
    if (!values.contains("id"))
        a.valid = false;
    out.id = ApiClient::CloudPlaylistId{a.number("id", 0)};
    out.kind = a.enumeration<ApiClient::PlaylistKind>("kind", {"owned", "collected"}, "owned");
    if (!values.contains("position"))
        a.valid = false;
    out.position = a.number("position", 0);
    valid = a.done() && valid;
    return out;
}
ApiClient::PlaylistUpdate parsePlaylistUpdate(const QVariantMap &values, bool &valid)
{
    Arguments a(values);
    ApiClient::PlaylistUpdate out;
    if (!values.contains("id"))
        a.valid = false;
    out.id = ApiClient::CloudPlaylistId{a.number("id", 0)};
    if (!values.contains("totalRevision"))
        a.valid = false;
    out.totalRevision = a.number("totalRevision", 0);
    if (!values.contains("sortIndex"))
        a.valid = false;
    out.sortIndex = a.signedNumber("sortIndex", 0);
    out.title = (a.values.contains("title") ? std::optional<QString>(a.text("title", ""))
                                            : (a.used.insert("title"), std::optional<QString>()));
    out.description = a.text("description", "");
    out.tags = a.text("tags", "");
    const auto kind = a.text("kind", "owned");
    if (kind != "owned" && kind != "collected")
        a.valid = false;
    out.kind =
        kind == "collected" ? ApiClient::PlaylistKind::Collected : ApiClient::PlaylistKind::Owned;
    out.coverReference = (a.values.contains("coverReference")
                              ? std::optional<QString>(a.text("coverReference", ""))
                              : (a.used.insert("coverReference"), std::optional<QString>()));
    valid = a.done() && valid;
    return out;
}
ApiClient::CollectionTarget parseCollectionTarget(const QVariantMap &values, bool &valid)
{
    Arguments a(values);
    ApiClient::CollectionTarget out;
    if (!values.contains("title"))
        a.valid = false;
    out.title = a.text("title", "");
    out.source = a.number("source", 1);
    out.creatorId =
        (a.values.contains("creatorId") ? std::optional<quint64>(a.number("creatorId", 0))
                                        : (a.used.insert("creatorId"), std::optional<quint64>()));
    out.creatorListId = a.number("creatorListId", 0);
    out.creatorGlobalId = a.text("creatorGlobalId", "");
    valid = a.done() && valid;
    return out;
}
ApiClient::PrivilegeResource parsePrivilegeResource(const QVariantMap &values, bool &valid)
{
    Arguments a(values);
    ApiClient::PrivilegeResource out;
    if (!values.contains("hash"))
        a.valid = false;
    out.hash = a.text("hash", "");
    out.albumId =
        (a.values.contains("albumId") ? std::optional<quint64>(a.number("albumId", 0))
                                      : (a.used.insert("albumId"), std::optional<quint64>()));
    valid = a.done() && valid;
    return out;
}
ApiClient::FmRequest parseFmRequest(const QVariantMap &values, bool &valid)
{
    Arguments a(values);
    ApiClient::FmRequest out;
    out.mode =
        a.enumeration<ApiClient::FmMode>("mode", {"normal", "small", "peak", "radio"}, "normal");
    out.action = a.enumeration<ApiClient::FmAction>(
        "action", {"play", "login", "garbage", "clickRed", "cancelRed", "changeSongPool"}, "play");
    out.pool = a.number("pool", 0);
    out.remaining = a.number("remaining", 0);
    out.overplay = a.boolean("overplay", false);
    out.hash = a.text("hash", "");
    out.mark = a.text("mark", "");
    out.audioId =
        (a.values.contains("audioId") ? std::optional<quint64>(a.number("audioId", 0))
                                      : (a.used.insert("audioId"), std::optional<quint64>()));
    out.playSeconds = (a.values.contains("playSeconds")
                           ? std::optional<quint64>(a.number("playSeconds", 0))
                           : (a.used.insert("playSeconds"), std::optional<quint64>()));
    valid = a.done() && valid;
    return out;
}
ListenRoomAudio parseListenRoomAudio(const QVariantMap &values, bool &valid)
{
    Arguments a(values);
    ListenRoomAudio out;
    if (!values.contains("hash"))
        a.valid = false;
    out.hash = a.text("hash", "");
    if (!values.contains("songId"))
        a.valid = false;
    out.songId = a.text("songId", "");
    valid = a.done() && valid;
    return out;
}
ListenRoomProgress parseListenRoomProgress(const QVariantMap &values, bool &valid)
{
    Arguments a(values);
    ListenRoomProgress out;
    if (!values.contains("audio"))
        a.valid = false;
    out.audio = parseListenRoomAudio(a.object("audio"), a.valid);
    out.positionMs = a.number("positionMs", 0);
    out.playing = a.boolean("playing", false);
    out.playMode = a.number("playMode", 1);
    valid = a.done() && valid;
    return out;
}
ListenRoomRequest parseListenRoomRequest(const QVariantMap &values, bool &valid)
{
    Arguments a(values);
    ListenRoomRequest out;
    out.action = a.enumeration<ListenRoomAction>(
        "action",
        {"create", "status", "join", "state", "heartbeat", "leave", "dismiss", "updateChat"},
        "status");
    out.roomId = a.text("roomId", "");
    out.name = a.text("name", "");
    out.privacy = a.number("privacy", 1);
    out.capacity = a.number("capacity", 5);
    out.chatEnabled = a.boolean("chatEnabled", true);
    valid = a.done() && valid;
    return out;
}
ListenMusicRequest parseListenMusicRequest(const QVariantMap &values, bool &valid)
{
    Arguments a(values);
    ListenMusicRequest out;
    out.action = a.enumeration<ListenMusicAction>(
        "action",
        {"list", "history", "detail", "members", "initialize", "sync", "playbackUrl", "switch",
         "player", "playlist", "recentPlaylist", "orderSong", "songOrders", "removeSong",
         "addSongs"},
        "list");
    out.roomId = a.text("roomId", "");
    out.tags = a.text("tags", "");
    out.version = a.text("version", "");
    out.requesterId = a.text("requesterId", "");
    out.page = a.number("page", 1);
    out.pageSize = a.number("pageSize", 20);
    out.cursor = a.number("cursor", 0);
    out.audios = [&] {
        QList<ListenRoomAudio> out;
        for (const auto &entry : a.array("audios")) {
            if (entry.metaType().id() != QMetaType::QVariantMap)
                a.valid = false;
            out.append(parseListenRoomAudio(entry.toMap(), a.valid));
        }
        return out;
    }();
    out.audio =
        (a.values.contains("audio")
             ? std::optional<ListenRoomAudio>(parseListenRoomAudio(a.object("audio"), a.valid))
             : (a.used.insert("audio"), std::optional<ListenRoomAudio>()));
    out.progress = (a.values.contains("progress")
                        ? std::optional<ListenRoomProgress>(
                              parseListenRoomProgress(a.object("progress"), a.valid))
                        : (a.used.insert("progress"), std::optional<ListenRoomProgress>()));
    out.command =
        a.enumeration<ListenPlayerCommand>("command", {"mode", "seek", "playing"}, "playing");
    out.positionMs = a.number("positionMs", 0);
    out.playMode = a.number("playMode", 1);
    out.playing = a.boolean("playing", false);
    out.automatic = a.boolean("automatic", false);
    valid = a.done() && valid;
    return out;
}
ListenChatRequest parseListenChatRequest(const QVariantMap &values, bool &valid)
{
    Arguments a(values);
    ListenChatRequest out;
    out.action = a.enumeration<ListenChatAction>("action", {"send", "history"}, "history");
    out.roomId = a.text("roomId", "");
    out.text = a.text("text", "");
    out.nickname = a.text("nickname", "");
    out.avatarUrl = a.text("avatarUrl", "");
    out.maxId = a.text("maxId", "0");
    valid = a.done() && valid;
    return out;
}
} // namespace EchoMusic::PluginBusiness
