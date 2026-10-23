#include "EchoPluginBusiness_p.h"
namespace EchoMusic::PluginBusiness
{
bool account(const QString &method, Arguments &a, ApiClient &api, std::function<quint64()> &command)
{
    const auto &arguments = a.values;
    if (method == "userDetail") {
        command = [&api] { return api.userDetail(); };
    }
    if (method == "userInfo") {
        command = [&api] { return api.userInfo(); };
    }
    if (method == "userVipDetail") {
        command = [&api] { return api.userVipDetail(); };
    }
    if (method == "userGradeInfo") {
        auto userId =
            (a.values.contains("userId") ? std::optional<quint64>(a.number("userId"))
                                         : (a.used.insert("userId"), std::optional<quint64>()));
        auto automaticVerification = a.boolean("automaticVerification", true);
        command = [&api, userId, automaticVerification] {
            return api.userGradeInfo(userId, automaticVerification);
        };
    }
    if (method == "loginDevices") {
        command = [&api] { return api.loginDevices(); };
    }
    if (method == "uploadUserAvatar") {
        if (!arguments.contains("image"))
            a.valid = false;

        auto image = a.bytes("image");
        command = [&api, image] { return api.uploadUserAvatar(image); };
    }
    if (method == "updateUserAvatar") {
        if (!arguments.contains("fileName"))
            a.valid = false;
        auto fileName = a.text("fileName", {});
        command = [&api, fileName] { return api.updateUserAvatar(fileName); };
    }
    if (method == "listeningPreferences") {
        command = [&api] { return api.listeningPreferences(); };
    }
    if (method == "userCloud") {
        auto page = a.number("page", 1, 4294967295ULL);
        auto pageSize = a.number("pageSize", 30, 4294967295ULL);
        command = [&api, page, pageSize] { return api.userCloud(page, pageSize); };
    }
    if (method == "cloudMatch") {
        if (!arguments.contains("hash"))
            a.valid = false;

        auto hash = a.text("hash", {});
        command = [&api, hash] { return api.cloudMatch(hash); };
    }
    if (method == "userVideoCollect") {
        auto page = a.number("page", 1, 4294967295ULL);
        auto pageSize = a.number("pageSize", 30, 4294967295ULL);
        command = [&api, page, pageSize] { return api.userVideoCollect(page, pageSize); };
    }
    if (method == "purchasedSongs") {
        auto page = a.number("page", 1, 4294967295ULL);
        auto pageSize = a.number("pageSize", 30, 4294967295ULL);
        command = [&api, page, pageSize] { return api.purchasedSongs(page, pageSize); };
    }
    if (method == "purchasedAlbums") {
        auto page = a.number("page", 1, 4294967295ULL);
        auto pageSize = a.number("pageSize", 30, 4294967295ULL);
        command = [&api, page, pageSize] { return api.purchasedAlbums(page, pageSize); };
    }
    if (method == "userVerify") {
        command = [&api] { return api.userVerify(); };
    }
    if (method == "kickLoginDevice") {
        if (!arguments.contains("target"))
            a.valid = false;
        auto target = parseLoginDeviceTarget(a.object("target"), a.valid);
        command = [&api, target] { return api.kickLoginDevice(target); };
    }
    if (method == "updateUserProfile") {
        if (!arguments.contains("patch"))
            a.valid = false;
        auto patch = parseProfilePatch(a.object("patch"), a.valid);
        command = [&api, patch] { return api.updateUserProfile(patch); };
    }
    if (method == "updateListeningPreferences") {
        if (!arguments.contains("patch"))
            a.valid = false;
        auto patch = parsePreferencePatch(a.object("patch"), a.valid);
        command = [&api, patch] { return api.updateListeningPreferences(patch); };
    }
    if (method == "userCloudDelete") {
        if (!arguments.contains("targets"))
            a.valid = false;
        auto targets = [&] {
            QList<ApiClient::CloudDeleteTarget> out;
            for (const auto &entry : a.array("targets")) {
                if (entry.metaType().id() != QMetaType::QVariantMap)
                    a.valid = false;
                out.append(parseCloudDeleteTarget(entry.toMap(), a.valid));
            }
            return out;
        }();
        command = [&api, targets] { return api.userCloudDelete(targets); };
    }
    if (method == "cloudSource") {
        if (!arguments.contains("hash"))
            a.valid = false;
        auto hash = a.text("hash", "");
        auto songId = a.number("songId", 0);
        auto audioId = a.number("audioId", 0);
        auto name = a.text("name", "");
        command = [&api, hash, songId, audioId, name] {
            return api.cloudSource(QJsonObject{{"hash", hash},
                                               {"song_id", qint64(songId)},
                                               {"audio_id", qint64(audioId)},
                                               {"name", name}});
        };
    }

    return bool(command);
}
} // namespace EchoMusic::PluginBusiness
