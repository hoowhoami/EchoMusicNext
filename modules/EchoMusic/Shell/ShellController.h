#pragma once
#include "ShellActionModel.h"
#include "PluginSurfaceModel.h"
#include "TaskCenterModel.h"
#include "ToastService.h"
#include "RouteController.h"
#include "PlaybackService.h"

namespace EchoMusic
{
class WindowController;
class ThemeStore;
/// Composition of shared shell commands/presentation. Settings own preferences;
/// providers own playback/account jobs. QML presents these single-owner models.
class ShellSettings;
class ShellController final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by the application")
    Q_PROPERTY(EchoMusic::ShellActionModel *titlebarActions READ titlebarActions CONSTANT)
    Q_PROPERTY(EchoMusic::ShellActionModel *playerbarActions READ playerbarActions CONSTANT)
    Q_PROPERTY(EchoMusic::PluginSurfaceModel *pluginSurfaces READ pluginSurfaces CONSTANT)
    Q_PROPERTY(EchoMusic::TaskCenterModel *tasks READ tasks CONSTANT)
    Q_PROPERTY(EchoMusic::ToastService *toasts READ toasts CONSTANT)
    Q_PROPERTY(EchoMusic::PlaybackService *playback READ playback NOTIFY playbackChanged)
public:
    enum PlayerCommand
    {
        SleepTimer,
        PlayMode,
        Previous,
        PlayToggle,
        Next,
        Volume,
        Speed,
        Favorite,
        AddToPlaylist,
        Comments,
        Mv,
        Share,
        Quality,
        Effect,
        DesktopLyric,
        Cast,
        Queue
    };
    Q_ENUM(PlayerCommand)
    explicit ShellController(EchoMusic::SettingsStore &settings, RouteController &router,
                             QObject *parent = nullptr);
    ~ShellController() override;
    ShellActionModel *titlebarActions()
    {
        return &m_titlebar;
    }
    ShellActionModel *playerbarActions()
    {
        return &m_playerbar;
    }
    PluginSurfaceModel *pluginSurfaces()
    {
        return &m_pluginSurfaces;
    }
    TaskCenterModel *tasks()
    {
        return &m_tasks;
    }
    ToastService *toasts()
    {
        return &m_toasts;
    }
    /// Trusted GUI-thread provider binding. Unload invalidates its handle and
    /// restores the disabled host definition; preferences survive that change.
    ShellActionModel::Handle bindPlayerAction(QObject &provider, PlayerCommand command,
                                              std::function<void()> invoke);
    Q_INVOKABLE void setLyricPageActive(bool active);
    void bindBarrageSettings(ShellSettings &);
    bool invokePlayerCommand(PlayerCommand command);
    void shutdown();
    void attachPlayback(PlaybackService &playback);
    void attachWindows(WindowController &window);
    void attachTheme(ThemeStore &theme);
    /// Share resolution uses the same typed metadata handoff and detail route,
    /// replacing only its transient resolver entry in native history.
    bool openSongDetails(const PlaybackTrack &, bool replace = false);
    /// Copy the current service-owned display identity on the GUI thread.
    Q_INVOKABLE void copyCurrentSongInfo();
    PlaybackService *playback() const
    {
        return m_playback;
    }
signals:
    void taskCenterRequested();
    /// Native entry facts are handed off before navigation; URL queries stay scalar.
    void songDetailOpening(const EchoMusic::PlaybackTrack &track);
    void playbackChanged();
    void playbackPopupRequested(const QString &kind);

private:
    void refreshTitlebar();
    /// 通过共享路由打开评论或 MV；目标页不可用时隐藏入口。
    void openComments();
    void openMv();
    void openMv(const PlaybackTrack &track);
    RouteController &m_router;
    ShellActionModel m_titlebar, m_playerbar;
    PluginSurfaceModel m_pluginSurfaces;
    TaskCenterModel m_tasks;
    ToastService m_toasts;
    QMetaObject::Connection m_themeFailure;
    QList<ShellActionModel::Handle> m_titlebarHandles;
    QHash<int, ShellActionModel::Contribution> m_playerDefinitions;
    QHash<int, ShellActionModel::Handle> m_playerHandles;
    QHash<int, QMetaObject::Connection> m_playerConnections;
    ShellActionModel::Handle m_lyricSkinHandle, m_lyricBarrageHandle;
    QPointer<ShellSettings> m_barrageSettings;
    void updateBarrageAction();
    bool m_closed = false;
    PlaybackService *m_playback = nullptr;
};
} // namespace EchoMusic
