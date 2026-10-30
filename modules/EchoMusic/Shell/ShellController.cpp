#include "ShellController.h"
#include "ShellSettings.h"
#include "WindowController.h"
#include "ThemeStore.h"
#include "SongCommands.h"
#include "SongShareText.h"
#include <QClipboard>
#include <QGuiApplication>
#include <QThread>
namespace EchoMusic
{
bool ShellController::invokePlayerCommand(PlayerCommand command)
{
    const auto handle = m_playerHandles.value(command);
    return !m_closed && !handle.key.isEmpty() && m_playerbar.activate(handle.key);
}
ShellController::~ShellController()
{
    shutdown();
}
ShellController::ShellController(EchoMusic::SettingsStore &settings, RouteController &router,
                                 QObject *parent)
    : QObject(parent), m_router(router), m_titlebar(settings, ShellActionModel::Titlebar, this),
      m_playerbar(settings, ShellActionModel::Playerbar, this), m_tasks(this), m_toasts(this)
{
    using M = ShellActionModel;
    const auto titlebar = [&](QString id, QString title, QString icon, M::Zone zone, int order) {
        M::Contribution contribution;
        contribution.key = "host/" + id;
        contribution.id = id;
        contribution.title = title;
        contribution.icon = icon;
        contribution.defaultZone = zone;
        contribution.order = order;
        contribution.invoke = [this, id] {
            if (m_closed)
                return;
            if (id == "tasks")
                emit taskCenterRequested();
            else
                m_router.navigate(id);
        };
        m_titlebarHandles.push_back(m_titlebar.registerAction(*this, std::move(contribution)));
    };
    titlebar("recognize", tr("听歌识曲"), "microphone", M::Toolbar, 10);
    titlebar("tasks", tr("任务中心"), "clipboard-list", M::Toolbar, 20);
    titlebar("free-listen", tr("免费听"), "gift", M::More, 25);
    titlebar("listen-together", tr("一起听"), "headphones", M::More, 30);
    titlebar("discover-flow", tr("刷歌"), "vinyl", M::More, 35);
    titlebar("plugin-management", tr("插件管理"), "puzzle", M::More, 40);
    connect(&router, &RouteController::registryChanged, this, &ShellController::refreshTitlebar);
    connect(&m_tasks, &TaskCenterModel::countChanged, this, &ShellController::refreshTitlebar);
    refreshTitlebar();
    // These final action definitions are shared by buttons and the layout editor.
    // Playback providers bind through bindPlayerAction with a real command/state;
    // no empty command is enabled before those providers are connected.
    const auto playerbar = [&](PlayerCommand command, QString id, QString title, QString icon,
                               M::Zone zone, int order, bool visible = true) {
        M::Contribution contribution;
        contribution.key = "host/" + id;
        contribution.id = id;
        contribution.title = title;
        contribution.icon = icon;
        contribution.defaultZone = zone;
        contribution.order = order;
        contribution.visible = visible;
        contribution.supportsBadge =
            id == "quality" || id == "effect" || id == "desktop-lyric" || id == "queue";
        contribution.invoke = [] {};
        m_playerDefinitions.insert(command, contribution);
        m_playerHandles.insert(command, m_playerbar.registerAction(*this, std::move(contribution)));
    };
    playerbar(SleepTimer, "sleep-timer", tr("定时关闭"), "moon", M::Center, 1);
    playerbar(PlayMode, "play-mode", tr("播放模式"), "repeat", M::Center, 2);
    playerbar(Previous, "previous", tr("上一首"), "player-skip-back", M::Center, 3);
    playerbar(PlayToggle, "play-toggle", tr("播放"), "play", M::Center, 4);
    playerbar(Next, "next", tr("下一首"), "player-skip-forward", M::Center, 5);
    playerbar(Volume, "volume", tr("音量"), "volume", M::Center, 6);
    playerbar(Speed, "speed", tr("倍速"), "gauge", M::Center, 7);
    playerbar(Favorite, "favorite", tr("收藏"), "heart", M::Left, 8);
    playerbar(AddToPlaylist, "add-to-playlist", tr("添加到"), "text-plus", M::Left, 10, false);
    playerbar(Comments, "comments", tr("详情及评论"), "message-circle", M::Left, 20, false);
    playerbar(Mv, "mv", tr("播放 MV"), "brand-youtube", M::Left, 30, false);
    playerbar(Share, "share", tr("分享"), "share", M::More, 40, false);
    playerbar(Quality, "quality", tr("音质"), "wave-sine", M::Right, 42);
    playerbar(Effect, "effect", tr("音效"), "adjustments-horizontal", M::Right, 44);
    playerbar(DesktopLyric, "desktop-lyric", tr("桌面歌词"), "typography", M::Right, 50);
    playerbar(Cast, "cast", tr("投放"), "cast", M::More, 55);
    playerbar(Queue, "queue", tr("播放队列"), "list", M::Right, 60);
}
void ShellController::attachTheme(ThemeStore &theme)
{
    Q_ASSERT(theme.thread() == thread());
    disconnect(m_themeFailure);
    if (m_closed)
        return;
    m_themeFailure =
        connect(&theme, &ThemeStore::backgroundUnavailable, this,
                [this](const QString &message) { m_toasts.show(message, ToastService::Warning); });
}
void ShellController::refreshTitlebar()
{
    for (const auto &handle : m_titlebarHandles) {
        const auto id = handle.key.mid(5);
        m_titlebar.setState(handle, !m_closed && (id == "tasks" || m_router.available(id)), true,
                            false,
                            id == "tasks" && m_tasks.count() ? QStringLiteral("•") : QString());
    }
}
void ShellController::bindBarrageSettings(ShellSettings &settings)
{
    if (m_barrageSettings)
        disconnect(m_barrageSettings, nullptr, this, nullptr);
    m_barrageSettings = &settings;
    connect(&settings, &ShellSettings::changed, this, &ShellController::updateBarrageAction);
    updateBarrageAction();
}
void ShellController::updateBarrageAction()
{
    if (!m_lyricBarrageHandle.generation)
        return;
    const bool available = m_playback && !m_playback->currentTrack().hash.isEmpty();
    m_playerbar.setState(m_lyricBarrageHandle, true, available, false, {},
                         m_barrageSettings && m_barrageSettings->lyricBarrageEnabled());
}
void ShellController::setLyricPageActive(bool active)
{
    if (m_closed || active == (m_lyricSkinHandle.generation != 0))
        return;
    // 播放页使用「评论」，主播放栏使用「详情及评论」。
    m_playerbar.setPresentation(m_playerHandles.value(Comments),
                                active ? tr("评论") : tr("详情及评论"), "message-circle");
    if (!active) {
        m_playerbar.unregisterAction(m_lyricBarrageHandle);
        m_lyricBarrageHandle = {};
        m_playerbar.unregisterAction(m_lyricSkinHandle);
        m_lyricSkinHandle = {};
        return;
    }
    // LyricPlayerControls adds lyric-skins at order35, before quality/effect.
    // Its existing host projection owns layout, overflow and persisted placement.
    ShellActionModel::Contribution skin;
    skin.key = "host/lyric-skins";
    skin.id = "lyric-skins";
    skin.title = tr("换肤");
    skin.icon = "shirt";
    skin.defaultZone = ShellActionModel::Right;
    skin.order = 35;
    skin.enabled = true;
    skin.invoke = [this] { emit playbackPopupRequested("lyric-skins"); };
    m_lyricSkinHandle = m_playerbar.registerAction(*this, std::move(skin));
    ShellActionModel::Contribution barrage;
    barrage.key = "host/barrage";
    barrage.id = "barrage";
    barrage.title = tr("弹幕");
    barrage.icon = "barrage";
    barrage.defaultZone = ShellActionModel::Left;
    barrage.order = 32;
    barrage.invoke = [this] { emit playbackPopupRequested("barrage"); };
    m_lyricBarrageHandle = m_playerbar.registerAction(*this, std::move(barrage));
    updateBarrageAction();
}
void ShellController::shutdown()
{
    if (m_closed)
        return;
    if (m_lyricSkinHandle.generation) {
        m_playerbar.unregisterAction(m_lyricBarrageHandle);
        m_lyricBarrageHandle = {};
        m_playerbar.unregisterAction(m_lyricSkinHandle);
        m_lyricSkinHandle = {};
    }
    m_closed = true;
    m_tasks.shutdown();
    m_toasts.shutdown();
    disconnect(m_themeFailure);
    refreshTitlebar();
    for (const auto &connection : m_playerConnections)
        disconnect(connection);
    m_playerConnections.clear();
    for (const auto &handle : m_playerHandles)
        m_playerbar.unregisterAction(handle);
}
ShellActionModel::Handle ShellController::bindPlayerAction(QObject &provider, PlayerCommand command,
                                                           std::function<void()> invoke)
{
    if (m_closed || provider.thread() != thread() || !invoke ||
        !m_playerDefinitions.contains(command))
        return {};
    disconnect(m_playerConnections.take(command));
    auto contribution = m_playerDefinitions.value(command);
    const QPointer<QObject> owner = &provider;
    contribution.invoke = [this, owner, invoke = std::move(invoke)] {
        if (!m_closed && owner)
            invoke();
    };
    const auto handle = m_playerbar.registerAction(*this, std::move(contribution));
    m_playerHandles.insert(command, handle);
    m_playerConnections.insert(
        command, connect(&provider, &QObject::destroyed, this, [this, command, handle] {
            if (m_closed || m_playerHandles.value(command).generation != handle.generation)
                return;
            m_playerConnections.remove(command);
            m_playerHandles.insert(
                command, m_playerbar.registerAction(*this, m_playerDefinitions.value(command)));
        }));
    return handle;
}
void ShellController::attachWindows(WindowController &window)
{
    auto *floating = window.floating();
    const auto handle =
        bindPlayerAction(*floating, DesktopLyric, [floating] { floating->toggleDesktop(); });
    const auto update = [this, floating, handle] {
        m_playerbar.setState(handle, true, true, false,
                             floating->desktopEnabled() ? QStringLiteral("ON")
                                                        : QStringLiteral("OFF"),
                             floating->desktopEnabled());
    };
    connect(floating, &FloatingPlayerController::changed, this, update);
    update();
}
void ShellController::attachPlayback(PlaybackService &service)
{
    if (m_closed || m_playback)
        return;
    m_playback = &service;
    connect(&service, &PlaybackService::failureNotice, this,
            [this](const QString &message, const QString &title) {
                m_toasts.show(message, ToastService::Danger, 6000, ToastService::Standard, title);
            });
    connect(&service, &PlaybackService::sourceOperationFailed, this,
            [this](const QString &message, const QString &title) {
                m_toasts.show(message, ToastService::Warning, 6000, ToastService::Standard, title);
            });
    connect(&service, &PlaybackService::notice, this, [this](const QString &message, int tone) {
        m_toasts.show(message, ToastService::Tone(tone));
    });
    connect(service.sleepTimer(), &SleepTimer::executionFailed, this,
            [this](const QString &message) { m_toasts.show(message, ToastService::Danger, 8000); });
    connect(service.lyricText(), &LyricTextController::copied, this,
            [this] { m_toasts.show(tr("歌词已复制"), ToastService::Success); });
    connect(service.lyricText(), &LyricTextController::notice, this,
            [this](const QString &message) { m_toasts.show(message, ToastService::Success); });
    auto *catalog = service.effectCatalog();
    connect(catalog, &AudioEffectCatalog::notice, this,
            [this](const QString &message, bool warning) {
                m_toasts.show(message, warning ? ToastService::Warning : ToastService::Success);
            });
    connect(catalog, &AudioEffectCatalog::downloaded, this,
            [this, catalog](const QString &identity, const QString &name) {
                m_toasts.showAction(
                    *catalog, tr("“%1”已下载").arg(name), tr("使用"),
                    [catalog, identity] { catalog->useDownloaded(identity); },
                    ToastService::Success);
            });
    connect(service.cast(), &CastService::notice, this,
            [this](const QString &message) { m_toasts.show(message, ToastService::Info); });
    connect(
        service.audioControls(), &PlaybackAudioControls::selectedOutputDisconnected, this,
        [this] { m_toasts.show(tr("音频输出设备已断开，已暂停播放"), ToastService::Info, 4200); });
    connect(service.songCommands(), &SongCommands::artistRequested, this,
            [this](const QString &id) { m_router.navigate("artist-detail", {}, id); });
    connect(service.songCommands(), &SongCommands::albumRequested, this,
            [this](const QString &id) { m_router.navigate("album-detail", {}, id); });
    connect(service.songCommands(), &SongCommands::detailsRequested, this,
            [this](const PlaybackTrack &track) { openSongDetails(track); });
    connect(service.songCommands(), &SongCommands::mvRequested, this,
            [this](const PlaybackTrack &track) { openMv(track); });
    connect(service.transition(), &TrackTransitionController::notice, this,
            [this](const QString &message) { m_toasts.show(message, ToastService::Info, 3200); });
    connect(service.audioControls(), &PlaybackAudioControls::fileNotice, this,
            [this](const QString &message, bool success) {
                m_toasts.show(message, success ? ToastService::Success : ToastService::Warning);
            });
    connect(service.lyricSource(), &LyricSourceController::notice, this,
            [this](const QString &message, bool success) {
                m_toasts.show(message, success ? ToastService::Success : ToastService::Warning);
            });
    bindPlayerAction(service, Previous, [&service] { service.previous(); });
    bindPlayerAction(service, Next, [&service] { service.next(); });
    bindPlayerAction(service, PlayToggle, [&service] { service.toggle(); });
    bindPlayerAction(service, PlayMode, [this] { emit playbackPopupRequested("play-mode"); });
    bindPlayerAction(service, SleepTimer, [this] { emit playbackPopupRequested("sleep-timer"); });
    bindPlayerAction(service, Comments, [this] { openComments(); });
    bindPlayerAction(service, Mv, [this] { openMv(); });
    bindPlayerAction(service, Share, [this] {
        if (m_playback)
            m_playback->songCommands()->share(m_playback->currentTrack());
    });
    // 右键/播放栏菜单共用的 toast（分享链接、复制信息、不感兴趣）从唯一命令服务发出。
    connect(service.songCommands(), &SongCommands::notice, this,
            [this](const QString &message, int tone) {
                m_toasts.show(message, ToastService::Tone(tone));
            });
    for (auto pair : {std::pair{Volume, QString("volume")}, std::pair{Speed, QString("speed")},
                      std::pair{Effect, QString("effect")}, std::pair{Quality, QString("quality")},
                      std::pair{Queue, QString("queue")}, std::pair{Cast, QString("cast")}})
        bindPlayerAction(service, pair.first,
                         [this, kind = pair.second] { emit playbackPopupRequested(kind); });
    const auto updateCast = [this, &service] {
        auto *output = service.cast();
        m_playerbar.setState(m_playerHandles.value(Cast), true, true, output->busy(),
                             output->remote() ? QStringLiteral("ON") : QString(), output->remote());
        m_playerbar.setPresentation(
            m_playerHandles.value(Cast),
            output->remote() ? tr("正在投放到%1").arg(output->activeTitle()) : tr("投放"), "cast");
    };
    connect(service.cast(), &CastService::changed, this, updateCast);
    updateCast();
    if (auto *actions = service.libraryActions()) {
        connect(actions, &PlaybackLibraryActions::notice, this,
                [this](const QString &message, bool warning) {
                    m_toasts.show(message, warning ? ToastService::Warning : ToastService::Success);
                });
        bindPlayerAction(*actions, Favorite, [actions] { actions->toggleFavorite(); });
        bindPlayerAction(*actions, AddToPlaylist, [actions] { actions->prepareCurrent(); });
        const auto updateLibrary = [this, &service, actions] {
            m_playerbar.setState(m_playerHandles.value(Favorite),
                                 service.hasTrack() && !actions->busy() &&
                                     !actions->favoriteLoading(),
                                 true, false, {}, actions->favorite());
            m_playerbar.setPresentation(m_playerHandles.value(Favorite),
                                        actions->favorite() ? tr("取消收藏") : tr("收藏"),
                                        actions->favorite() ? "heart-filled" : "heart");
            // 只有已登录且存在当前曲目时才显示添加到歌单入口；收藏/添加
            // 请求期间 busy 只禁用按钮，不隐藏，避免动作栏跳动（上游
            // issue #18）。
            const bool canShowAdd = actions->signedIn() && service.hasTrack();
            const bool canAdd = canShowAdd && !actions->busy();
            m_playerbar.setState(m_playerHandles.value(AddToPlaylist), canAdd, canShowAdd, false,
                                 {});
        };
        connect(actions, &PlaybackLibraryActions::changed, this, updateLibrary);
        updateLibrary();
    }
    const auto update = [this, &service] {
        // Song and provider effects take priority over IR and EQ.
        // Project existing services rather than retaining another mutable effect state.
        QString effectBadge;
        auto *audio = service.audioControls();
        if (service.hasTrack()) {
            if (service.appliedSongEffect() != "none" ||
                (!audio->providerId().isEmpty() && !audio->providerPresetId().isEmpty()))
                effectBadge = "FX";
            else if (audio->impulseEnabled() && !audio->impulseResponse().isEmpty())
                effectBadge = "IR";
            else {
                for (const auto &gain : audio->gains()) {
                    if (gain.toDouble() != 0.) {
                        effectBadge = "EQ";
                        break;
                    }
                }
            }
        }
        for (auto command :
             {Previous, Next, PlayToggle, PlayMode, Volume, Speed, Quality, Queue, Effect}) {
            const bool enabled = (command != Speed || !service.cast()->remoteMedia()) &&
                                 (command == Volume || command == Speed || command == Queue ||
                                  command == PlayMode || command == Effect ||
                                  (command == Previous || command == Next ? !service.loading()
                                   : command == PlayToggle                ? !service.loading()
                                                                          : service.hasTrack()));
            m_playerbar.setState(m_playerHandles.value(command), enabled, true, false,
                                 command == Queue
                                     ? service.queue()->rowCount() > 99
                                           ? QString("99+")
                                           : QString::number(service.queue()->rowCount())
                                 : command == Quality && service.hasTrack()
                                     ? (QMap<QString, QString>{{"cloud", "CLD"},
                                                               {"mv", "MV"},
                                                               {"standard", "SD"},
                                                               {"higher", "HQ"},
                                                               {"flac", "SQ"},
                                                               {"hi_res", "HR"},
                                                               {"viper_tape", "VPT"},
                                                               {"viper_clear", "VPC"},
                                                               {"viper_atmos", "VPA"}}
                                            .value(service.effectiveQuality()))
                                 : command == Effect ? effectBadge
                                                     : QString());
        }
        // 详情、评论和 MV 动作要求目标页面可用；分享要求有效 hash。
        // 无当前曲目时隐藏这些动作。
        const auto &track = service.currentTrack();
        const bool commentsReady =
            service.hasTrack() && m_router.available(QStringLiteral("song-detail"));
        const bool mvReady = service.hasTrack() && track.songId != 0 &&
                             m_router.available(QStringLiteral("mv-detail"));
        const bool shareReady = service.hasTrack() && isSongShareHash(track.hash);
        m_playerbar.setState(m_playerHandles.value(Comments), commentsReady, commentsReady, false,
                             {});
        m_playerbar.setState(m_playerHandles.value(Mv), mvReady, mvReady, false, {});
        m_playerbar.setState(m_playerHandles.value(Share), shareReady, shareReady, false, {});
        m_playerbar.setPresentation(m_playerHandles.value(Volume), tr("音量"),
                                    service.volume() == 0   ? "volume-3"
                                    : service.volume() < 50 ? "volume-2"
                                                            : "volume");
        m_playerbar.setPresentation(m_playerHandles.value(PlayToggle),
                                    service.playing() ? tr("暂停") : tr("播放"),
                                    service.playing() ? "pause" : "play");
        const QStringList names{tr("列表循环"), tr("单曲循环"), tr("随机播放"), tr("顺序播放")};
        const QStringList icons{"repeat", "repeat-once", "arrows-shuffle", "repeat-off"};
        m_playerbar.setPresentation(m_playerHandles.value(PlayMode), names[int(service.playMode())],
                                    icons[int(service.playMode())]);
        m_playerbar.setState(m_playerHandles.value(SleepTimer), true, true, false, {},
                             service.sleepTimer()->active());
    };
    connect(&service, &PlaybackService::changed, this, update);
    connect(service.audioControls(), &PlaybackAudioControls::changed, this, update);
    connect(service.queue(), &PlaybackQueueModel::changed, this, update);
    connect(service.sleepTimer(), &EchoMusic::SleepTimer::changed, this, update);
    // 目标页面注册/注销会改变详情及评论与 MV 的可用性。
    connect(&m_router, &RouteController::registryChanged, this, update);
    update();
    emit playbackChanged();
}
void ShellController::openComments()
{
    if (m_closed || !m_playback || !m_playback->hasTrack())
        return;
    if (m_lyricSkinHandle.generation)
        emit playbackPopupRequested("comments");
    else
        openSongDetails(m_playback->currentTrack());
}
bool ShellController::openSongDetails(const PlaybackTrack &track, bool replace)
{
    if (m_closed || track.id.isEmpty() || !m_router.available("song-detail"))
        return false;
    QVariantMap query;
    query.insert("mainTab", "detail");
    query.insert("type", "music");
    if (!track.hash.isEmpty())
        query.insert("hash", track.hash);
    if (track.songId)
        query.insert("mixSongId", QString::number(track.songId));
    if (track.albumId)
        query.insert("albumId", QString::number(track.albumId));
    if (!track.album.isEmpty())
        query.insert("album", track.album);
    if (!track.title.isEmpty())
        query.insert("title", track.title);
    if (!track.artist.isEmpty())
        query.insert("artist", track.artist);
    const auto artists = track.catalogMetadata.value("artist_refs").toArray();
    if (!artists.isEmpty()) {
        const auto id = artists[0].toObject().value("id").toVariant().toString();
        if (id.toULongLong())
            query.insert("artistId", id);
    }
    if (!track.cover.isEmpty())
        query.insert("cover", track.cover);
    const QPointer<RouteContext> origin = m_router.current();
    emit songDetailOpening(track);
    if (replace && (m_closed || !origin || m_router.current() != origin))
        return false;
    const auto id = track.songId ? QString::number(track.songId) : track.id;
    return replace ? m_router.replace("song-detail", query, id)
                   : m_router.navigate("song-detail", query, id);
}
void ShellController::openMv()
{
    if (m_closed || !m_playback || !m_playback->hasTrack())
        return;
    openMv(m_playback->currentTrack());
}
void ShellController::openMv(const PlaybackTrack &track)
{
    if (m_closed || !m_router.available("mv-detail"))
        return;
    const auto albumAudioId = track.songId;
    if (!albumAudioId)
        return;
    QVariantMap query;
    for (const auto &video : track.catalogMetadata.value("videos").toArray()) {
        const auto hash = video.toObject().value("hash").toString();
        if (isSongShareHash(hash)) {
            query.insert("hash", hash);
            break;
        }
    }
    if (!track.title.isEmpty())
        query.insert("title", track.title);
    if (!track.artist.isEmpty())
        query.insert("artist", track.artist);
    if (!track.cover.isEmpty())
        query.insert("cover", track.cover);
    if (!track.album.isEmpty())
        query.insert("album", track.album);
    query.insert("albumAudioId", QString::number(albumAudioId));
    m_router.navigate("mv-detail", query, QString::number(albumAudioId));
}
void ShellController::copyCurrentSongInfo()
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (m_closed || !m_playback || !m_playback->hasTrack())
        return;
    const auto &track = m_playback->currentTrack();
    if (track.title.isEmpty())
        return;
    const auto text = track.artist.isEmpty() ? track.title : track.title + " - " + track.artist;
    QGuiApplication::clipboard()->setText(text);
    m_toasts.show(tr("已复制歌曲信息"), ToastService::Success);
}
} // namespace EchoMusic
