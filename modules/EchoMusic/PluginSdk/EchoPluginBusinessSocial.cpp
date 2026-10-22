#include "EchoPluginBusiness_p.h"
namespace EchoMusic::PluginBusiness
{
bool social(const QString &method, Arguments &a, ApiClient &api, std::function<quint64()> &command)
{
    const auto &arguments = a.values;
    if (method == "songRanking") {
        if (!arguments.contains("mixSongId"))
            a.valid = false;

        auto mixSongId = a.number("mixSongId", 0);
        auto details = a.boolean("details", false);
        command = [&api, mixSongId, details] { return api.songRanking(mixSongId, details); };
    }
    if (method == "commentCount") {
        if (!arguments.contains("hash"))
            a.valid = false;

        auto hash = a.text("hash", {});
        command = [&api, hash] { return api.commentCount(hash); };
    }
    if (method == "collectVideo") {
        if (!arguments.contains("videoId"))
            a.valid = false;
        if (!arguments.contains("collect"))
            a.valid = false;

        auto videoId = a.number("videoId", 0);
        auto collect = a.boolean("collect", false);
        command = [&api, videoId, collect] { return api.collectVideo(videoId, collect); };
    }
    if (method == "songBarrage") {
        if (!arguments.contains("hash"))
            a.valid = false;

        auto hash = a.text("hash", {});
        auto pageSize = a.number("pageSize", 100, 4294967295ULL);
        command = [&api, hash, pageSize] { return api.songBarrage(hash, pageSize); };
    }
    if (method == "sendSongBarrage") {
        if (!arguments.contains("hash"))
            a.valid = false;
        if (!arguments.contains("name"))
            a.valid = false;
        if (!arguments.contains("content"))
            a.valid = false;

        auto hash = a.text("hash", {});
        auto name = a.text("name", {});
        auto content = a.text("content", {});
        command = [&api, hash, name, content] { return api.sendSongBarrage(hash, name, content); };
    }
    if (method == "videoBarrage") {
        if (!arguments.contains("hash"))
            a.valid = false;

        auto hash = a.text("hash", {});
        auto pageSize = a.number("pageSize", 100, 4294967295ULL);
        command = [&api, hash, pageSize] { return api.videoBarrage(hash, pageSize); };
    }
    if (method == "sendVideoBarrage") {
        if (!arguments.contains("hash"))
            a.valid = false;
        if (!arguments.contains("name"))
            a.valid = false;
        if (!arguments.contains("content"))
            a.valid = false;

        auto hash = a.text("hash", {});
        auto name = a.text("name", {});
        auto content = a.text("content", {});
        command = [&api, hash, name, content] { return api.sendVideoBarrage(hash, name, content); };
    }
    if (method == "followUser") {
        if (!arguments.contains("id"))
            a.valid = false;
        if (!arguments.contains("following"))
            a.valid = false;

        auto id = a.number("id", 0);
        auto following = a.boolean("following", false);
        command = [&api, id, following] { return api.followUser(id, following); };
    }
    if (method == "followArtist") {
        if (!arguments.contains("id"))
            a.valid = false;
        if (!arguments.contains("following"))
            a.valid = false;

        auto id = a.number("id", 0);
        auto following = a.boolean("following", false);
        command = [&api, id, following] { return api.followArtist(id, following); };
    }
    if (method == "socialMessages") {
        if (!arguments.contains("target"))
            a.valid = false;

        auto target = a.number("target", 0);
        auto tag = a.text("tag", {});
        command = [&api, target, tag] { return api.socialMessages(target, tag); };
    }
    if (method == "sendSocialMessage") {
        if (!arguments.contains("target"))
            a.valid = false;
        if (!arguments.contains("tag"))
            a.valid = false;
        if (!arguments.contains("text"))
            a.valid = false;
        if (!arguments.contains("nickname"))
            a.valid = false;

        auto target = a.number("target", 0);
        auto tag = a.text("tag", {});
        auto text = a.text("text", {});
        auto nickname = a.text("nickname", {});
        command = [&api, target, tag, text, nickname] {
            return api.sendSocialMessage(target, tag, text, nickname);
        };
    }
    if (method == "listenTogetherRoom") {
        if (!arguments.contains("request"))
            a.valid = false;
        auto request = parseListenRoomRequest(a.object("request"), a.valid);
        command = [&api, request] { return api.listenTogetherRoom(request); };
    }
    if (method == "listenTogetherMusic") {
        if (!arguments.contains("request"))
            a.valid = false;
        auto request = parseListenMusicRequest(a.object("request"), a.valid);
        command = [&api, request] { return api.listenTogetherMusic(request); };
    }
    if (method == "listenTogetherChat") {
        if (!arguments.contains("request"))
            a.valid = false;
        auto request = parseListenChatRequest(a.object("request"), a.valid);
        command = [&api, request] { return api.listenTogetherChat(request); };
    }
    if (method == "comments") {
        if (!arguments.contains("query"))
            a.valid = false;
        auto query = parseCommentQuery(a.object("query"), a.valid);
        command = [&api, query] { return api.comments(query); };
    }
    if (method == "sendComment") {
        if (!arguments.contains("write"))
            a.valid = false;
        auto write = parseCommentWrite(a.object("write"), a.valid);
        command = [&api, write] { return api.sendComment(write); };
    }
    if (method == "replyComment") {
        if (!arguments.contains("write"))
            a.valid = false;
        auto write = parseCommentWrite(a.object("write"), a.valid);
        command = [&api, write] { return api.replyComment(write); };
    }
    if (method == "deleteComment") {
        if (!arguments.contains("write"))
            a.valid = false;
        auto write = parseCommentWrite(a.object("write"), a.valid);
        command = [&api, write] { return api.deleteComment(write); };
    }
    if (method == "commentAuthorProducts") {
        if (!arguments.contains("userIds"))
            a.valid = false;
        auto userIds = a.identifiers("userIds");
        command = [&api, userIds] { return api.commentAuthorProducts(userIds); };
    }
    if (method == "socialUsers") {
        auto list = a.enumeration<ApiClient::SocialList>(
            "list", {"follow", "friends", "fans", "visitors"}, "follow");
        command = [&api, list] { return api.socialUsers(list); };
    }
    if (method == "contentBlacklist") {
        auto kind = a.enumeration<ApiClient::BlacklistKind>("kind", {"song", "singer"}, "song");
        auto page = a.number("page", 1);
        auto pageSize = a.number("pageSize", 30);
        command = [&api, kind, page, pageSize] {
            return api.contentBlacklist(kind, page, pageSize);
        };
    }
    if (method == "editContentBlacklist") {
        auto kind = a.enumeration<ApiClient::BlacklistKind>("kind", {"song", "singer"}, "song");
        if (!arguments.contains("key"))
            a.valid = false;
        auto key = a.text("key", "");
        if (!arguments.contains("name"))
            a.valid = false;
        auto name = a.text("name", "");
        if (!arguments.contains("mixSongId"))
            a.valid = false;
        auto mixSongId = a.text("mixSongId", "");
        auto remove = a.boolean("remove", false);
        command = [&api, kind, key, name, mixSongId, remove] {
            return api.editContentBlacklist(kind, key, name, mixSongId, remove);
        };
    }
    return bool(command);
}
} // namespace EchoMusic::PluginBusiness
