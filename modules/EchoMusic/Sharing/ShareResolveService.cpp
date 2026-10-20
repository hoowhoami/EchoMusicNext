#include "ShareResolveService.h"
#include "PlaybackService.h"
#include "ShellController.h"
#include "WindowController.h"
#include <QClipboard>
#include <QGuiApplication>
#include <QJsonArray>
#include <QQmlEngine>
#include <utility>

namespace EchoMusic
{
ShareResolveService::ShareResolveService(ApiClient &api, SharingCommands &commands,
                                         RouteController &router, ShellController &shell,
                                         WindowController &window, bool network, QObject *parent)
    : QObject(parent), m_api(api), m_commands(commands), m_router(router), m_shell(shell),
      m_window(window), m_network(network)
{
    QQmlEngine::setObjectOwnership(this, QQmlEngine::CppOwnership);
    connect(&router, &RouteController::pageActivated, this, [this](RouteContext *context) {
        if (context->name() == "share-resolve")
            begin(context->query());
        else {
            cancel();
            if (std::exchange(m_loading, false))
                emit changed();
        }
    });
    connect(&api, &ApiClient::succeeded, this, &ShareResolveService::accept);
    connect(&api, &ApiClient::failed, this,
            [this](quint64 id, int, const QByteArray &bytes) { reject(id, bytes); });
    connect(&api, &ApiClient::sessionChanged, this, [this] {
        cancel();
        if (active())
            begin(m_router.current()->query());
    });
    connect(&api, &ApiClient::closed, this, &ShareResolveService::shutdown);
    connect(&m_commands, &SharingCommands::copied, this,
            [this](const QString &text) { m_lastClipboardText = text.trimmed(); });
    connect(&window, &WindowController::mainWindowActivated, this,
            &ShareResolveService::inspectClipboard);
    m_spinnerTimer.setSingleShot(true);
    m_spinnerTimer.setInterval(220);
    connect(&m_spinnerTimer, &QTimer::timeout, this, [this] {
        if (m_loading && active()) {
            m_spinner = true;
            emit changed();
        }
    });
}
ShareResolveService::~ShareResolveService()
{
    shutdown();
}
bool ShareResolveService::active() const
{
    return !m_closed && m_router.current() && m_router.current()->name() == "share-resolve";
}
QString ShareResolveService::resourceLabel() const
{
    return m_target ? shareResourceLabel(m_target->type) : tr("内容");
}
QString ShareResolveService::targetId() const
{
    return m_target ? m_target->id : QString();
}
QString ShareResolveService::heading() const
{
    if (m_loading)
        return tr("正在打开分享的%1").arg(resourceLabel());
    const auto title = m_target ? m_target->query.value("title") : QString();
    return title.isEmpty() ? tr("分享的%1打不开").arg(resourceLabel())
                           : tr("分享的%1「%2」打不开").arg(resourceLabel(), title);
}
QString ShareResolveService::explanation() const
{
    if (m_loading)
        return tr("正在确认分享内容是否可用。");
    if (m_reason == "invalid")
        return tr("这个分享链接格式不正确，无法识别目标内容。");
    if (m_reason == "not-found")
        return tr("分享的%1可能已被删除、权限不可见，或当前数据源没有返回。").arg(resourceLabel());
    return tr("暂时没能获取到分享的%1，可能是网络或数据源异常。").arg(resourceLabel());
}
void ShareResolveService::setPluginOpener(std::function<bool(const ShareTarget &)> opener)
{
    m_pluginOpener = std::move(opener);
}
bool ShareResolveService::openUrl(const QUrl &url)
{
    return openText(url.toString(QUrl::FullyEncoded));
}
bool ShareResolveService::openText(const QString &text)
{
    if (m_closed)
        return false;
    const auto target = parseShareTarget(text);
    if (!target)
        return m_router.navigate("share-resolve", {{"invalid", "1"}});
    const auto appUrl = shareAppUrl(*target);
    if (appUrl.isEmpty())
        return m_router.navigate("share-resolve", {{"invalid", "1"}});
    return m_router.navigate("share-resolve", {{"target", appUrl.toString(QUrl::FullyEncoded)}});
}
void ShareResolveService::cancel()
{
    ++m_generation;
    m_spinnerTimer.stop();
    m_spinner = false;
    const auto pending = std::exchange(m_pending, {});
    for (auto id : pending.keys())
        m_api.cancel(id);
    m_roomPending = 0;
    m_roomDetail = {};
}
void ShareResolveService::fail(const QString &reason)
{
    cancel();
    m_loading = false;
    m_reason = reason;
    emit changed();
}
void ShareResolveService::watch(quint64 id, Kind kind)
{
    if (!id) {
        fail("load-failed");
        return;
    }
    m_pending.insert(id, {kind, m_generation, m_api.sessionRevision()});
}
void ShareResolveService::begin(const QVariantMap &query)
{
    cancel();
    m_target = parseShareTarget(query.value("target").toString());
    if (query.value("invalid").toBool() || !m_target) {
        fail("invalid");
        return;
    }
    m_loading = true;
    m_reason.clear();
    emit changed();
    m_spinnerTimer.start();
    if (m_target->type == ShareResource::Plugin) {
        if (!m_pluginOpener || !m_pluginOpener(*m_target))
            fail("load-failed");
        return;
    }
    if (!m_network) {
        fail("load-failed");
        return;
    }
    const auto &target = *m_target;
    switch (target.type) {
    case ShareResource::Song: {
        const auto album = target.query.value("albumId").toULongLong();
        watch(m_api.songPrivileges({{target.id, album ? std::optional(album) : std::nullopt}},
                                   {AuthenticationMode::CurrentAccount, true}),
              Kind::Song);
        break;
    }
    case ShareResource::Playlist:
        watch(m_api.playlistDetails({{target.id}}), Kind::Playlist);
        break;
    case ShareResource::Artist:
    case ShareResource::Album: {
        bool valid = false;
        const auto id = target.id.toULongLong(&valid);
        if (!valid || !id) {
            fail("invalid");
            return;
        }
        if (target.type == ShareResource::Artist)
            watch(m_api.artistDetail(id), Kind::Artist);
        else
            watch(m_api.albumDetail(id), Kind::Album);
        break;
    }
    case ShareResource::ListenTogether: {
        if (target.query.contains("roomType") && target.query.value("roomType") != "0") {
            fail("invalid");
            return;
        }
        m_roomPending = 2;
        ListenRoomRequest state;
        state.action = ListenRoomAction::State;
        state.roomId = target.id;
        watch(m_api.listenTogetherRoom(state), Kind::RoomState);
        if (!m_loading || !active())
            return;
        ListenMusicRequest detail;
        detail.action = ListenMusicAction::Detail;
        detail.roomId = target.id;
        watch(m_api.listenTogetherMusic(detail), Kind::RoomDetail);
        break;
    }
    case ShareResource::Plugin:
        break;
    }
}
void ShareResolveService::resolved(const QString &route, const QVariantMap &query,
                                   const QString &id)
{
    m_spinnerTimer.stop();
    if (!m_router.replace(route, query, id))
        fail("load-failed");
}
void ShareResolveService::accept(quint64 id, const QByteArray &bytes)
{
    if (!m_pending.contains(id))
        return;
    const auto pending = m_pending.take(id);
    if (!active() || !m_target || pending.generation != m_generation ||
        pending.session != m_api.sessionRevision())
        return;
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(bytes, &error);
    // The API's explicit optional resources serialize as null when absent.
    if (bytes.trimmed() == "null") {
        fail("not-found");
        return;
    }
    if (error.error != QJsonParseError::NoError) {
        fail("load-failed");
        return;
    }
    switch (pending.kind) {
    case Kind::Song: {
        const auto list = document.array();
        if (list.isEmpty()) {
            fail("not-found");
            return;
        }
        const auto resource = list.first().toObject();
        const auto primary = resource.value("primary").toObject();
        PlaybackTrack track;
        track.hash = resource.value("hash").toString();
        if (track.hash.isEmpty())
            track.hash = m_target->id;
        track.songId = resource.value("song_id").toVariant().toULongLong();
        if (!track.songId)
            track.songId = m_target->query.value("albumAudioId").toULongLong();
        track.id = track.songId ? QString::number(track.songId) : track.hash;
        track.albumId = resource.value("album_id").toVariant().toULongLong();
        if (!track.albumId)
            track.albumId = m_target->query.value("albumId").toULongLong();
        track.title = resource.value("display_name").toString();
        track.artist = resource.value("artist_text").toString();
        track.album = resource.value("album_title").toString();
        track.cover = primary.value("cover_url").toString();
        track.durationMs = quint64(primary.value("duration_ms").toInteger());
        track.catalogPrivilege = quint32(primary.value("privilege").toInt());
        track.catalogMetadata = {{"resource_privileges", resource},
                                 {"catalog_privilege", primary.value("privilege").isDouble()
                                                           ? primary.value("privilege")
                                                           : QJsonValue(QJsonValue::Null)},
                                 {"catalog_duration_ms", primary.value("duration_ms").isDouble()
                                                             ? primary.value("duration_ms")
                                                             : QJsonValue(QJsonValue::Null)}};
        if (!resource.value("language").isNull() && !resource.value("language").isUndefined())
            track.catalogMetadata.insert("language", resource.value("language"));
        m_spinnerTimer.stop();
        if (!m_shell.openSongDetails(track, true))
            fail("load-failed");
        break;
    }
    case Kind::Playlist:
        if (document.array().isEmpty())
            fail("not-found");
        else
            resolved("playlist-detail", {}, m_target->id);
        break;
    case Kind::Artist:
    case Kind::Album:
        resolved(pending.kind == Kind::Artist ? "artist-detail" : "album-detail", {}, m_target->id);
        break;
    case Kind::RoomState:
        if (document.object().value("active_code").isDouble() &&
            document.object().value("active_code").toInt() != 1) {
            fail("not-found");
            return;
        }
        --m_roomPending;
        finishRoom();
        break;
    case Kind::RoomDetail:
        m_roomDetail = document.object();
        if (m_roomDetail.value("active_code").isDouble() &&
            m_roomDetail.value("active_code").toInt() != 1) {
            fail("not-found");
            return;
        }
        --m_roomPending;
        finishRoom();
        break;
    }
}
void ShareResolveService::finishRoom()
{
    if (m_roomPending || !m_target)
        return;
    auto name = m_roomDetail.value("name").toString();
    if (name.isEmpty())
        name = m_target->query.value("roomName");
    resolved("listen-together", {{"roomId", m_target->id}, {"roomType", "0"}, {"roomName", name}});
}
void ShareResolveService::reject(quint64 id, const QByteArray &bytes)
{
    if (!m_pending.contains(id))
        return;
    const auto pending = m_pending.take(id);
    if (!active() || pending.generation != m_generation ||
        pending.session != m_api.sessionRevision())
        return;
    const auto error = QJsonDocument::fromJson(bytes).object();
    const auto code = error.value("code").toVariant().toInt();
    const auto message = error.value("message").toString();
    const bool missingRoom =
        m_target && m_target->type == ShareResource::ListenTogether &&
        (code == 55006 || code == 20005 ||
         (code == 20002 &&
          (message.contains(QStringLiteral("群组")) || message.contains(QStringLiteral("房间"))) &&
          (message.contains(QStringLiteral("不存在")) ||
           message.contains(QStringLiteral("已解散")))));
    fail(missingRoom ? "not-found" : "load-failed");
}
void ShareResolveService::retry()
{
    if (active())
        begin(m_router.current()->query());
}
void ShareResolveService::goHome()
{
    m_router.navigateHome(true);
}
void ShareResolveService::goBack()
{
    if (m_router.canGoBack())
        m_router.back();
    else
        goHome();
}
void ShareResolveService::startClipboardMonitoring()
{
    m_clipboardMonitoring = true;
    inspectClipboard();
}
void ShareResolveService::inspectClipboard()
{
    if (!m_clipboardMonitoring || m_closed || !m_window.mainWindowActive() || !qGuiApp)
        return;
    const auto text = QGuiApplication::clipboard()->text().trimmed();
    if (text == m_lastClipboardText)
        return;
    const auto target = parseShareTarget(text);
    if (!target) {
        m_lastClipboardText.clear();
        return;
    }
    m_lastClipboardText = text;
    m_shell.toasts()->showAction(
        *this, tr("检测到 EchoMusic %1分享").arg(shareResourceLabel(target->type)), tr("打开"),
        [this, target = *target] { openUrl(shareAppUrl(target)); });
}
void ShareResolveService::shutdown()
{
    if (m_closed)
        return;
    m_closed = true;
    cancel();
    m_clipboardMonitoring = m_loading = false;
    m_pluginOpener = {};
}
} // namespace EchoMusic

namespace EchoMusic
{
void ShareResolveService::registerRoutes(RouteController &router)
{
    router.registerPage({"share-resolve",
                         "/main/share",
                         QUrl("qrc:/qt/qml/EchoMusic/Sharing/ShareResolvePage.qml"),
                         {},
                         false,
                         false,
                         false,
                         {{"service", QVariant::fromValue(this)}}});
}
} // namespace EchoMusic
