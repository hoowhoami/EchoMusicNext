#include "SettingsKeys.h"
#include "WindowController.h"
#include "WindowPresentationDiagnostics.h"
#include "WindowResizeSync.h"
#include "PlaybackService.h"
#include "PointerHoverRefresh.h"
#include "TrayIconTheme.h"
#include <QMenuBar>
#include <QActionGroup>
#include <QCursor>
#include <QGuiApplication>
#include <QQmlEngine>
#include <QScreen>
#include <QApplication>
#include <QCloseEvent>
#include <QLoggingCategory>
#include <QQuickItem>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QIcon>
#include <functional>
namespace EchoMusic
{
namespace
{
Q_LOGGING_CATEGORY(pointerLog, "echo.shell.pointer")
Qt::WindowState restoreState(Qt::WindowStates states)
{
    if (states.testFlag(Qt::WindowFullScreen))
        return Qt::WindowFullScreen;
    return states.testFlag(Qt::WindowMaximized) ? Qt::WindowMaximized : Qt::WindowNoState;
}
// After a click the window overlay can keep the mouse grab even though no button
// is down. While the overlay holds that grab, Qt Quick stops delivering hover
// events to the scene: every hover effect in the window freezes until the window
// is hidden and shown again. Presses the scene does not claim are routed to the
// overlay, and a release that the grabbed item consumes never reaches it, so the
// grab is left behind.
//
// A visible popup does not make the grab legitimate. The playback page is a child
// of the overlay, and a grab held for it is exactly what leaves the whole page
// without hover after it opens - no tooltips, no control highlights - until the
// user clicks inside it. Only a held button needs the grab: that is the drag a
// popup is tracking. Hover, the popup's own item tracking and closing on an
// outside press all keep working through the normal item tree.
QQuickItem *staleOverlayGrabber(QQuickWindow *window)
{
    if (!window || QGuiApplication::mouseButtons() != Qt::NoButton)
        return nullptr;
    QQuickItem *grabber = window->mouseGrabberItem();
    // QQuickOverlay is a private Qt Quick type; match it by name instead of
    // pulling in private headers for a single runtime check.
    const QLatin1String overlayClass("QQuickOverlay");
    if (!grabber || QLatin1String(grabber->metaObject()->className()) != overlayClass)
        return nullptr;
    return grabber;
}
} // namespace
#ifdef Q_OS_MACOS
void configureMacWindow(QQuickWindow *window);
QRectF macCaptionBounds(QQuickWindow *window);
void setMacDockHidden(bool hidden);
QMenuBar *createMacApplicationMenu(WindowController *controller);
void popupMacTrayMenu(QMenu *menu);
void retireMacMenu(QMenu *menu);
QObject *createMacDockMenuOwner(WindowController *controller, std::function<QMenu *()> create);
#endif
WindowController::WindowController(QSettings &settings, bool persist, QObject *parent)
    : QObject(parent), m_settings(settings), m_floating(settings, persist, this),
      m_taskbar(settings, this), m_taskbarButton(settings, this), m_persist(persist),
      m_startup(persist), m_backdrop(persist)
{
    connect(&m_floating, &FloatingPlayerController::mainRequested, this,
            &WindowController::showWindow);
    connect(&m_taskbar, &TaskbarLyricController::openMainRequested, this,
            &WindowController::showWindow);
    connect(&m_taskbar, &TaskbarLyricController::menuRequested, this,
            &WindowController::showTaskbarLyricMenu);
    connect(&m_floating, &FloatingPlayerController::miniEntered, this, [this] {
        m_restoreMini = true;
        if (!m_window)
            return;
        m_backgroundState = restoreState(m_window->windowStates());
        m_window->hide();
        setBackground(true);
    });
    connect(&m_floating, &FloatingPlayerController::miniClosed, this, [this] {
        if (!trayAvailable())
            showWindow();
        else
            setBackground(true);
    });
    connect(&m_floating, &FloatingPlayerController::changed, this,
            &WindowController::updateIncubationController, Qt::QueuedConnection);
    connect(&m_floating, &FloatingPlayerController::presentationWindowChanged, this,
            &WindowController::updateIncubationController, Qt::QueuedConnection);
    m_remember = settings.value(EchoMusic::SettingsKeys::Window::RememberSize, true).toBool();
    m_closeToTray = settings.value(EchoMusic::SettingsKeys::Window::CloseToTray, true).toBool();
    m_hideDock =
        settings.value(EchoMusic::SettingsKeys::Window::HideDockInBackground, false).toBool();
    m_hideMenu =
        settings.value(EchoMusic::SettingsKeys::Window::HideMenuBarInBackground, false).toBool();
    m_startMinimized =
        settings.value(EchoMusic::SettingsKeys::Window::StartMinimized, false).toBool();
    connect(&m_backdrop, &WindowBackdrop::completed, this, [this](const QString &error) {
        if (m_appearanceError == error)
            return;
        m_appearanceError = error;
        emit changed();
    });
    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(250);
    connect(&m_saveTimer, &QTimer::timeout, this, &WindowController::save);
}
WindowController::~WindowController()
{
    if (m_tray) {
        m_tray->hide();
        m_tray->setContextMenu(nullptr);
    }
    // A tray menu is top-level, not a QWidget child of this QObject owner.
    delete m_trayMenu.data();
}
QRect WindowController::restoreBounds(const QRect &saved, const QList<QRect> &areas,
                                      const QRect &primary)
{
    // Recover single-screen placements within 128 DIP. Preserve intersecting
    // multi-screen placements and reject disconnected ones.
    if (saved.isValid()) {
        if (areas.size() == 1) {
            const auto area = areas.first();
            const auto size =
                QSize(qMin(saved.width(), area.width()), qMin(saved.height(), area.height()));
            auto x = qMax(saved.x(), area.x()), y = qMax(saved.y(), area.y());
            if (x > area.x() + area.width() - 128)
                x = area.x() + area.width() - size.width();
            if (y > area.y() + area.height() - 128)
                y = area.y() + area.height() - size.height();
            return {QPoint(qMax(x, area.x()), qMax(y, area.y())), size};
        }
        for (const auto &area : areas)
            if (saved.intersects(area))
                return saved;
    }
    const QSize size(qMin(1150, primary.width()), qMin(750, primary.height()));
    return {primary.center() - QPoint(size.width() / 2, size.height() / 2), size};
}
bool WindowController::mainWindowActive() const
{
    return m_window && m_window->isVisible() && m_window->isActive() && !m_floating.miniActive();
}
void WindowController::attach(QQuickWindow *window)
{
    Q_ASSERT(!m_window);
    if (!window)
        return;
    m_window = window;
    connect(window, &QWindow::activeChanged, this, [this] {
        if (mainWindowActive())
            emit mainWindowActivated();
    });
    m_backdrop.attach(window);
    m_taskbarButton.attach(window);
    if (qEnvironmentVariableIntValue("ECHO_RENDER_DIAGNOSTICS") == 1)
        new WindowPresentationDiagnostics(window);
    window->installEventFilter(this);
#ifdef Q_OS_WIN
    if (QGuiApplication::platformName() == QStringLiteral("windows"))
        new WindowResizeSync(window);
#endif
    // Test/offscreen controllers never allocate a QWidget menu or touch the OS.
    if (m_persist && qobject_cast<QApplication *>(QCoreApplication::instance()) &&
        QGuiApplication::platformName() != "offscreen") {
        m_tray = std::make_unique<QSystemTrayIcon>();
        m_trayTheme = std::make_unique<TrayIconTheme>([this] {
            if (m_tray)
                m_tray->setIcon(TrayIconTheme::icon());
        });
        m_tray->setIcon(TrayIconTheme::icon());
        m_tray->setToolTip("EchoMusicNext");
        // Never bind a context menu on Cocoa: AppKit then intercepts BOTH mouse
        // buttons, so left activation would also open a menu.
        connect(m_tray.get(), &QSystemTrayIcon::activated, this,
                [this](QSystemTrayIcon::ActivationReason reason) { activateTray(int(reason)); });
#ifdef Q_OS_LINUX
        // SNI panels may fetch/cache GetLayout before any Context activation.
        // Bind before registration so Qt exports /MenuBar, not /NO_DBUSMENU.
        installTrayMenuModel();
#endif
#ifdef Q_OS_MACOS
        if (QGuiApplication::platformName() == "cocoa") {
            m_applicationMenu.reset(createMacApplicationMenu(this));
            m_dockMenuOwner.reset(
                createMacDockMenuOwner(this, [this] { return createDockMenu(); }));
        }
#endif
        m_tray->show();
        // Qt's Cocoa delegate forwards Finder/Dock reopen with forcePropagate,
        // even if already active. Use that public signal, not delegate swizzling.
        connect(qGuiApp, &QGuiApplication::applicationStateChanged, this,
                [this](Qt::ApplicationState state) {
                    if (macOS() && m_background && !m_floating.miniActive() &&
                        state == Qt::ApplicationActive)
                        restoreWindow();
                });
    }
    m_restoring = true;
    auto *primary = QGuiApplication::primaryScreen();
    if (primary) {
        const auto saved =
            m_remember && m_persist
                ? m_settings.value(EchoMusic::SettingsKeys::Window::NormalGeometry).toRect()
                : QRect{};
        if (QGuiApplication::platformName().startsWith("wayland")) {
            window->setMinimumSize({1100, 650});
            window->resize({qMax(1100, saved.isValid() ? saved.width() : 1150),
                            qMax(650, saved.isValid() ? saved.height() : 750)});
        } else {
            QList<QRect> areas;
            for (auto *screen : QGuiApplication::screens())
                areas.append(screen->availableGeometry());
            const auto bounds = restoreBounds(saved, areas, primary->availableGeometry());
            window->setMinimumSize({qMin(1100, bounds.width()), qMin(650, bounds.height())});
            window->setGeometry(bounds);
        }
    }
    m_normal = window->geometry();
    m_restoring = false;
#ifdef Q_OS_MACOS
    if (QGuiApplication::platformName() == "cocoa")
        configureMacWindow(window);
#endif
    connect(window, &QWindow::xChanged, this, &WindowController::captureNormalGeometry);
    connect(window, &QWindow::yChanged, this, &WindowController::captureNormalGeometry);
    connect(window, &QWindow::widthChanged, this, &WindowController::captureNormalGeometry);
    connect(window, &QWindow::heightChanged, this, &WindowController::captureNormalGeometry);
    connect(window, &QWindow::windowStateChanged, this, [this] { emit changed(); });
    // A shortcut can choose Mini while the engine is still creating Main.qml.
    if (m_floating.miniActive()) {
        m_backgroundState = restoreState(window->windowStates());
        window->hide();
        setBackground(true);
    }
    connect(window, &QWindow::visibleChanged, this, &WindowController::updateIncubationController,
            Qt::QueuedConnection);
    updateIncubationController();
    setAppearance(m_frosted, m_dark, m_transparency);
    m_presentationReady = true;
    emit presentationReadyChanged();
    // Native capability is known only after the QML window has been attached.
    emit changed();
}
void WindowController::attachPlayback(PlaybackService &playback)
{
    Q_ASSERT(!m_playback);
    m_playback = &playback;
    m_taskbarButton.attachPlayback(playback);
#ifdef Q_OS_WIN
    // Windows tracks the bound menu as a native menu whose WM_COMMAND dispatch
    // keys off the last tracked popup; rebinding a fresh menu per activation
    // never dispatches (QWindowsSystemTrayIcon::updateMenu is a no-op, so the
    // platform menu only exists through the persistent binding). Install one
    // stable model here, once the playback commands exist - the same shape the
    // SNI path uses on Linux.
    if (m_tray && !m_trayMenuModel)
        installTrayMenuModel();
#endif
}
void WindowController::activateTray(int reason)
{
    if (reason == QSystemTrayIcon::Trigger)
        restoreWindow();
    else if (reason == QSystemTrayIcon::Context)
        popupTrayMenu();
}
void WindowController::restoreWindow()
{
    if (m_shuttingDown)
        return;
    if (m_restoreMini || m_floating.miniActive()) {
        m_floating.showMini();
        return;
    }
    showWindow();
}
void WindowController::showMiniWindow()
{
    if (m_shuttingDown)
        return;
    m_floating.showMini();
}
QMenu *WindowController::createTrayMenu()
{
    auto *menu = new QMenu;
    menu->setObjectName("nativeTrayMenu");
    menu->addAction(tr("打开主窗口"), this, &WindowController::showWindow);
    menu->addAction(tr("打开 Mini 窗口"), this, &WindowController::showMiniWindow);
    menu->addSeparator();
    // The shell shows this menu as a native Windows menu, so the modes cannot
    // ride in a submenu (see addPlaybackMenuActions).
    addPlaybackMenuActions(menu, false);
    addLyricsMenuActions(menu);
    menu->addSeparator();
    menu->addAction(tr("退出EchoMusic"), this, &WindowController::quit);
    return menu;
}

void WindowController::showTaskbarLyricMenu(qreal globalX, qreal globalY)
{
    if (m_shuttingDown)
        return;
    // The shell coordinator must know a native menu is about to open, or the
    // app's own popups keep fighting it for the foreground.
    emit nativeMenuOpening();
    auto *menu = createTaskbarLyricMenu();
    // The strip is a child of the taskbar, so the menu has to be shown at an
    // explicit position and owns its own lifetime.
    menu->setAttribute(Qt::WA_DeleteOnClose);
    menu->popup(QPoint(qRound(globalX), qRound(globalY)));
}
QMenu *WindowController::createTaskbarLyricMenu()
{
    auto *menu = new QMenu;
    menu->setObjectName("taskbarLyricMenu");
    menu->addAction(tr("打开主窗口"), this, &WindowController::showWindow);
    menu->addAction(tr("打开设置页"), this, &WindowController::openTaskbarLyricSettings);
    menu->addSeparator();
    // Disabling the strip is the counterpart of the lyric settings switch; it
    // sits next to 退出 because both take away something the user is looking at.
    menu->addAction(tr("关闭任务栏歌词"), this, [this] { m_taskbar.setEnabled(false); })
        ->setObjectName("disableTaskbarLyric");
    menu->addAction(tr("退出EchoMusic"), this, &WindowController::quit);
    return menu;
}

void WindowController::openTaskbarLyricSettings()
{
    // The settings page lives inside the main window, so the window has to be
    // brought up first: from the taskbar strip the app is often in the tray,
    // and opening the page in a hidden window would look like nothing happened.
    showWindow();
    // The taskbar lyric section lives in the lyric settings page.
    emit settingsRequested(QStringLiteral("lyric"));
}
QMenu *WindowController::createDockMenu()
{
    if (m_shuttingDown)
        return nullptr;
    auto *menu = new QMenu;
    menu->setObjectName("nativeDockMenu");
    addPlaybackMenuActions(menu);
    addLyricsMenuActions(menu);
    return menu;
}
void WindowController::addPlaybackMenuActions(QMenu *menu, bool submenuModes)
{
    if (m_playback) {
        auto *playback = m_playback.data();
        auto *play = menu->addAction(QString(), playback, &PlaybackService::toggle);
        play->setObjectName("togglePlayback");
        menu->addAction(tr("上一首"), playback, &PlaybackService::previous)
            ->setObjectName("previous");
        menu->addAction(tr("下一首"), playback, [playback] { playback->next(); })
            ->setObjectName("next");
        // The tray menu is handed to the shell as a native menu, and Windows'
        // converter hoists a submenu's items to the front of its parent while
        // keeping the submenu. There the modes become a titled group instead -
        // the same shape as the 音量 row above the volume commands.
        QMenu *modes = nullptr;
        if (submenuModes) {
            modes = menu->addMenu(tr("播放模式"));
        } else {
            menu->addSeparator();
            auto *heading = menu->addAction(tr("播放模式"));
            heading->setObjectName("playModeHeading");
            heading->setEnabled(false);
            modes = menu;
        }
        auto *group = new QActionGroup(modes);
        struct ModeAction
        {
            PlaybackService::PlayMode mode;
            QString label;
            QString command;
        };
        const QList<ModeAction> definitions{
            {PlaybackService::Sequential, tr("顺序播放"), "sequential"},
            {PlaybackService::ListLoop, tr("列表循环"), "listLoop"},
            {PlaybackService::Shuffle, tr("随机播放"), "shuffle"},
            {PlaybackService::SingleLoop, tr("单曲循环"), "singleLoop"}};
        for (const auto &[mode, label, command] : definitions) {
            auto *action = modes->addAction(label, playback,
                                            [playback, mode] { playback->setPlayMode(mode); });
            action->setCheckable(true);
            action->setData(int(mode));
            action->setObjectName(command);
            group->addAction(action);
        }
        menu->addSeparator();
        auto *volume = menu->addAction(QString());
        volume->setEnabled(false);
        menu->addAction(tr("增大音量"), playback,
                        [playback] { playback->setVolume(playback->volume() + 5); })
            ->setObjectName("volumeUp");
        menu->addAction(tr("减小音量"), playback,
                        [playback] { playback->setVolume(playback->volume() - 5); })
            ->setObjectName("volumeDown");
        menu->addAction(tr("静音 / 取消静音"), playback, &PlaybackService::toggleMute)
            ->setObjectName("toggleMute");
        const auto update = [playback, play, volume, group] {
            play->setText(playback->playing() ? tr("暂停") : tr("播放"));
            volume->setText(tr("音量 %1%").arg(qRound(playback->volume())));
            for (auto *action : group->actions())
                action->setChecked(action->data().toInt() == int(playback->playMode()));
        };
        connect(playback, &PlaybackService::changed, menu, update);
        update();
        menu->addSeparator();
    }
}
void WindowController::addLyricsMenuActions(QMenu *menu)
{
    auto *lock = menu->addAction(QString(), &m_floating, [this] {
        if (m_floating.desktopEnabled())
            m_floating.setLocked(!m_floating.locked());
    });
    lock->setObjectName("toggleLyricsLock");
    const auto updateLock = [this, lock] {
        lock->setText(m_floating.locked() ? tr("解锁桌面歌词") : tr("锁定桌面歌词"));
        lock->setEnabled(m_floating.desktopEnabled());
    };
    connect(&m_floating, &FloatingPlayerController::changed, menu, updateLock);
    updateLock();
}
void WindowController::popupTrayMenu()
{
    // SNI hosts own placement, focus and dismissal of their remote menu, XEmbed
    // and Windows both use Qt's bound context menu (Windows tracks it as a
    // native menu, see attachPlayback). Never open a second popup here.
    if (m_shuttingDown || m_trayMenuModel || m_trayMenu)
        return;
    emit nativeMenuOpening();
    auto *menu = createTrayMenu();
    m_trayMenu = menu;
#ifdef Q_OS_MACOS
    if (QGuiApplication::platformName() == "cocoa") {
        popupMacTrayMenu(menu);
        retireMacMenu(menu);
        return;
    }
#endif
    connect(menu, &QMenu::aboutToHide, menu, &QObject::deleteLater);
    menu->popup(QCursor::pos());
}
void WindowController::installTrayMenuModel()
{
    Q_ASSERT(m_tray && !m_trayMenuModel);
    // A complete, stable command model is needed even while the panel is closed:
    // GetLayout need not be preceded by AboutToShow, and hosts cache action IDs.
    // QMenu is only the Qt exporter recipe; no local QWidget popup is shown for
    // SNI. Windows needs the same stability: its native menu dispatches
    // WM_COMMAND through the popup tracked from this binding (see attachPlayback).
    m_trayMenuModel.reset(createTrayMenu());
    connect(m_trayMenuModel.get(), &QMenu::aboutToShow, this, &WindowController::nativeMenuOpening);
    m_tray->setContextMenu(m_trayMenuModel.get());
}
void WindowController::attachPluginWindow(QQuickWindow *window)
{
    if (!window)
        return;
    m_pluginWindows.removeIf([](const auto &item) { return !item; });
    if (m_pluginWindows.contains(window))
        return;
    m_pluginWindows.append(window);
    connect(window, &QWindow::visibleChanged, this, &WindowController::updateIncubationController,
            Qt::QueuedConnection);
    connect(window, &QWindow::activeChanged, this, &WindowController::updateIncubationController,
            Qt::QueuedConnection);
    connect(window, &QObject::destroyed, this, &WindowController::updateIncubationController,
            Qt::QueuedConnection);
}
void WindowController::updateIncubationController()
{
    if (m_shuttingDown)
        return;
    // One engine can have several windows, while its incubation controller is
    // owned/budgeted by one QQuickWindow. A hidden Main must not starve Mini.
    auto *preferred = m_floating.presentationWindow();
    auto *window = m_floating.miniActive() && preferred ? preferred
                   : m_window && m_window->isVisible()  ? m_window.data()
                   : preferred                          ? preferred
                                                        : m_window.data();
    // A visible plugin window must keep asynchronous QML creation progressing
    // while Main and Mini are hidden. The engine still has one controller owner.
    m_pluginWindows.removeIf([](const auto &item) { return !item; });
    for (auto plugin : m_pluginWindows)
        if (plugin && plugin->isVisible() &&
            (plugin->isActive() || !window || !window->isVisible())) {
            window = plugin;
            if (plugin->isActive())
                break;
        }
    if (window) {
        auto *engine = m_window ? qmlEngine(m_window) : qmlEngine(window);
        if (engine)
            engine->setIncubationController(window->incubationController());
    }
}
QRectF WindowController::nativeCaptionBounds() const
{
#ifdef Q_OS_MACOS
    if (QGuiApplication::platformName() == "cocoa")
        return macCaptionBounds(m_window);
#endif
    return {};
}
bool WindowController::maximized() const
{
    return m_window && m_window->windowStates().testFlag(Qt::WindowMaximized);
}
bool WindowController::fullscreen() const
{
    return m_window && m_window->windowStates().testFlag(Qt::WindowFullScreen);
}
bool WindowController::presentationReady() const
{
    return m_window && m_presentationReady;
}
void WindowController::setAppearance(bool frosted, bool dark, int transparency)
{
    m_frosted = frosted;
    m_dark = dark;
    m_transparency = qBound(0, transparency, 100);
    m_taskbarButton.setDark(dark);
    if (!m_window)
        return;
    const auto error = m_backdrop.apply(frosted, dark, m_transparency);
    if (m_appearanceError != error) {
        m_appearanceError = error;
        emit changed();
    }
}
void WindowController::captureNormalGeometry()
{
    if (m_frosted || m_transparency > 0)
        setAppearance(m_frosted, m_dark, m_transparency);
    if (!m_window || m_restoring || m_window->windowState() != Qt::WindowNoState)
        return;
    m_normal = m_window->geometry();
    if (m_persist && m_remember)
        m_saveTimer.start();
}
void WindowController::save()
{
    if (m_persist && m_remember && m_normal.isValid())
        m_settings.setValue(EchoMusic::SettingsKeys::Window::NormalGeometry, m_normal);
}
void WindowController::setRememberSize(bool remember)
{
    if (m_remember == remember)
        return;
    m_remember = remember;
    m_settings.setValue(EchoMusic::SettingsKeys::Window::RememberSize, remember);
    emit changed();
}
void WindowController::minimize()
{
    if (m_window)
        // Keep the pre-minimize state, including content fullscreen, so its
        // retirement and the later taskbar/tray restore retain the host state.
        m_window->setWindowStates(m_window->windowStates() | Qt::WindowMinimized);
}
void WindowController::toggleMaximized()
{
    if (!m_window)
        return;
    if (maximized() || fullscreen())
        m_window->showNormal();
    else
        m_window->showMaximized();
}
void WindowController::refreshPointerHover()
{
    EchoMusic::refreshPointerHover(m_window);
}
void WindowController::toggleFullscreen()
{
    if (!m_window)
        return;
    // F11/window actions retire the content layer first. In particular, an MV
    // nested in existing application fullscreen must leave that host state intact.
    if (m_contentFullscreenOwner) {
        emit contentFullscreenExitRequested(m_contentFullscreenOwner);
        return;
    }
    if (fullscreen()) {
        if (m_wasMaximized)
            m_window->showMaximized();
        else
            m_window->showNormal();
    } else {
        m_wasMaximized = maximized();
        m_window->showFullScreen();
    }
}
void WindowController::beginContentFullscreen(QObject *owner)
{
    if (!owner || !m_window || m_contentFullscreenOwner == owner)
        return;
    if (m_contentFullscreenOwner)
        endContentFullscreen(m_contentFullscreenOwner);
    m_contentFullscreenOwner = owner;
    m_contentEnteredFullscreen = !fullscreen();
    m_contentFullscreenDestroyed =
        connect(owner, &QObject::destroyed, this, [this] { endContentFullscreen(nullptr); });
    if (m_contentEnteredFullscreen) {
        m_wasMaximized = maximized();
        m_window->showFullScreen();
    }
}
void WindowController::endContentFullscreen(QObject *owner)
{
    if (owner && m_contentFullscreenOwner != owner)
        return;
    disconnect(m_contentFullscreenDestroyed);
    m_contentFullscreenOwner = nullptr;
    const bool restore = m_contentEnteredFullscreen;
    m_contentEnteredFullscreen = false;
    if (!restore || !m_window)
        return;
    // Native QPA owns restore geometry and transitions already completed by the
    // window manager. Never reveal a hidden/minimized host while retiring content.
    Qt::WindowStates state = m_wasMaximized ? Qt::WindowMaximized : Qt::WindowNoState;
    if (m_window->windowStates().testFlag(Qt::WindowMinimized))
        state |= Qt::WindowMinimized;
    if (m_backgroundState == Qt::WindowFullScreen)
        m_backgroundState = restoreState(state);
    if (!fullscreen())
        return;
    m_window->setWindowStates(state);
    // Combined state bits can change while the effective state stays Minimized,
    // in which case QWindow::windowStateChanged does not notify our projections.
    emit changed();
}
void WindowController::close()
{
    if (m_window)
        m_window->close();
}
bool WindowController::macOS() const
{
#ifdef Q_OS_MACOS
    return true;
#else
    return false;
#endif
}
bool WindowController::trayAvailable() const
{
    return m_tray && QSystemTrayIcon::isSystemTrayAvailable();
}
void WindowController::setCloseToTray(bool value)
{
    if (m_closeToTray == value)
        return;
    m_closeToTray = value;
    m_settings.setValue(EchoMusic::SettingsKeys::Window::CloseToTray, value);
    if (!value && m_background)
        showWindow();
    emit changed();
}
void WindowController::setHideDockInBackground(bool value)
{
    if (m_hideDock == value)
        return;
    m_hideDock = value;
    m_settings.setValue(EchoMusic::SettingsKeys::Window::HideDockInBackground, value);
    if (m_background)
        setBackground(true);
    emit changed();
}
void WindowController::setHideMenuBarInBackground(bool value)
{
    if (m_hideMenu == value)
        return;
    m_hideMenu = value;
    m_settings.setValue(EchoMusic::SettingsKeys::Window::HideMenuBarInBackground, value);
    if (m_background)
        setBackground(true);
    emit changed();
}
void WindowController::setStartMinimized(bool value)
{
    if (m_startMinimized == value)
        return;
    m_startMinimized = value;
    m_settings.setValue(EchoMusic::SettingsKeys::Window::StartMinimized, value);
    emit changed();
}
void WindowController::setAutoLaunch(bool value)
{
    m_systemError = m_startup.setEnabled(value);
    emit changed();
}
void WindowController::setBackground(bool value)
{
    m_background = value;
    if (m_tray)
        m_tray->setVisible(!(value && !m_floating.miniActive() && macOS() && m_hideMenu));
#ifdef Q_OS_MACOS
    if (m_persist && QGuiApplication::platformName() == "cocoa")
        setMacDockHidden(value && !m_floating.miniActive() && m_hideDock);
#endif
    emit changed();
}
void WindowController::showWindow()
{
    if (!m_window || m_shuttingDown)
        return;
    m_restoreMini = false;
    if (m_floating.miniActive())
        m_floating.leaveMini();
    if (!m_background)
        m_backgroundState = restoreState(m_window->windowStates());
    setBackground(false);
    if (m_backgroundState == Qt::WindowMaximized)
        m_window->showMaximized();
    else if (m_backgroundState == Qt::WindowFullScreen)
        m_window->showFullScreen();
    else
        m_window->showNormal();
    m_window->raise();
    m_window->requestActivate();
}
void WindowController::quit()
{
    emit quitRequested();
}
void WindowController::toggleVisibility()
{
    if (!m_window || m_shuttingDown)
        return;
    if (m_background || !m_window->isVisible() || m_window->visibility() == QWindow::Minimized) {
        showWindow();
        return;
    }
    m_backgroundState = restoreState(m_window->windowStates());
    m_window->hide();
    setBackground(true);
}
void WindowController::applyStartupVisibility()
{
    if (!m_startMinimized || !m_window)
        return;
    if (!trayAvailable()) {
        m_systemError = tr("当前桌面没有可用托盘，保留主窗口");
        emit changed();
        return;
    }
    m_backgroundState = restoreState(m_window->windowStates());
    m_window->hide();
    setBackground(true);
}
bool WindowController::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_window && event->type() == QEvent::MouseMove) {
        if (auto *grabber = staleOverlayGrabber(m_window)) {
            grabber->ungrabMouse();
            qCDebug(pointerLog) << "released stale overlay mouse grab";
        }
    }
    if (watched == m_window && event->type() == QEvent::Close && !m_shuttingDown && m_closeToTray &&
        trayAvailable()) {
        static_cast<QCloseEvent *>(event)->ignore();
        m_backgroundState = restoreState(m_window->windowStates());
        m_window->hide();
        setBackground(true);
        return true;
    }
    return QObject::eventFilter(watched, event);
}
void WindowController::shutdown()
{
    m_shuttingDown = true;
    if (m_trayMenu) {
        m_trayMenu->close();
        m_trayMenu->deleteLater();
    }
    m_floating.shutdown();
    m_saveTimer.stop();
    save();
    if (m_tray) {
        m_tray->hide();
        m_tray->setContextMenu(nullptr);
    }
    m_trayMenuModel.reset();
    m_backdrop.attach(nullptr);
    if (m_window)
        m_window->close();
}
} // namespace EchoMusic
