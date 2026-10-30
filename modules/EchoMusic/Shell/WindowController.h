#pragma once
#include <QObject>
#include <QPointer>
#include <QRectF>
#include <QQuickWindow>
#include "SettingsStore.h"
#include "AppIconService.h"
#include <QTimer>
#include <QtQml/qqmlregistration.h>
#include "StartupIntegration.h"
#include "FloatingPlayerController.h"
#include "TaskbarButtonController.h"
#include "TaskbarLyricController.h"
#include "WindowBackdrop.h"
#include <memory>
class QSystemTrayIcon;
class QMenu;
class QMenuBar;
namespace EchoMusic
{
class PlaybackService;
class TrayIconTheme;
/// Native window lifetime/geometry, separate from the reusable shell layout.
class WindowController final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by the application")
    Q_PROPERTY(EchoMusic::FloatingPlayerController *floating READ floating CONSTANT)
    Q_PROPERTY(EchoMusic::TaskbarLyricController *taskbar READ taskbar CONSTANT)
    Q_PROPERTY(EchoMusic::TaskbarButtonController *taskbarButton READ taskbarButton CONSTANT)
    Q_PROPERTY(bool maximized READ maximized NOTIFY changed)
    Q_PROPERTY(bool fullscreen READ fullscreen NOTIFY changed)
    Q_PROPERTY(bool presentationReady READ presentationReady NOTIFY presentationReadyChanged)
    Q_PROPERTY(bool rememberSize READ rememberSize WRITE setRememberSize NOTIFY changed)
    Q_PROPERTY(bool closeToTray READ closeToTray WRITE setCloseToTray NOTIFY changed)
    Q_PROPERTY(bool trayAvailable READ trayAvailable NOTIFY changed)
    Q_PROPERTY(bool background READ background NOTIFY changed)
    Q_PROPERTY(bool hideDockInBackground READ hideDockInBackground WRITE setHideDockInBackground
                   NOTIFY changed)
    Q_PROPERTY(bool hideMenuBarInBackground READ hideMenuBarInBackground WRITE
                   setHideMenuBarInBackground NOTIFY changed)
    Q_PROPERTY(bool startMinimized READ startMinimized WRITE setStartMinimized NOTIFY changed)
    Q_PROPERTY(bool autoLaunch READ autoLaunch WRITE setAutoLaunch NOTIFY changed)
    Q_PROPERTY(QString systemError READ systemError NOTIFY changed)
    Q_PROPERTY(bool supportsWindowFrost READ supportsWindowFrost NOTIFY changed)
    Q_PROPERTY(QString appearanceError READ appearanceError NOTIFY changed)
    Q_PROPERTY(bool macOS READ macOS CONSTANT)
    Q_PROPERTY(Qt::WindowFlags mainWindowFlags READ mainWindowFlags CONSTANT)
    Q_PROPERTY(QRectF nativeCaptionBounds READ nativeCaptionBounds NOTIFY changed)
public:
    WindowController(EchoMusic::SettingsStore &settings, bool persist, QObject *parent = nullptr);
    ~WindowController() override;
    AppIconService &appIcons()
    {
        return m_appIcons;
    }
    void attach(QQuickWindow *window);
    void attachPluginWindow(QQuickWindow *window);
    bool mainWindowActive() const;

    void attachPlayback(PlaybackService &playback);
    void restoreWindow();
    void showMiniWindow();
    /// Shows the taskbar lyric context menu at a global position.
    void showTaskbarLyricMenu(qreal globalX, qreal globalY);
    /// Opens the settings page on the taskbar lyric section.
    Q_INVOKABLE void openTaskbarLyricSettings();
    FloatingPlayerController *floating()
    {
        return &m_floating;
    }
    TaskbarLyricController *taskbar()
    {
        return &m_taskbar;
    }
    TaskbarButtonController *taskbarButton()
    {
        return &m_taskbarButton;
    }
    /// Read-only system button bounds in Qt window coordinates; never moves them.
    QRectF nativeCaptionBounds() const;
    bool maximized() const;
    bool fullscreen() const;
    /// Initial native surface and appearance are configured before first show.
    bool presentationReady() const;
    bool rememberSize() const
    {
        return m_remember;
    }
    void setRememberSize(bool remember);
    bool closeToTray() const
    {
        return m_closeToTray;
    }
    bool trayAvailable() const;
    bool background() const
    {
        return m_background;
    }
    bool hideDockInBackground() const
    {
        return m_hideDock;
    }
    bool hideMenuBarInBackground() const
    {
        return m_hideMenu;
    }
    bool startMinimized() const
    {
        return m_startMinimized;
    }
    bool autoLaunch() const
    {
        return m_startup.enabled();
    }
    QString systemError() const
    {
        return m_systemError;
    }
    bool macOS() const;
    Qt::WindowFlags mainWindowFlags() const
    {
        return WindowBackdrop::mainWindowFlags();
    }
    bool supportsWindowFrost() const
    {
        return m_backdrop.supported();
    }
    QString appearanceError() const
    {
        return m_appearanceError;
    }
    void setAppearance(bool frosted, bool dark, int transparency = 0);
    void setCloseToTray(bool enabled);
    void setHideDockInBackground(bool enabled);
    void setHideMenuBarInBackground(bool enabled);
    void setStartMinimized(bool enabled);
    void setAutoLaunch(bool enabled);
    Q_INVOKABLE void showWindow();
    Q_INVOKABLE void toggleVisibility();
    Q_INVOKABLE void quit();
    void applyStartupVisibility();
    /// Explicit caption gesture; on macOS native movement is allowed only for
    /// this call and revoked after the OS tracking loop, including failure.
    Q_INVOKABLE bool startSystemMove();
    Q_INVOKABLE void minimize();
    Q_INVOKABLE void toggleMaximized();
    Q_INVOKABLE void toggleFullscreen();
    // One content overlay may temporarily own fullscreen. Closing/destroying
    // it restores the previous state without revealing a hidden/minimized host.
    Q_INVOKABLE void beginContentFullscreen(QObject *owner);
    Q_INVOKABLE void endContentFullscreen(QObject *owner);
    /// Re-delivers the pointer's current position to the main window.
    ///
    /// Qt only updates hover while the pointer moves, so a surface that becomes
    /// interactive after the pointer stopped moving - the playback page once its
    /// entry transition is done - keeps whatever hover target the pointer had
    /// before, which is none at all: no tooltips and no control highlights until
    /// the pointer moves again.
    Q_INVOKABLE void refreshPointerHover();
    Q_INVOKABLE void close();
    void shutdown();
    /// Pure placement rule, tested independently of a desktop/window manager.
    static QRect restoreBounds(const QRect &saved, const QList<QRect> &areas, const QRect &primary);
signals:
    void changed();
    void presentationReadyChanged();
    void mainWindowActivated();
    void contentFullscreenExitRequested(QObject *owner);
    void quitRequested();
    void settingsRequested(const QString &section);
    void nativeMenuOpening();

private:
    AppIconService m_appIcons;
    friend class NativeMenusTestAccess;
    void activateTray(int reason);
    void captureNormalGeometry();
    void updateNormalGeometry();
    void updateIncubationController();
    void save();
    bool eventFilter(QObject *watched, QEvent *event) override;
    void setBackground(bool background);
    void popupTrayMenu();
    void installTrayMenuModel();
    QMenu *createTrayMenu();
    QMenu *createDockMenu();
    /// Built separately from the popup so the strip's menu is inspectable.
    QMenu *createTaskbarLyricMenu();
    void addPlaybackMenuActions(QMenu *menu, bool submenuModes = true);
    void addLyricsMenuActions(QMenu *menu);
    EchoMusic::SettingsStore &m_settings;
    FloatingPlayerController m_floating;
    TaskbarLyricController m_taskbar;
    TaskbarButtonController m_taskbarButton;
    QPointer<QQuickWindow> m_window;
    QList<QPointer<QQuickWindow>> m_pluginWindows;
    QPointer<QObject> m_contentFullscreenOwner;
    QMetaObject::Connection m_contentFullscreenDestroyed;
    bool m_contentEnteredFullscreen = false;
    // Window mode intent outlives the Mini surface. Closing Mini releases its
    // complete UI; tray activation recreates that last mode as EchoMusic does.
    bool m_restoreMini = false;
    bool m_presentationReady = false;
    bool m_systemMoveActive = false;
    QTimer m_saveTimer;
    QTimer m_geometryTimer;
    QRect m_normal;
    bool m_persist, m_remember = true, m_restoring = false, m_wasMaximized = false;
    bool m_closeToTray = true, m_hideDock = false, m_hideMenu = false, m_startMinimized = false,
         m_background = false, m_shuttingDown = false;
    Qt::WindowState m_backgroundState = Qt::WindowNoState;
    StartupIntegration m_startup;
    WindowBackdrop m_backdrop;
    bool m_frosted = false, m_dark = false;
    int m_transparency = 0;
    QString m_appearanceError;
    QString m_systemError;
    std::unique_ptr<QSystemTrayIcon> m_tray;
    // Linux exports this command model for the panel's GetLayout/cache lifetime.
    // It is separate from a locally tracked, transient QWidget popup.
    std::unique_ptr<QMenu> m_trayMenuModel;
    QPointer<QMenu> m_trayMenu;
    QPointer<PlaybackService> m_playback;
    std::unique_ptr<TrayIconTheme> m_trayTheme;
    std::unique_ptr<QMenuBar> m_applicationMenu;
    std::unique_ptr<QObject> m_dockMenuOwner;
};
} // namespace EchoMusic
