pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Theme
import EchoMusic.Controls
import EchoMusic.Shell
import EchoMusic.Account
import EchoMusic.Library

EchoUiPartHost {
    id: sidebar
    AppearanceHost {
        anchors.fill: parent
        entry: Theme.store.extensions.theme
        placement: "sidebar"
        z: 0
    }
    partRole: "shell.sidebar"
    partKey: "shell.sidebar"
    required property RouteController router
    required property ShellSettings settings
    required property real uiScale
    required property AccountController accountController
    required property LibraryController libraryController
    readonly property int playlistTab: libraryController.tab
    // User alignment adjustment: move expanded rows 2px right to the header's
    // actual text edge (30px + Basic.Button's 2px horizontal padding).
    // Keep the 14px inner padding and the collapsed rail centered.
    readonly property real playlistRowLeftInset: 18
    Binding {
        target: sidebar.settings.sidebarLayout
        property: "libraryController"
        value: sidebar.libraryController
    }
    readonly property bool collapsed: settings.sidebarCollapsed
    // One reversible clock drives both the shell width and every internal slot.
    // Boolean mode changes are reserved for commands and endpoint visibility.
    property real resizeProgress: collapsed ? 1 : 0
    // Layouts polish width changes after the animation tick. Derive internal
    // presentation from the allocated width so it never runs a frame ahead.
    readonly property real collapseProgress: resizeScope.allowed ? Math.max(0, Math.min(1, (230 - width) / 150)) : resizeProgress
    readonly property bool resizing: resizeAnimation.running
    readonly property real expandedOpacity: Math.max(0, 1 - collapseProgress * 2)
    // Unfold rows before merging columns, reversing that order on expand.
    // The overlap leaves a gentle handoff without cards passing through peers.
    readonly property real shortcutProgress: Theme.sidebarPhase(resizeProgress, 0, 0.4)
    implicitWidth: 230 - 150 * Theme.sidebarPhase(resizeProgress, 0.35, 1)
    clip: true
    EchoMotionScope {
        id: resizeScope
        target: sidebar
        animations: [resizeAnimation]
    }
    Behavior on resizeProgress {
        enabled: resizeScope.allowed
        NumberAnimation {
            id: resizeAnimation
            duration: Theme.sidebarResizeDuration
            easing.type: Easing.Linear
        }
    }
    // Resizing reuses this content tree; replaying its entry fade would blank
    // every row on each toggle, including rapid reversals of the width motion.
    EchoEnter {
        id: contentEntry
        target: content
        duration: Theme.sidebarDuration
        curve: Theme.easeEmphasized
    }
    EchoPopupLoader {
        id: createDialogOwner
        objectName: "playlistCreateDialogOwner"
        hostItem: sidebar
        sourceComponent: Component {
            PlaylistCreateDialog {
                id: createDialog
                service: sidebar.libraryController
                uiScale: sidebar.uiScale
            }
        }
    }
    EchoPopupLoader {
        id: importDialogOwner
        objectName: "playlistImportDialogOwner"
        hostItem: sidebar.Window.window ? sidebar.Window.window.contentItem : sidebar
        sourceComponent: Component {
            PlaylistImportDialog {
                id: importDialog
                service: sidebar.libraryController
                uiScale: sidebar.uiScale
            }
        }
    }
    Connections {
        target: sidebar.libraryController.importer
        function onShowRequested(): void {
            importDialogOwner.open({
                duplicateDraft: sidebar.libraryController.importer.duplicateName
            });
        }
        function onDuplicateConfirmationRequested(): void {
            importDialogOwner.open({
                duplicateDraft: sidebar.libraryController.importer.duplicateName
            });
        }
        function onAccountInvalidated(): void {
            importDialogOwner.close();
        }
    }
    Connections {
        target: sidebar.Window.window
        function onVisibilityChanged(): void {
            const window = sidebar.Window.window;
            const importer = sidebar.libraryController.importer;
            // The provider's pending question belongs to the service, even if
            // its UI was disposed while the window was hidden/minimized.
            if (window && window.visible && window.visibility !== Window.Minimized && importer.phase === PlaylistImportService.Preparing && importer.duplicateName.length)
                importDialogOwner.open({
                    duplicateDraft: importer.duplicateName
                });
        }
    }
    EchoPopupLoader {
        id: orderDialogOwner
        objectName: "playlistOrderDialogOwner"
        hostItem: sidebar
        sourceComponent: Component {
            PlaylistOrderDialog {
                id: orderDialog
                editor: sidebar.libraryController.order
                playlistTab: sidebar.libraryController.tab
                uiScale: sidebar.uiScale
            }
        }
    }
    EchoUiColumn {
        id: content
        partRole: "layout.sidebar"
        partKey: "content"
        objectName: "sidebarContent"
        anchors.fill: parent
        spacing: 0
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: Math.max(46, 35 / sidebar.uiScale)
        }
        SidebarAccount {
            Layout.fillWidth: true
            Layout.preferredHeight: 56 - 14 * sidebar.collapseProgress
            Layout.leftMargin: 18 - 10 * sidebar.collapseProgress
            Layout.rightMargin: 18 - 10 * sidebar.collapseProgress
            Layout.bottomMargin: 22 - 14 * sidebar.collapseProgress
            service: sidebar.accountController
            collapsed: sidebar.collapsed
            collapseProgress: sidebar.collapseProgress
            uiScale: sidebar.uiScale
            onProfileRequested: sidebar.router.navigate("profile")
        }
        ListView {
            id: sidebarScroll
            objectName: "sidebarPlaylistScroll"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: sidebar.settings.sidebarLayout.playlistAreaVisible ? sidebar.libraryController.playlists : null
            property bool keepHeaderStart: false
            onMovementStarted: keepHeaderStart = false
            onAtYBeginningChanged: {
                if (atYBeginning && sidebar.resizing)
                    keepHeaderStart = true;
            }
            Connections {
                target: sidebar
                function onCollapsedChanged(): void {
                    sidebarScroll.captureHeaderPosition();
                }
            }
            function captureHeaderPosition() {
                keepHeaderStart = atYBeginning;
            }
            function restoreHeaderPosition() {
                if (!keepHeaderStart)
                    return;
                Qt.callLater(() => {
                    sidebarScroll.forceLayout();
                    sidebarScroll.positionViewAtBeginning();
                });
            }
            Connections {
                target: sidebar.settings.sidebarLayout.shortcutModel
                function onRowsAboutToBeInserted() {
                    sidebarScroll.captureHeaderPosition();
                }
                function onRowsAboutToBeRemoved() {
                    sidebarScroll.captureHeaderPosition();
                }
                function onRowsInserted() {
                    sidebarScroll.restoreHeaderPosition();
                }
                function onRowsRemoved() {
                    sidebarScroll.restoreHeaderPosition();
                }
            }
            // Plugin menus arrive after startup and change the inline header's
            // extent just like shortcut edits. Capture before Qt adjusts the
            // outer ListView origin, while atYBeginning still describes the user.
            Connections {
                target: sidebar.settings.sidebarLayout.navigationModel
                function onRowsAboutToBeInserted() {
                    sidebarScroll.captureHeaderPosition();
                }
                function onRowsAboutToBeRemoved() {
                    sidebarScroll.captureHeaderPosition();
                }
                function onRowsAboutToBeMoved() {
                    sidebarScroll.captureHeaderPosition();
                }
                function onModelAboutToBeReset() {
                    sidebarScroll.captureHeaderPosition();
                }
                function onRowsInserted() {
                    sidebarScroll.restoreHeaderPosition();
                }
                function onRowsRemoved() {
                    sidebarScroll.restoreHeaderPosition();
                }
                function onRowsMoved() {
                    sidebarScroll.restoreHeaderPosition();
                }
                function onModelReset() {
                    sidebarScroll.restoreHeaderPosition();
                }
            }
            Connections {
                target: sidebarScroll.headerItem
                function onHeightChanged() {
                    sidebarScroll.restoreHeaderPosition();
                }
            }
            reuseItems: true
            EchoViewPoolGuard {
                targetView: sidebarScroll
            }
            cacheBuffer: 40 * 3
            spacing: 2 + 5 * sidebar.collapseProgress
            boundsBehavior: Flickable.StopAtBounds
            // Keep the normal shared reveal/overflow policy in both sidebar
            // modes, with Qt owning the scroll ratio and thumb drag mapping.
            ScrollBar.vertical: EchoPageScrollBar {
                objectName: "sidebarScrollBar"
                view: sidebarScroll
                viewActive: sidebar.visible && sidebar.Window.window !== null && sidebar.Window.window.visible && sidebar.Window.window.visibility !== Window.Minimized
            }
            // ListView owns the root delegate position. Keep the row inset
            // on an inner row so view layout/reuse cannot overwrite it.
            delegate: Item {
                id: playlistDelegate
                required property string title
                required property string coverUrl
                required property double cloudListId
                required property string globalId
                required property string creatorGlobalId
                required property string kind
                required property var defaultKind
                property bool pooled: false
                width: sidebarScroll.width
                height: playlistRow.height
                ListView.onPooled: pooled = true
                ListView.onReused: pooled = false
                SidebarPlaylistRow {
                    id: playlistRow
                    width: Math.max(0, playlistDelegate.width - sidebar.playlistRowLeftInset - 16 - 4 * sidebar.collapseProgress)
                    x: sidebar.playlistRowLeftInset + sidebar.collapseProgress
                    title: playlistDelegate.title
                    coverUrl: playlistDelegate.coverUrl
                    cloudListId: playlistDelegate.cloudListId
                    globalId: playlistDelegate.globalId
                    creatorGlobalId: playlistDelegate.creatorGlobalId
                    kind: playlistDelegate.kind
                    defaultKind: playlistDelegate.defaultKind
                    collapsed: sidebar.collapsed
                    collapseProgress: sidebar.collapseProgress
                    busy: sidebar.libraryController.busy
                    resourceActive: sidebar.visible && !playlistDelegate.pooled
                    selected: sidebar.router.current && sidebar.router.current.name === "playlist-detail" && sidebar.router.current.resourceId === (kind === "owned" ? globalId : creatorGlobalId)
                    onOpenRequested: cloudId => sidebar.libraryController.openPlaylist(cloudId)
                    playbackBusy: sidebar.libraryController.pendingPlayListId === cloudListId
                    onPlayRequested: cloudId => sidebar.libraryController.playPlaylist(cloudId)
                }
            }
            footer: ColumnLayout {
                width: sidebarScroll.width
                Label {
                    Layout.fillWidth: true
                    Layout.topMargin: 28
                    Layout.bottomMargin: 28
                    visible: sidebar.settings.sidebarLayout.playlistAreaVisible && sidebar.libraryController.playlists.count === 0
                    opacity: sidebar.expandedOpacity
                    text: !sidebar.libraryController.signedIn ? qsTr("登录同步云端歌单") : sidebar.libraryController.loading ? qsTr("正在加载歌单…") : sidebar.playlistTab === 0 ? qsTr("暂无自建歌单") : qsTr("暂无收藏内容")
                    color: Theme.secondaryText
                    font: Theme.decoratedFont(12, Theme.regularWeight, 0, true, false)
                    horizontalAlignment: Text.AlignHCenter
                }
                Label {
                    Layout.fillWidth: true
                    Layout.leftMargin: 16 + 5 * sidebar.collapseProgress
                    Layout.rightMargin: 16 + 5 * sidebar.collapseProgress
                    visible: text.length > 0 && sidebar.collapseProgress < 1
                    opacity: sidebar.expandedOpacity
                    text: sidebar.libraryController.errorMessage
                    color: Theme.colors.dangerText
                    font: Theme.font(12, Theme.regularWeight)
                    wrapMode: Text.Wrap
                }
            }
            header: ColumnLayout {
                width: sidebarScroll.width
                spacing: 0
                SidebarShortcuts {
                    objectName: "sidebarShortcuts"
                    railProgress: sidebar.shortcutProgress
                    Layout.fillWidth: true
                    // The rail adds 8px padding outside the shortcut component's 10px inset.
                    Layout.leftMargin: 18
                    Layout.rightMargin: 18
                    layout: sidebar.settings.sidebarLayout
                    router: sidebar.router
                    collapsed: sidebar.collapsed
                    collapseProgress: sidebar.collapseProgress
                    resourceActive: sidebar.visible && sidebar.Window.window !== null && sidebar.Window.window.visible && sidebar.Window.window.visibility !== Window.Minimized
                    onAddRequested: shortcutPopupOwner.open()
                }
                EchoPopupLoader {
                    id: shortcutPopupOwner
                    objectName: "sidebarShortcutPopupOwner"
                    hostItem: sidebar
                    sourceComponent: Component {
                        SidebarShortcutPopup {
                            id: shortcutPopup
                            router: sidebar.router
                            layout: sidebar.settings.sidebarLayout
                            anchorItem: sidebar
                            uiScale: sidebar.uiScale
                        }
                    }
                }
                SidebarNavigation {
                    id: menu
                    objectName: "sidebarMusicMenu"
                    visible: sidebar.settings.sidebarLayout.menuVisible
                    Layout.fillWidth: true
                    Layout.preferredWidth: -1
                    Layout.alignment: Qt.AlignHCenter
                    Layout.leftMargin: 16 + 5 * sidebar.collapseProgress
                    Layout.rightMargin: 16 + 5 * sidebar.collapseProgress
                    Layout.topMargin: 24 - 12 * sidebar.collapseProgress
                    layout: sidebar.settings.sidebarLayout
                    router: sidebar.router
                    collapsed: sidebar.collapsed
                    collapseProgress: sidebar.collapseProgress
                }
                SidebarPlaylistHeader {
                    visible: sidebar.settings.sidebarLayout.playlistAreaVisible
                    Layout.fillWidth: true
                    Layout.preferredHeight: implicitHeight
                    Layout.bottomMargin: 8
                    Layout.topMargin: (menu.visible ? 18 : 24) * (1 - sidebar.collapseProgress) + 12 * sidebar.collapseProgress
                    Layout.leftMargin: 30 - 22 * sidebar.collapseProgress
                    Layout.rightMargin: 12 - 4 * sidebar.collapseProgress
                    service: sidebar.libraryController
                    uiScale: sidebar.uiScale
                    collapsed: sidebar.collapsed
                    collapseProgress: sidebar.collapseProgress
                    onOrderRequested: orderDialogOwner.open()
                    onCreateRequested: createDialogOwner.open()
                    onImportRequested: {
                        if (!sidebar.libraryController.importer.busy)
                            sidebar.libraryController.importer.dismiss();
                        importDialogOwner.open();
                    }
                }
            }
        }
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: 32
            Layout.topMargin: 6
            Layout.bottomMargin: 10
            EchoUiRow {
                partRole: "layout.sidebar"
                partKey: "row2"
                anchors.centerIn: parent
                anchors.alignWhenCentered: false
                width: 152 - 120 * sidebar.collapseProgress
                height: 32
                spacing: 0
                EchoIconButton {
                    objectName: "sidebarCollapseTrigger"
                    glyph: "circle-chevron-left"
                    text: sidebar.collapsed ? qsTr("展开侧栏") : qsTr("折叠侧栏")
                    implicitWidth: 32
                    implicitHeight: 32
                    contentItem: EchoIcon {
                        name: "circle-chevron-left"
                        color: parent.foreground
                        rotation: -180 * sidebar.collapseProgress
                    }
                    onClicked: sidebar.settings.toggleSidebar()
                }
                Item {
                    Layout.preferredWidth: 120 * (1 - sidebar.collapseProgress)
                    Layout.preferredHeight: 32
                    visible: sidebar.collapseProgress < 1
                    opacity: sidebar.expandedOpacity
                    enabled: !sidebar.collapsed
                    RowLayout {
                        x: 8
                        width: 112
                        height: 32
                        spacing: 8
                        EchoIconButton {
                            id: layoutTrigger
                            objectName: "sidebarLayoutTrigger"
                            glyph: "layout-sidebar"
                            text: qsTr("编辑侧栏布局")
                            onClicked: layoutPopupOwner.open()
                            implicitWidth: 32
                            implicitHeight: 32
                        }
                        EchoIconButton {
                            glyph: "settings"
                            text: qsTr("设置")
                            implicitWidth: 32
                            implicitHeight: 32
                            onClicked: sidebar.router.navigate("settings")
                        }
                        EchoIconButton {
                            glyph: "shirt"
                            text: qsTr("主题中心")
                            implicitWidth: 32
                            implicitHeight: 32
                            onClicked: sidebar.router.navigate("themes")
                        }
                    }
                }
            }
        }
    }
    EchoPopupLoader {
        id: layoutPopupOwner
        objectName: "sidebarLayoutPopupOwner"
        hostItem: sidebar
        sourceComponent: Component {
            SidebarLayoutPopup {
                id: layoutPopup
                layout: sidebar.settings.sidebarLayout
                anchorItem: layoutTrigger
                uiScale: sidebar.uiScale
            }
        }
    }
}
