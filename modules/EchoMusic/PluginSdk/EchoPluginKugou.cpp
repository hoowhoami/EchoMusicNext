#include "EchoPluginKugou.h"
#include "EchoPluginContext.h"
#include "ApiClient.h"
#include "AccountService.h"
#include "EchoPluginBusiness_p.h"
#include "VerificationService.h"
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <cmath>
namespace EchoMusic
{
using PluginBusiness::Arguments;
namespace
{
QVariant publicValue(const QVariant &value, const QString &key = {})
{
    if (value.metaType().id() == QMetaType::QVariantMap) {
        QVariantMap result;
        const auto map = value.toMap();
        for (auto i = map.begin(); i != map.end(); ++i)
            result.insert(i.key(), publicValue(i.value(), i.key()));
        return result;
    }
    if (value.metaType().id() == QMetaType::QVariantList) {
        QVariantList result;
        for (const auto &entry : value.toList())
            result.append(publicValue(entry, key));
        return result;
    }
    const bool identifier = key == "id" || key.endsWith("_id") || key.endsWith("_ids");
    if (identifier &&
        (value.metaType().id() == QMetaType::LongLong ||
         value.metaType().id() == QMetaType::ULongLong || value.metaType().id() == QMetaType::Int))
        return value.toString();
    return value;
}
} // namespace

EchoPluginKugou::EchoPluginKugou(EchoPluginContext &scope) : QObject(&scope), m_scope(scope)
{
    const auto changed = [this] {
        if (m_accountNoticePending)
            return;
        m_accountNoticePending = true;
        QMetaObject::invokeMethod(
            this,
            [this] {
                m_accountNoticePending = false;
                if (m_scope.active())
                    emit accountChanged();
            },
            Qt::QueuedConnection);
    };
    connect(&scope.host().api, &ApiClient::sessionChanged, this, changed);
    connect(&scope.host().account, &AccountService::sessionAdopted, this, changed);
    connect(&scope.host().api, &ApiClient::succeeded, this,
            [this](quint64 id, const QByteArray &payload) {
                auto op = m_pending.take(id);
                if (!op || op->finished())
                    return;
                QJsonParseError e;
                auto doc = QJsonDocument::fromJson(payload, &e);
                if (e.error != QJsonParseError::NoError)
                    op->fail("InvalidResponse", "Business response is invalid JSON");
                else
                    op->succeed(publicValue(doc.toVariant()));
            });
    connect(&scope.host().api, &ApiClient::failed, this,
            [this](quint64 id, int, const QByteArray &payload) {
                auto op = m_pending.take(id);
                if (!op || op->finished())
                    return;
                auto error = QJsonDocument::fromJson(payload).object();
                op->fail(error.value("kind").toString("ApiError"),
                         error.value("message").toString("Business request failed"));
            });
}
QVariantMap EchoPluginKugou::account() const
{
    return {{"userId", QString::number(m_scope.host().api.currentAccountUserId())},
            {"loggedIn", m_scope.host().api.currentAccountUserId() != 0},
            {"revision", QString::number(m_scope.accountEpoch())}};
}
EchoPluginOperation *EchoPluginKugou::submit(const std::function<quint64()> &start)
{
    auto *op = m_scope.operation();
    if (op->finished())
        return op;
    if (m_pending.size() >= 64) {
        op->fail("LimitExceeded", "Too many business requests");
        return op;
    }
    auto id = start();
    if (!id) {
        op->fail("Unavailable", "Business service is unavailable");
        return op;
    }
    m_scope.host().api.bindRequestToSession(id);
    m_pending.insert(id, op);
    op->start([this, id] {
        m_pending.remove(id);
        m_scope.host().api.cancelRequest(id);
    });
    return op;
}
EchoPluginOperation *EchoPluginKugou::verify(const QString &eventId)
{
    if (!m_scope.package().capabilities.contains("kugouVerification"))
        return m_scope.rejected("PermissionDenied", "kugouVerification capability is required");
    if (eventId.trimmed().isEmpty() || eventId.size() > 512 || !m_scope.host().verification)
        return m_scope.rejected("InvalidArgument", "Verification event is unavailable");
    return submit([&] { return m_scope.host().api.requestVerification(eventId); });
}
QStringList EchoPluginKugou::methods()
{
    return {"addPlaylistTracks",
            "albumDetail",
            "albumDynamicCover",
            "albumSongs",
            "artistAlbums",
            "artistDetail",
            "artistDirectory",
            "artistSongs",
            "audioEffects",
            "cloudMatch",
            "cloudPlaylistTracks",
            "cloudSource",
            "collectPlaylist",
            "collectVideo",
            "commentAuthorProducts",
            "commentCount",
            "comments",
            "commonHeadphoneEffect",
            "contentBlacklist",
            "createPlaylist",
            "dailyRecommendations",
            "deleteComment",
            "deletePlaylist",
            "discoverRecommendations",
            "editContentBlacklist",
            "editorPlaylists",
            "favoriteCount",
            "followArtist",
            "followUser",
            "freeListenSongs",
            "importPlaylistLink",
            "importPlaylistScreenshot",
            "kickLoginDevice",
            "listenTogetherChat",
            "listenTogetherMusic",
            "listenTogetherRoom",
            "listeningPreferences",
            "loginDevices",
            "lyric",
            "lyricCandidates",
            "lyricCandidatesByKeywords",
            "newAlbumCatalog",
            "newSongCatalog",
            "personalFm",
            "playbackSource",
            "playlistCategories",
            "playlistDetails",
            "playlistImportResult",
            "playlistImportStatus",
            "publicPlaylistTracks",
            "purchasedAlbums",
            "purchasedSongs",
            "rankCatalog",
            "rankSongs",
            "recommendedPlaylists",
            "recommendedRanks",
            "removePlaylistTracks",
            "replyComment",
            "search",
            "searchDefault",
            "searchHot",
            "searchSuggest",
            "sendComment",
            "sendSocialMessage",
            "sendSongBarrage",
            "sendVideoBarrage",
            "socialMessages",
            "socialUsers",
            "songBarrage",
            "songClimax",
            "songMetadata",
            "songMv",
            "songPrivileges",
            "songRanking",
            "sortPlaylistTracks",
            "sortPlaylists",
            "styleRecommendations",
            "submitPlaylistImportImage",
            "updateListeningPreferences",
            "updatePlaylist",
            "updateUserAvatar",
            "updateUserProfile",
            "uploadPlaylistCover",
            "userCloud",
            "userCloudDelete",
            "userDetail",
            "userGradeInfo",
            "userInfo",
            "userPlaylists",
            "userVerify",
            "userVideoCollect",
            "userVipDetail",
            "videoBarrage",
            "videoDetail",
            "videoPrivilege",
            "videoUrl"};
}
EchoPluginOperation *EchoPluginKugou::request(const QString &method, const QVariantMap &arguments)
{
    if (!m_scope.package().capabilities.contains("kugouApi"))
        return m_scope.rejected("PermissionDenied", "kugouApi capability is required");
    if (!m_scope.active())
        return m_scope.rejected("Unavailable", "Plugin scope has retired");
    Arguments a(arguments);
    auto &api = m_scope.host().api;
    std::function<quint64()> command;
    const bool known = PluginBusiness::catalog(method, a, api, command) ||
                       PluginBusiness::library(method, a, api, command) ||
                       PluginBusiness::social(method, a, api, command) ||
                       PluginBusiness::account(method, a, api, command) ||
                       PluginBusiness::media(method, a, api, command);
    if (!known || !command)
        return m_scope.rejected("UnknownMethod", "Unknown business method");
    if (!a.done())
        return m_scope.rejected("InvalidArgument", "Invalid or unknown business arguments");
    return submit(command);
}
} // namespace EchoMusic
