#include "EchoPluginBusiness_p.h"
namespace EchoMusic::PluginBusiness
{
bool catalog(const QString &method, Arguments &a, ApiClient &api, std::function<quint64()> &command)
{
    const auto &arguments = a.values;
    if (method == "searchHot") {
        command = [&api] { return api.searchHot(); };
    }
    if (method == "searchDefault") {
        command = [&api] { return api.searchDefault(); };
    }
    if (method == "searchSuggest") {
        if (!arguments.contains("keyword"))
            a.valid = false;

        auto keyword = a.text("keyword", {});
        command = [&api, keyword] { return api.searchSuggest(keyword); };
    }
    if (method == "albumDynamicCover") {
        if (!arguments.contains("albumAudioId"))
            a.valid = false;
        if (!arguments.contains("albumId"))
            a.valid = false;

        auto albumAudioId = a.number("albumAudioId", 0);
        auto albumId = a.number("albumId", 0);
        command = [&api, albumAudioId, albumId] {
            return api.albumDynamicCover(albumAudioId, albumId);
        };
    }
    if (method == "recommendedPlaylists") {
        if (!arguments.contains("categoryId"))
            a.valid = false;

        auto categoryId = a.text("categoryId", {});
        command = [&api, categoryId] { return api.recommendedPlaylists(categoryId); };
    }
    if (method == "editorPlaylists") {
        command = [&api] { return api.editorPlaylists(); };
    }
    if (method == "styleRecommendations") {
        auto tagIds = a.strings("tagIds");
        command = [&api, tagIds] { return api.styleRecommendations(tagIds); };
    }
    if (method == "dailyRecommendations") {
        command = [&api] { return api.dailyRecommendations(); };
    }
    if (method == "freeListenSongs") {
        command = [&api] { return api.freeListenSongs(); };
    }
    if (method == "discoverRecommendations") {
        auto pageSize = a.number("pageSize", 4, 4294967295ULL);
        command = [&api, pageSize] { return api.discoverRecommendations(pageSize); };
    }
    if (method == "newSongCatalog") {
        command = [&api] { return api.newSongCatalog(); };
    }
    if (method == "newAlbumCatalog") {
        command = [&api] { return api.newAlbumCatalog(); };
    }
    if (method == "playlistCategories") {
        command = [&api] { return api.playlistCategories(); };
    }
    if (method == "artistDirectory") {
        if (!arguments.contains("sex"))
            a.valid = false;
        if (!arguments.contains("category"))
            a.valid = false;
        if (!arguments.contains("musician"))
            a.valid = false;

        auto sex = a.number("sex", 0, 4294967295ULL);
        auto category = a.number("category", 0, 4294967295ULL);
        auto musician = a.number("musician", 0, 4294967295ULL);
        command = [&api, sex, category, musician] {
            return api.artistDirectory(sex, category, musician);
        };
    }
    if (method == "recommendedRanks") {
        command = [&api] { return api.recommendedRanks(); };
    }
    if (method == "rankCatalog") {
        command = [&api] { return api.rankCatalog(); };
    }
    if (method == "rankSongs") {
        if (!arguments.contains("rankId"))
            a.valid = false;

        auto rankId = a.number("rankId", 0);
        command = [&api, rankId] { return api.rankSongs(rankId); };
    }
    if (method == "albumDetail") {
        if (!arguments.contains("albumId"))
            a.valid = false;

        auto albumId = a.number("albumId", 0);
        command = [&api, albumId] { return api.albumDetail(albumId); };
    }
    if (method == "albumSongs") {
        if (!arguments.contains("albumId"))
            a.valid = false;

        auto albumId = a.number("albumId", 0);
        auto page = a.number("page", 1, 4294967295ULL);
        auto pageSize = a.number("pageSize", 30, 4294967295ULL);
        command = [&api, albumId, page, pageSize] {
            return api.albumSongs(albumId, page, pageSize);
        };
    }
    if (method == "artistDetail") {
        if (!arguments.contains("artistId"))
            a.valid = false;

        auto artistId = a.number("artistId", 0);
        command = [&api, artistId] { return api.artistDetail(artistId); };
    }
    if (method == "artistSongs") {
        if (!arguments.contains("artistId"))
            a.valid = false;

        auto artistId = a.number("artistId", 0);
        auto page = a.number("page", 1, 4294967295ULL);
        auto pageSize = a.number("pageSize", 100, 4294967295ULL);
        auto sort = a.text("sort", "new");
        command = [&api, artistId, page, pageSize, sort] {
            return api.artistSongs(artistId, page, pageSize, sort);
        };
    }
    if (method == "artistAlbums") {
        if (!arguments.contains("artistId"))
            a.valid = false;

        auto artistId = a.number("artistId", 0);
        auto page = a.number("page", 1, 4294967295ULL);
        auto pageSize = a.number("pageSize", 30, 4294967295ULL);
        auto sort = a.text("sort", "hot");
        command = [&api, artistId, page, pageSize, sort] {
            return api.artistAlbums(artistId, page, pageSize, sort);
        };
    }
    if (method == "search") {
        auto kind = a.enumeration<ApiClient::SearchKind>(
            "kind", {"song", "album", "artist", "playlist", "lyric", "mv"}, "song");
        if (!arguments.contains("keyword"))
            a.valid = false;
        auto keyword = a.text("keyword", "");
        auto page = a.number("page", 1);
        auto pageSize = a.number("pageSize", 30);
        command = [&api, kind, keyword, page, pageSize] {
            return api.search(kind, keyword, page, pageSize);
        };
    }
    if (method == "personalFm") {
        if (!arguments.contains("request"))
            a.valid = false;
        auto request = parseFmRequest(a.object("request"), a.valid);
        command = [&api, request] { return api.personalFm(request); };
    }
    return bool(command);
}
} // namespace EchoMusic::PluginBusiness
