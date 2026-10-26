pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Controls
import EchoMusic.Theme
import EchoMusic.Shell
import EchoMusic.Settings
import EchoMusic.Search
import EchoMusic.Account
import EchoMusic.Library
import EchoMusic.Plugins
import EchoMusic.Playback
import EchoMusic.Details

ApplicationWindow {
    id: mainWindow
    property StartupController startup: null
    // The taskbar lyric strip window (created without a parent on purpose).
    property var taskbarLyricWindow: null
    // Create or destroy the strip so it matches the enabled preference.
    function syncTaskbarLyricWindow(): void {
        const controller = windowService.taskbar;
        if (controller.enabled) {
            if (!taskbarLyricWindow)
                taskbarLyricWindow = taskbarLyricComponent.createObject(null);
        } else if (taskbarLyricWindow) {
            taskbarLyricWindow.destroy();
            taskbarLyricWindow = null;
        }
    }
    Component.onCompleted: {
        syncTaskbarLyricWindow();
        syncBackupConfirmation();
    }
    function syncBackupConfirmation(): void {
        if (settingsDataService && settingsDataService.confirmation.summary)
            backupConfirmationOwner.open();
        else
            backupConfirmationOwner.close();
    }
    readonly property bool startupPending: startup !== null && !startup.entered
    readonly property MainShell mainShell: shellLoader.item as MainShell
    property PluginManager pluginManager: null
    required property RouteController router
    required property ShellSettings shellSettings
    required property WindowController windowService
    required property SearchModel searchService
    required property ShellController shellController
    required property AccountController accountController
    required property LibraryController libraryController
    property PlaybackDiscussion playbackDiscussion: null
    property PlaybackService playbackService: null
    readonly property real uiScale: shellSettings.uiScale
    property NetworkPreferences networkPreferences: null
    property ShortcutService shortcutService: null
    Binding {
        target: mainWindow.windowService.floating
        property: "miniScale"
        value: mainWindow.shellSettings.uiScale
    }
    property AboutService aboutService: null
    property ExperimentalSettingsService experimentalSettingsService: null
    property SettingsDataService settingsDataService: null
    property RelaunchService relaunchService: null
    property LogService logService: null
    EchoPopupLoader {
        id: backupConfirmationOwner
        hostItem: mainWindow.contentItem
        sourceComponent: Component {
            SettingsBackupConfirmation {
                service: mainWindow.settingsDataService
                uiScale: mainWindow.shellSettings.uiScale
            }
        }
    }
    Connections {
        target: mainWindow.settingsDataService
        function onConfirmationChanged(): void {
            mainWindow.syncBackupConfirmation();
        }
    }
    Connections {
        target: mainWindow.shortcutService
        function onTriggered(command: int): void {
            if (command === ShortcutService.ToggleMainLyric && mainWindow.mainShell)
                mainWindow.mainShell.togglePlayback();
        }
    }
    Loader {
        active: !mainWindow.startupPending && mainWindow.playbackService !== null && mainWindow.windowService.floating.desktopEnabled
        sourceComponent: DesktopLyricWindow {
            service: mainWindow.windowService.floating
            playback: mainWindow.playbackService
        }
    }
    Loader {
        active: !mainWindow.startupPending && mainWindow.playbackService !== null && mainWindow.windowService.floating.unlockAvailable
        sourceComponent: DesktopLyricUnlockWindow {
            service: mainWindow.windowService.floating
        }
    }
    Loader {
        active: !mainWindow.startupPending && mainWindow.playbackService !== null && mainWindow.windowService.floating.miniActive
        sourceComponent: MiniPlayerWindow {
            pluginSurfaces: mainWindow.shellController.pluginSurfaces
            service: mainWindow.windowService.floating
            playback: mainWindow.playbackService
        }
    }
    // The taskbar strip must be a real top-level window: a QML Window created
    // inside the main window's object tree becomes its native child, and the
    // toolkit then re-parents it over the taskbar mount. Creating it with a
    // null parent keeps that from ever happening.
    Component {
        id: taskbarLyricComponent
        TaskbarLyricWindow {
            controller: mainWindow.windowService.taskbar
            playback: mainWindow.playbackService
        }
    }
    Connections {
        target: mainWindow.windowService.taskbar
        function onEnabledChanged(): void {
            mainWindow.syncTaskbarLyricWindow();
        }
    }
    // The cover's click is forwarded to WindowController in C++ (it owns the
    // window operations); a second QML handler here called showWindow twice.
    Connections {
        target: mainWindow.windowService
        function onSettingsRequested(section: string): void {
            if (mainWindow.mainShell)
                mainWindow.mainShell.openSettingsSection(section);
        }
        function onNativeMenuOpening(): void {
            EchoPopupCoordinator.nativeMenuOpening();
        }
    }
    Connections {
        target: mainWindow.windowService.floating
        function onSettingsRequested(): void {
            if (mainWindow.mainShell)
                mainWindow.mainShell.openLyricSettings();
        }
        function onLyricSelectionRequested(): void {
            if (mainWindow.playbackService)
                floatingLyricSource.open();
        }
    }
    EchoPopupLoader {
        id: floatingLyricSource
        objectName: "lyricSourceDialogOwner"
        hostItem: mainWindow.contentItem
        sourceComponent: LyricSourceDialog {
            playback: mainWindow.playbackService
            uiScale: mainWindow.shellSettings.uiScale
        }
    }
    // A translucent lyric page replaces the shell, rather than revealing it.
    // Frost hides it from the first entering frame; ordinary transparency waits
    // for the cover flight. Restore both content and artwork before exit.
    readonly property bool playbackCoversShell: mainShell !== null && mainShell.playbackCoversShell
    width: 1150
    height: 750
    minimumWidth: 1100
    minimumHeight: 650
    visible: true
    title: "EchoMusicNext"
    flags: windowService.mainWindowFlags
    topPadding: 0
    bottomPadding: 0
    leftPadding: 0
    rightPadding: 0
    color: "transparent"
    font: Theme.bodyFont

    Shortcut {
        sequence: "F11"
        autoRepeat: false
        // Window context stops matching while a modal popup such as the playback
        // page owns focus. Keep it application-scoped but gate on the main
        // window so Mini and desktop lyrics do not toggle the shell.
        context: Qt.ApplicationShortcut
        enabled: mainWindow.active && !mainWindow.windowService.macOS
        onActivated: mainWindow.windowService.toggleFullscreen()
    }
    Shortcut {
        sequences: [StandardKey.Find]
        enabled: !mainWindow.startupPending
        onActivated: if (mainWindow.mainShell)
            mainWindow.mainShell.focusSearch()
    }
    Shortcut {
        sequence: "Alt+Left"
        enabled: !mainWindow.startupPending
        onActivated: mainWindow.router.back()
    }
    Shortcut {
        sequence: "Alt+Right"
        enabled: !mainWindow.startupPending
        onActivated: mainWindow.router.forward()
    }
    Shortcut {
        sequences: [StandardKey.Refresh]
        enabled: !mainWindow.startupPending
        onActivated: mainWindow.router.refresh()
    }
    Shortcut {
        sequences: [StandardKey.ZoomIn, "Ctrl+="]
        // Application context reaches modal playback/settings popups; the
        // active-window gate keeps Mini and desktop lyrics out of main UI zoom.
        context: Qt.ApplicationShortcut
        enabled: mainWindow.active
        onActivated: mainWindow.shellSettings.zoomIn()
    }
    Shortcut {
        sequences: [StandardKey.ZoomOut]
        context: Qt.ApplicationShortcut
        enabled: mainWindow.active
        onActivated: mainWindow.shellSettings.zoomOut()
    }
    Shortcut {
        // Qt maps Ctrl to Command on macOS; Meta would mean its Control key.
        sequence: "Ctrl+0"
        context: Qt.ApplicationShortcut
        enabled: mainWindow.active
        onActivated: mainWindow.shellSettings.resetZoom()
    }
    Loader {
        id: startupLoader
        objectName: "startupPageLoader"
        width: mainWindow.width / mainWindow.uiScale
        height: mainWindow.height / mainWindow.uiScale
        scale: mainWindow.uiScale
        transformOrigin: Item.TopLeft
        active: mainWindow.startupPending
        sourceComponent: StartupPage {
            startup: mainWindow.startup
            settings: mainWindow.shellSettings
            windowService: mainWindow.windowService
            window: mainWindow
        }
    }
    component MainShell: EchoBackdropRoot {
        id: frame
        objectName: "shellFrame"
        readonly property bool playbackCoversShell: playbackPageLoader.item !== null && (playbackPageLoader.item as PlaybackPage).visible && ((playbackPageLoader.item as PlaybackPage).opened || Theme.store.windowFrosted && (playbackPageLoader.item as PlaybackPage).enteringPage)
        // Qt blocks input for the modal playback page only once the popup has
        // finished entering, and a press during that window would otherwise be
        // claimed by the shell surface under the pointer - the cover, or the
        // title strip whose press starts a native move loop. Hand the shell no
        // interactive surface at all while the page is up; it is fully covered
        // anyway, and the page restores this the moment it hides.
        readonly property bool playbackPagePresent: playbackPageLoader.item !== null && (playbackPageLoader.item as PlaybackPage).visible
        enabled: !playbackPagePresent
        function focusSearch() {
            titleBar.focusSearch();
        }
        function openLyricSettings() {
            preferencesOwner.open({
                initialSection: "lyric"
            });
        }
        function openSettingsSection(section: string) {
            preferencesOwner.open({
                initialSection: section
            });
        }
        function togglePlayback() {
            if (playbackPageLoader.visible)
                playbackPageLoader.close();
            else
                playbackPageLoader.open();
        }
        width: mainWindow.width / mainWindow.shellSettings.uiScale
        height: mainWindow.height / mainWindow.shellSettings.uiScale
        scale: mainWindow.shellSettings.uiScale
        transformOrigin: Item.TopLeft
        readonly property bool narrow: width < 700
        onWidthChanged: mainWindow.shellSettings.updateViewport(width)
        Component.onCompleted: mainWindow.shellSettings.updateViewport(width)
        ThemeBackground {
            id: windowArtwork
            EchoUiPart {
                id: backgroundPart
                target: windowArtwork
                role: "background.window"
                key: "main"
                surface: "main"
                properties: ["background"]
                baseValues: ({
                        background: Theme.shell
                    })
            }
            baseColor: backgroundPart.value("background", Theme.shell)
            objectName: "themeWindowBackground"
            anchors.fill: parent
            visible: !mainWindow.playbackCoversShell
            // Artwork, panel material and atmosphere fade together once.
            // Business content retains its own opacity.
            opacity: 1 - Theme.store.transparency / 100
            surfaceEffects: Component {
                Item {
                    Rectangle {
                        objectName: "themeMainPanelMaterial"
                        x: workspace.x + mainPanel.x
                        y: workspace.y + mainPanel.y
                        width: mainPanel.width
                        height: mainPanel.height
                        color: Theme.panelColor
                        radius: Theme.shellRadius
                    }
                    Rectangle {
                        objectName: "themePlayerPanelMaterial"
                        x: workspace.x + playerPanel.x
                        y: workspace.y + playerPanel.y
                        width: playerPanel.width
                        height: playerPanel.height
                        color: Theme.panelColor
                        radius: Theme.shellRadius
                    }
                }
            }
        }
        RowLayout {
            id: shellContent
            objectName: "shellContent"
            visible: !mainWindow.playbackCoversShell
            // Keep route/service state alive while its covered scenegraph is culled.
            // A painted login cover blocks clicks but not hover delivery.
            // Disable the covered shell, including already-open tooltips.
            enabled: !mainWindow.accountController.loginOpen && !mainWindow.accountController.verification.open
            anchors.fill: parent
            spacing: 0
            Rectangle {
                Layout.preferredWidth: mainWindow.shellSettings.sidebarCollapsed ? 80 : 230
                Layout.fillHeight: true
                color: "transparent"
                EchoWindowDragArea {
                    width: parent.width
                    height: Math.max(46, 35 / mainWindow.shellSettings.uiScale)
                    onMoveRequested: mainWindow.startSystemMove()
                    onDoubleClickRequested: mainWindow.windowService.toggleMaximized()
                }
                Behavior on Layout.preferredWidth {
                    enabled: mainWindow.shellSettings.motionEnabled
                    NumberAnimation {
                        duration: Theme.panelDuration
                        easing.type: Easing.OutCubic
                    }
                }
                Sidebar {
                    anchors.fill: parent
                    router: mainWindow.router
                    settings: mainWindow.shellSettings
                    uiScale: mainWindow.shellSettings.uiScale
                    accountController: mainWindow.accountController
                    libraryController: mainWindow.libraryController
                }
            }
            ColumnLayout {
                id: workspace
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.topMargin: frame.narrow ? 4 : 8
                Layout.bottomMargin: frame.narrow ? 4 : 8
                Layout.rightMargin: frame.narrow ? 4 : 8
                spacing: frame.narrow ? 6 : 10
                Rectangle {
                    id: mainPanel
                    objectName: "mainPanelSurface"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    color: "transparent"
                    radius: Theme.shellRadius
                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 0
                        TitleBar {
                            id: titleBar
                            objectName: "shellTitleBar"
                            Layout.fillWidth: true
                            Layout.preferredHeight: implicitHeight
                            router: mainWindow.router
                            windowService: mainWindow.windowService
                            window: mainWindow
                            uiScale: mainWindow.shellSettings.uiScale
                            controller: mainWindow.shellController
                            settings: mainWindow.shellSettings
                            searchControl: Component {
                                SearchBox {
                                    id: titlebarSearch
                                    service: mainWindow.searchService
                                    router: mainWindow.router
                                    titlebarSurface: titleBar
                                    window: mainWindow
                                    uiScale: mainWindow.shellSettings.uiScale
                                    Connections {
                                        target: titleBar
                                        function onFocusSearchRequested() {
                                            titlebarSearch.focusSearch();
                                        }
                                    }
                                }
                            }
                        }
                        RouteHost {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            router: mainWindow.router
                            settings: mainWindow.shellSettings
                        }
                    }
                }
                // Material belongs to the surface, never to business content opacity.
                Rectangle {
                    id: playerPanel
                    objectName: "playerPanelSurface"
                    Layout.fillWidth: true
                    Layout.preferredHeight: 88
                    color: "transparent"
                    radius: Theme.shellRadius
                    PlayerBar {
                        id: playerBar
                        coverItem.opacity: playbackPageLoader.item && (playbackPageLoader.item as PlaybackPage).coverMoving ? 0 : 1
                        // Frost hides the shell before the cover flight takes its
                        // snapshot. Keep only this small static source until the
                        // transition ends, then release it with the hidden bar.
                        coverItem.resourceActive: playerBar.visible || playbackPageLoader.item !== null && (playbackPageLoader.item as PlaybackPage).visible && !(playbackPageLoader.item as PlaybackPage).opened
                        manageCapacity: !playbackPageLoader.item || !(playbackPageLoader.item as PlaybackPage).visible
                        onPlaybackPageRequested: {
                            playbackPageLoader.open();
                        }
                        anchors.fill: parent
                        controller: mainWindow.shellController
                        router: mainWindow.router
                        uiScale: mainWindow.shellSettings.uiScale
                    }
                }
            }
        }
        EchoPopupLoader {
            id: preferencesOwner
            objectName: "settingsDialogOwner"
            hostItem: mainWindow.contentItem
            sourceComponent: Component {
                SettingsDialog {
                    id: preferences
                    pluginManager: mainWindow.pluginManager
                    router: mainWindow.router
                    playback: mainWindow.playbackService
                    network: mainWindow.networkPreferences
                    shortcuts: mainWindow.shortcutService
                    about: mainWindow.aboutService
                    experimental: mainWindow.experimentalSettingsService
                    dataService: mainWindow.settingsDataService
                    logging: mainWindow.logService
                    relaunch: mainWindow.relaunchService
                    settings: mainWindow.shellSettings
                    windowService: mainWindow.windowService
                }
            }
        }
        EchoPopupLoader {
            hostItem: mainWindow.contentItem
            requestedVisible: mainWindow.aboutService !== null && mainWindow.aboutService.resultOpen
            sourceComponent: SettingsUpdateDialog {
                about: mainWindow.aboutService
                uiScale: mainWindow.shellSettings.uiScale
            }
        }
        EchoPopupLoader {
            id: taskCenterOwner
            objectName: "taskCenterDialogOwner"
            hostItem: mainWindow.contentItem
            sourceComponent: Component {
                TaskCenterDialog {
                    id: taskCenter
                    service: mainWindow.shellController.tasks
                    uiScale: mainWindow.shellSettings.uiScale
                }
            }
        }
        Loader {
            anchors.fill: parent
            z: 100
            active: mainWindow.accountController.loginOpen
            enabled: !mainWindow.accountController.verification.open
            sourceComponent: LoginOverlay {
                service: mainWindow.accountController
            }
        }
        Loader {
            anchors.fill: parent
            z: 200
            active: mainWindow.accountController.verification.open
            sourceComponent: VerificationOverlay {
                service: mainWindow.accountController.verification
                uiScale: mainWindow.shellSettings.uiScale
            }
        }
        WindowCaptionControls {
            anchors.right: parent.right
            anchors.rightMargin: 16
            anchors.top: parent.top
            anchors.topMargin: 7
            z: 102
            visible: mainWindow.accountController.loginOpen && !mainWindow.windowService.macOS && !mainWindow.windowService.fullscreen
            service: mainWindow.windowService
        }
        EchoWindowDragArea {
            z: 101
            visible: mainWindow.accountController.loginOpen
            width: parent.width
            height: 46
            onMoveRequested: mainWindow.startSystemMove()
            onDoubleClickRequested: mainWindow.windowService.toggleMaximized()
        }
        EchoPopupLoader {
            id: playbackPageLoader
            objectName: "playbackPageOwner"
            hostItem: mainWindow.contentItem
            keepOnMinimize: true
            sourceComponent: PlaybackPage {
                id: nativePlaybackPage
                controlsTopReserve: (nativePlaybackPage.controlsItem as PlayerBar)?.aboveControlsHeight ?? 0
                onGlobalSettingsRequested: {
                    preferencesOwner.open({
                        initialSection: "lyric"
                    });
                }
                onTitleDragRequested: mainWindow.startSystemMove()
                onTitleDoubleClickRequested: mainWindow.windowService.toggleMaximized()
                // The page accepts pointer input once its entry transition is
                // done. The pointer usually stopped moving before that, and Qt
                // only updates hover while it moves: re-deliver the position so
                // the control under the pointer gets its hover right away
                // instead of after the next twitch of the mouse.
                onAcceptingInputChanged: if (acceptingInput)
                    mainWindow.windowService.refreshPointerHover()
                nativeWindowControlsBounds: mainWindow.windowService.nativeCaptionBounds
                windowActions: Component {
                    WindowActions {
                        objectName: "playbackWindowActions"
                        showMiniPlayer: false
                        service: mainWindow.windowService
                        settings: mainWindow.shellSettings
                        colors: playbackPageLoader.item ? (playbackPageLoader.item as PlaybackPage).colors : Theme.colors
                    }
                }
                discussionComponent: mainWindow.playbackDiscussion ? discussionRecipe : null
                Component {
                    id: discussionRecipe
                    PlaybackDiscussionHost {
                        service: mainWindow.playbackDiscussion
                        settings: mainWindow.shellSettings
                        playback: mainWindow.playbackService
                        resourceActive: playbackPageLoader.item !== null && (playbackPageLoader.item as PlaybackPage).discussionActive
                        uiScale: mainWindow.uiScale
                        actionAnchor: playerBar.lastActionAnchor
                    }
                }
                controlsComponent: Component {
                    PlayerBar {
                        controller: mainWindow.shellController
                        router: mainWindow.router
                        uiScale: mainWindow.shellSettings.uiScale
                        lyricStyle: true
                        colors: playbackPageLoader.item ? (playbackPageLoader.item as PlaybackPage).colors : Theme.colors
                        onLastActionAnchorChanged: playerBar.lastActionAnchor = lastActionAnchor
                    }
                }
                sourceCover: playerBar.coverItem
                playback: mainWindow.playbackService
                uiScale: mainWindow.shellSettings.uiScale
            }
        }
        EchoPopupLoader {
            id: playbackQueueLoader
            objectName: "playbackQueueDrawerOwner"
            hostItem: mainWindow.contentItem
            sourceComponent: PlaybackQueueDrawer {
                playback: mainWindow.playbackService
                uiScale: mainWindow.shellSettings.uiScale
                onAddToPlaylistRequested: if (playback.libraryActions)
                    playback.libraryActions.prepareViewedQueue()
            }
        }
        EchoPopupLoader {
            id: addToPlaylistLoader
            objectName: "addToPlaylistDialogOwner"
            hostItem: mainWindow.contentItem
            sourceComponent: AddToPlaylistDialog {
                actions: mainWindow.playbackService.libraryActions
                uiScale: mainWindow.shellSettings.uiScale
            }
        }
        Connections {
            target: mainWindow.router
            function onDismissOverlays(): void {
                addToPlaylistLoader.close();
            }
        }
        Connections {
            target: mainWindow.playbackService ? mainWindow.playbackService.libraryActions : null
            function onDialogRequested() {
                addToPlaylistLoader.open();
            }
            function onCompleted() {
                addToPlaylistLoader.close();
            }
            function onLoginRequested() {
                mainWindow.windowService.showWindow();
                mainWindow.accountController.openLogin();
            }
        }
        EchoPopupLoader {
            id: playbackOptionsLoader
            objectName: "playbackOptionsPopupOwner"
            hostItem: mainWindow.contentItem
            sourceComponent: PlaybackOptionsPopup {
                playback: mainWindow.playbackService
                anchorItem: playerBar.lastActionAnchor
                uiScale: mainWindow.shellSettings.uiScale
            }
        }
        EchoPopupLoader {
            id: effectLoader
            objectName: "playbackEffectPopupOwner"
            hostItem: mainWindow.contentItem
            sourceComponent: PlaybackEffectPopup {
                playback: mainWindow.playbackService
                anchorItem: playerBar.lastActionAnchor
                uiScale: mainWindow.shellSettings.uiScale
            }
        }
        EchoPopupLoader {
            id: castLoader
            objectName: "playbackCastPopupOwner"
            hostItem: mainWindow.contentItem
            sourceComponent: PlaybackCastPopup {
                playback: mainWindow.playbackService
                anchorItem: playerBar.lastActionAnchor
                uiScale: mainWindow.shellSettings.uiScale
                onSettingsRequested: preferencesOwner.open({
                    initialSection: "cast"
                })
            }
        }
        EchoPopupLoader {
            id: sleepTimerLoader
            objectName: "sleepTimerPopupOwner"
            hostItem: mainWindow.contentItem
            sourceComponent: SleepTimerPopup {
                playback: mainWindow.playbackService
                anchorItem: playerBar.lastActionAnchor
                uiScale: mainWindow.shellSettings.uiScale
            }
        }
        ToastViewport {
            parent: Overlay.overlay
            service: mainWindow.shellController.toasts
            uiScale: mainWindow.shellSettings.uiScale
            anchorItem: playbackPageLoader.item && (playbackPageLoader.item as PlaybackPage).visible ? (playbackPageLoader.item as PlaybackPage).toastAnchor : playerBar
        }
        Connections {
            target: Theme.store.extensions
            function onRequestRaised(action) {
                if (action === "themes") {
                    mainWindow.windowService.floating.showMain();
                    mainWindow.router.navigate("themes");
                    return;
                }
                if (action === "closePlayback") {
                    playbackPageLoader.close();
                    return;
                }
                if (action === "settings") {
                    mainWindow.router.navigate("settings");
                    return;
                }
                if (action === "playback") {
                    playbackPageLoader.open();
                    return;
                }
                if (action === "skins" || action === "source" || action === "comments" || action === "barrage") {
                    playbackPageLoader.open();
                    Qt.callLater(function () {
                        const page = playbackPageLoader.item as PlaybackPage;
                        if (!page)
                            return;
                        if (action === "skins")
                            page.openSkins();
                        else if (action === "source")
                            page.openSources();
                        else
                            mainWindow.shellController.playbackPopupRequested(action);
                    });
                }
            }
        }
        Connections {
            target: mainWindow.shellController
            function onPlaybackPopupRequested(kind) {
                if (kind === "lyric-skins" && playbackPageLoader.item) {
                    (playbackPageLoader.item as PlaybackPage).openSkins();
                } else if ((kind === "comments" || kind === "barrage") && playbackPageLoader.item) {
                    const host = (playbackPageLoader.item as PlaybackPage).discussionHost as PlaybackDiscussionHost;
                    if (host) {
                        if (kind === "comments")
                            host.openComments();
                        else
                            host.openBarrage();
                    }
                } else if (kind === "queue") {
                    playbackQueueLoader.open();
                } else if (kind === "effect") {
                    effectLoader.open();
                } else if (kind === "cast") {
                    castLoader.open();
                } else if (kind === "sleep-timer") {
                    sleepTimerLoader.open();
                } else {
                    playbackOptionsLoader.open({
                        kind: kind
                    });
                }
            }
            function onTaskCenterRequested() {
                if (taskCenterOwner.opened)
                    taskCenterOwner.close();
                else
                    taskCenterOwner.open();
            }
        }
        Connections {
            target: mainWindow.router
            function onSettingsRequested() {
                preferencesOwner.open();
            }
            function onDismissOverlays() {
                preferencesOwner.close();
                taskCenterOwner.close();
                mainWindow.accountController.closeLogin();
            }
        }
    }
    // Right clicks that no one above accepts would otherwise travel out of the
    // QML scene: Qt hands the unconsumed WM_CONTEXTMENU back to the shell, which
    // pops the window system menu over the app. This layer sits behind the shell
    // and swallows them; areas that do use the right button (the song lists'
    // context menus, text fields) still win because they are above it.
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.RightButton
        onPressed: mouse => mouse.accepted = true
    }
    Loader {
        id: shellLoader
        objectName: "mainShellLoader"
        width: mainWindow.width / mainWindow.uiScale
        height: mainWindow.height / mainWindow.uiScale
        active: !mainWindow.startupPending
        sourceComponent: MainShell {}
    }
    // Native frame resize keeps OS snap/Wayland semantics. This fixed set of
    // eight handles is decoration, never a list of business records.
    Repeater {
        model: Qt.platform.os === "osx" ? [] : [Qt.LeftEdge, Qt.RightEdge, Qt.TopEdge, Qt.BottomEdge, Qt.LeftEdge | Qt.TopEdge, Qt.RightEdge | Qt.TopEdge, Qt.LeftEdge | Qt.BottomEdge, Qt.RightEdge | Qt.BottomEdge]
        delegate: MouseArea {
            required property int modelData
            readonly property bool horizontalEdge: (modelData & (Qt.LeftEdge | Qt.RightEdge)) !== 0
            readonly property bool verticalEdge: (modelData & (Qt.TopEdge | Qt.BottomEdge)) !== 0
            width: horizontalEdge ? 6 : mainWindow.width - 12
            height: verticalEdge ? 6 : mainWindow.height - 12
            x: modelData & Qt.RightEdge ? mainWindow.width - width : horizontalEdge ? 0 : 6
            y: modelData & Qt.BottomEdge ? mainWindow.height - height : verticalEdge ? 0 : 6
            visible: !mainWindow.windowService.maximized && !mainWindow.windowService.fullscreen
            cursorShape: horizontalEdge && verticalEdge ? ((modelData === (Qt.LeftEdge | Qt.TopEdge) || modelData === (Qt.RightEdge | Qt.BottomEdge)) ? Qt.SizeFDiagCursor : Qt.SizeBDiagCursor) : horizontalEdge ? Qt.SizeHorCursor : Qt.SizeVerCursor
            onPressed: mainWindow.startSystemResize(modelData)
        }
    }
}
