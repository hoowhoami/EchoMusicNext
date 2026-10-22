#include "EchoPluginBusiness_p.h"
namespace EchoMusic::PluginBusiness
{
bool media(const QString &method, Arguments &a, ApiClient &api, std::function<quint64()> &command)
{
    const auto &arguments = a.values;
    if (method == "songClimax") {
        if (!arguments.contains("hash"))
            a.valid = false;

        auto hash = a.text("hash", {});
        command = [&api, hash] { return api.songClimax(hash); };
    }
    if (method == "favoriteCount") {
        if (!arguments.contains("mixSongId"))
            a.valid = false;

        auto mixSongId = a.number("mixSongId", 0);
        command = [&api, mixSongId] { return api.favoriteCount(mixSongId); };
    }
    if (method == "songMv") {
        if (!arguments.contains("mixSongId"))
            a.valid = false;

        auto mixSongId = a.number("mixSongId", 0);
        command = [&api, mixSongId] { return api.songMv(mixSongId); };
    }
    if (method == "videoDetail") {
        if (!arguments.contains("videoId"))
            a.valid = false;

        auto videoId = a.text("videoId", {});
        command = [&api, videoId] { return api.videoDetail(videoId); };
    }
    if (method == "videoPrivilege") {
        if (!arguments.contains("hash"))
            a.valid = false;

        auto hash = a.text("hash", {});
        command = [&api, hash] { return api.videoPrivilege(hash); };
    }
    if (method == "videoUrl") {
        if (!arguments.contains("hash"))
            a.valid = false;

        auto hash = a.text("hash", {});
        command = [&api, hash] { return api.videoUrl(hash); };
    }
    if (method == "commonHeadphoneEffect") {
        command = [&api] { return api.commonHeadphoneEffect(); };
    }
    if (method == "lyricCandidates") {
        if (!arguments.contains("hash"))
            a.valid = false;
        if (!arguments.contains("songId"))
            a.valid = false;
        if (!arguments.contains("durationMs"))
            a.valid = false;

        auto hash = a.text("hash", {});
        auto songId = a.number("songId", 0);
        auto durationMs = a.number("durationMs", 0);
        auto allCandidates = a.boolean("allCandidates", false);
        command = [&api, hash, songId, durationMs, allCandidates] {
            return api.lyricCandidates(hash, songId, durationMs, allCandidates);
        };
    }
    if (method == "lyricCandidatesByKeywords") {
        if (!arguments.contains("keywords"))
            a.valid = false;
        if (!arguments.contains("durationMs"))
            a.valid = false;

        auto keywords = a.text("keywords", {});
        auto durationMs = a.number("durationMs", 0);
        auto allCandidates = a.boolean("allCandidates", true);
        command = [&api, keywords, durationMs, allCandidates] {
            return api.lyricCandidatesByKeywords(keywords, durationMs, allCandidates);
        };
    }
    if (method == "lyric") {
        if (!arguments.contains("id"))
            a.valid = false;
        if (!arguments.contains("accessKey"))
            a.valid = false;

        auto id = a.text("id", {});
        auto accessKey = a.text("accessKey", {});
        command = [&api, id, accessKey] { return api.lyric(id, accessKey); };
    }
    if (method == "audioEffects") {
        auto category = a.enumeration<ApiClient::EffectCategory>(
            "category", {"artist", "headphoneBrands", "headphoneModels", "market"}, "artist");
        auto page = a.number("page", 1);
        auto pageSize = a.number("pageSize", 20);
        auto sort = a.number("sort", 2);
        auto brand = a.number("brand", 0);
        command = [&api, category, page, pageSize, sort, brand] {
            return api.audioEffects(category, page, pageSize, sort, brand);
        };
    }
    if (method == "playbackSource") {
        if (!arguments.contains("hash"))
            a.valid = false;
        auto hash = a.text("hash", "");
        if (!arguments.contains("albumId"))
            a.valid = false;
        auto albumId = a.number("albumId", 0);
        if (!arguments.contains("songId"))
            a.valid = false;
        auto songId = a.number("songId", 0);
        auto quality = a.enumeration<ApiClient::PlaybackQuality>(
            "quality",
            {"standard", "higher", "flac", "hiRes", "viperTape", "viperClear", "viperAtmos",
             "automatic"},
            "standard");
        auto authorization = a.text("authorization", "");
        auto effect = a.enumeration<ApiClient::SoundEffect>(
            "effect",
            {"none", "piano", "vocal", "accompaniment", "subwoofer", "ancient", "surnay", "dj"},
            "none");
        auto originPage = a.text("originPage", "");
        command = [&api, hash, albumId, songId, quality, authorization, effect, originPage] {
            return api.playbackSource(hash, albumId, songId, quality, authorization, {}, effect,
                                      originPage);
        };
    }
    if (method == "songPrivileges") {
        if (!arguments.contains("resources"))
            a.valid = false;
        auto resources = [&] {
            QList<ApiClient::PrivilegeResource> out;
            for (const auto &entry : a.array("resources")) {
                if (entry.metaType().id() != QMetaType::QVariantMap)
                    a.valid = false;
                out.append(parsePrivilegeResource(entry.toMap(), a.valid));
            }
            return out;
        }();
        command = [&api, resources] { return api.songPrivileges(resources); };
    }
    return bool(command);
}
} // namespace EchoMusic::PluginBusiness
