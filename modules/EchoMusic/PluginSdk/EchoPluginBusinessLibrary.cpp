#include "EchoPluginBusiness_p.h"
namespace EchoMusic::PluginBusiness
{
bool library(const QString &method, Arguments &a, ApiClient &api, std::function<quint64()> &command)
{
    const auto &arguments = a.values;
    if (method == "userPlaylists") {
        auto page = a.number("page", 1, 4294967295ULL);
        auto pageSize = a.number("pageSize", 30, 4294967295ULL);
        command = [&api, page, pageSize] { return api.userPlaylists(page, pageSize); };
    }
    if (method == "importPlaylistLink") {
        if (!arguments.contains("text"))
            a.valid = false;

        auto text = a.text("text", {});
        command = [&api, text] { return api.importPlaylistLink(text); };
    }
    if (method == "submitPlaylistImportImage") {
        if (!arguments.contains("taskSerial"))
            a.valid = false;
        if (!arguments.contains("image"))
            a.valid = false;

        auto taskSerial = a.text("taskSerial", {});
        auto image = a.bytes("image");
        command = [&api, taskSerial, image] {
            return api.submitPlaylistImportImage(taskSerial, image);
        };
    }
    if (method == "playlistImportStatus") {
        if (!arguments.contains("taskId"))
            a.valid = false;

        auto taskId = a.number("taskId", 0);
        command = [&api, taskId] { return api.playlistImportStatus(taskId); };
    }
    if (method == "uploadPlaylistCover") {
        if (!arguments.contains("png"))
            a.valid = false;

        auto png = a.bytes("png");
        command = [&api, png] { return api.uploadPlaylistCover(png); };
    }
    if (method == "createPlaylist") {
        if (!arguments.contains("title"))
            a.valid = false;
        if (!arguments.contains("isPrivate"))
            a.valid = false;

        auto title = a.text("title", {});
        auto isPrivate = a.boolean("isPrivate", false);
        command = [&api, title, isPrivate] { return api.createPlaylist(title, isPrivate); };
    }
    if (method == "importPlaylistScreenshot") {
        if (!arguments.contains("taskSerial"))
            a.valid = false;
        auto taskSerial = a.text("taskSerial", "");
        if (!arguments.contains("id"))
            a.valid = false;
        auto id = ApiClient::CloudPlaylistId{a.number("id", 0)};
        if (!arguments.contains("title"))
            a.valid = false;
        auto title = a.text("title", "");
        command = [&api, taskSerial, id, title] {
            return api.importPlaylistScreenshot(taskSerial, id, title);
        };
    }
    if (method == "playlistImportResult") {
        if (!arguments.contains("id"))
            a.valid = false;
        auto id = ApiClient::CloudPlaylistId{a.number("id", 0)};
        auto page = a.number("page", 1);
        auto pageSize = a.number("pageSize", 100);
        command = [&api, id, page, pageSize] {
            return api.playlistImportResult(id, page, pageSize);
        };
    }
    if (method == "playlistDetails") {
        if (!arguments.contains("ids"))
            a.valid = false;
        auto ids = [&] {
            QList<ApiClient::PublicCollectionId> out;
            for (const auto &entry : a.array("ids")) {
                if (entry.metaType().id() != QMetaType::QString)
                    a.valid = false;
                out.append(ApiClient::PublicCollectionId{entry.toString()});
            }
            return out;
        }();
        command = [&api, ids] { return api.playlistDetails(ids); };
    }
    if (method == "publicPlaylistTracks") {
        if (!arguments.contains("id"))
            a.valid = false;
        auto id = ApiClient::PublicCollectionId{a.text("id", "")};
        auto page = a.number("page", 1);
        auto pageSize = a.number("pageSize", 30);
        command = [&api, id, page, pageSize] {
            return api.publicPlaylistTracks(id, page, pageSize);
        };
    }
    if (method == "cloudPlaylistTracks") {
        if (!arguments.contains("id"))
            a.valid = false;
        auto id = ApiClient::CloudPlaylistId{a.number("id", 0)};
        auto page = a.number("page", 1);
        auto pageSize = a.number("pageSize", 300);
        command = [&api, id, page, pageSize] {
            return api.cloudPlaylistTracks(id, page, pageSize);
        };
    }
    if (method == "songMetadata") {
        if (!arguments.contains("ids"))
            a.valid = false;
        auto ids = a.identifiers("ids");
        command = [&api, ids] { return api.songMetadata(ids); };
    }
    if (method == "sortPlaylists") {
        if (!arguments.contains("totalRevision"))
            a.valid = false;
        auto totalRevision = a.number("totalRevision", 0);
        if (!arguments.contains("positions"))
            a.valid = false;
        auto positions = [&] {
            QList<ApiClient::PlaylistPosition> out;
            for (const auto &entry : a.array("positions")) {
                if (entry.metaType().id() != QMetaType::QVariantMap)
                    a.valid = false;
                out.append(parsePlaylistPosition(entry.toMap(), a.valid));
            }
            return out;
        }();
        command = [&api, totalRevision, positions] {
            return api.sortPlaylists(totalRevision, positions);
        };
    }
    if (method == "collectPlaylist") {
        if (!arguments.contains("target"))
            a.valid = false;
        auto target = parseCollectionTarget(a.object("target"), a.valid);
        command = [&api, target] { return api.collectPlaylist(target); };
    }
    if (method == "addPlaylistTracks") {
        if (!arguments.contains("id"))
            a.valid = false;
        auto id = ApiClient::CloudPlaylistId{a.number("id", 0)};
        if (!arguments.contains("tracks"))
            a.valid = false;
        auto tracks = [&] {
            QList<ApiClient::PlaylistTrackInput> out;
            for (const auto &entry : a.array("tracks")) {
                if (entry.metaType().id() != QMetaType::QVariantMap)
                    a.valid = false;
                out.append(parsePlaylistTrackInput(entry.toMap(), a.valid));
            }
            return out;
        }();
        command = [&api, id, tracks] { return api.addPlaylistTracks(id, tracks); };
    }
    if (method == "removePlaylistTracks") {
        if (!arguments.contains("id"))
            a.valid = false;
        auto id = ApiClient::CloudPlaylistId{a.number("id", 0)};
        if (!arguments.contains("files"))
            a.valid = false;
        QList<ApiClient::PlaylistTrackFileId> files;
        for (auto value : a.identifiers("files"))
            files.append({value});
        command = [&api, id, files] { return api.removePlaylistTracks(id, files); };
    }
    if (method == "sortPlaylistTracks") {
        if (!arguments.contains("id"))
            a.valid = false;
        auto id = ApiClient::CloudPlaylistId{a.number("id", 0)};
        if (!arguments.contains("revision"))
            a.valid = false;
        auto revision = a.number("revision", 0);
        if (!arguments.contains("positions"))
            a.valid = false;
        auto positions = [&] {
            QList<ApiClient::PlaylistTrackPosition> out;
            for (const auto &entry : a.array("positions")) {
                if (entry.metaType().id() != QMetaType::QVariantMap)
                    a.valid = false;
                out.append(parsePlaylistTrackPosition(entry.toMap(), a.valid));
            }
            return out;
        }();
        command = [&api, id, revision, positions] {
            return api.sortPlaylistTracks(id, revision, positions);
        };
    }
    if (method == "updatePlaylist") {
        if (!arguments.contains("update"))
            a.valid = false;
        auto update = parsePlaylistUpdate(a.object("update"), a.valid);
        command = [&api, update] { return api.updatePlaylist(update); };
    }
    if (method == "deletePlaylist") {
        if (!arguments.contains("id"))
            a.valid = false;
        auto id = ApiClient::CloudPlaylistId{a.number("id", 0)};
        command = [&api, id] { return api.deletePlaylist(id); };
    }
    return bool(command);
}
} // namespace EchoMusic::PluginBusiness
