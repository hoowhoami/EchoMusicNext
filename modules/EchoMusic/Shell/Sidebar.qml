pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Theme
import EchoMusic.Controls
import EchoMusic.Shell
import EchoMusic.Account
import EchoMusic.Library

Item {
    id: sidebar
    AppearanceHost {
        anchors.fill: parent
        entry: Theme.store.extensions.theme
        placement: "sidebar"
        z: 0
    }
    EchoUiPart {
        id: publicSurface
        target: sidebar
        role: "shell.sidebar"
        key: "shell.sidebar"
        properties: ["background", "borderColor", "borderWidth", "radius", "opacity"]
    }
    opacity: publicSurface.value("opacity", 1)
    Loader {
        anchors.fill: parent
        z: -0.5
        active: sidebar.visible && sidebar.Window.window !== null && sidebar.Window.window.visible && sidebar.Window.window.visibility !== Window.Minimized && Object.keys(publicSurface.overrides).some(name => name !== "opacity")
        sourceComponent: Rectangle {
            color: publicSurface.value("background", "transparent")
            radius: publicSurface.value("radius", 0)
            border.color: publicSurface.value("borderColor", "transparent")
            border.width: publicSurface.value("borderWidth", 0)
        }
    }
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
    EchoPopupLoader {
        id: removeDialogOwner
        objectName: "playlistRemoveDialogOwner"
        hostItem: sidebar
        sourceComponent: Component {
            PlaylistRemoveDialog {
                id: removeDialog
                service: sidebar.libraryController
                uiScale: sidebar.uiScale
            }
        }
    }
    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: Math.max(46, 35 / sidebar.uiScale)
        }
        SidebarAccount {
            Layout.fillWidth: true
            Layout.preferredHeight: sidebar.collapsed ? 42 : 56
            Layout.leftMargin: sidebar.collapsed ? 8 : 18
            Layout.rightMargin: sidebar.collapsed ? 8 : 18
            Layout.bottomMargin: sidebar.collapsed ? 8 : 22
            service: sidebar.accountController
            collapsed: sidebar.collapsed
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
            Connections {
                target: sidebarScroll.headerItem
                function onHeightChanged() {
                    sidebarScroll.restoreHeaderPosition();
                }
            }
            reuseItems: true
            cacheBuffer: 40 * 3
            spacing: sidebar.collapsed ? 7 : 2
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
                    width: sidebar.collapsed ? 42 : Math.max(0, playlistDelegate.width - sidebar.playlistRowLeftInset - 16)
                    x: sidebar.collapsed ? (playlistDelegate.width - width) / 2 : sidebar.playlistRowLeftInset
                    title: playlistDelegate.title
                    coverUrl: playlistDelegate.coverUrl
                    cloudListId: playlistDelegate.cloudListId
                    globalId: playlistDelegate.globalId
                    creatorGlobalId: playlistDelegate.creatorGlobalId
                    kind: playlistDelegate.kind
                    defaultKind: playlistDelegate.defaultKind
                    collapsed: sidebar.collapsed
                    busy: sidebar.libraryController.busy
                    resourceActive: sidebar.visible && !playlistDelegate.pooled
                    selected: sidebar.router.current && sidebar.router.current.name === "playlist-detail" && sidebar.router.current.resourceId === (kind === "owned" ? globalId : creatorGlobalId)
                    onOpenRequested: cloudId => sidebar.libraryController.openPlaylist(cloudId)
                    onRemoveRequested: (cloudId, title, owned) => {
                        removeDialogOwner.open({
                            cloudId: cloudId,
                            playlistTitle: title,
                            owned: owned
                        });
                    }
                }
            }
            footer: ColumnLayout {
                width: sidebarScroll.width
                Label {
                    Layout.fillWidth: true
                    Layout.topMargin: 28
                    Layout.bottomMargin: 28
                    visible: sidebar.settings.sidebarLayout.playlistAreaVisible && sidebar.libraryController.playlists.count === 0
                    text: sidebar.collapsed ? "" : !sidebar.libraryController.signedIn ? qsTr("登录同步云端歌单") : sidebar.libraryController.loading ? qsTr("正在加载歌单…") : sidebar.playlistTab === 0 ? qsTr("暂无自建歌单") : qsTr("暂无收藏内容")
                    color: Theme.secondaryText
                    font: Theme.decoratedFont(12, Theme.regularWeight, 0, true, false)
                    horizontalAlignment: Text.AlignHCenter
                }
                Label {
                    Layout.fillWidth: true
                    Layout.leftMargin: 16
                    Layout.rightMargin: 16
                    visible: text.length > 0 && !sidebar.collapsed
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
                    Layout.fillWidth: true
                    // The rail adds 8px padding outside the shortcut component's 10px inset.
                    Layout.leftMargin: 18
                    Layout.rightMargin: 18
                    layout: sidebar.settings.sidebarLayout
                    router: sidebar.router
                    collapsed: sidebar.collapsed
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
                    Layout.fillWidth: !sidebar.collapsed
                    Layout.preferredWidth: sidebar.collapsed ? 38 : -1
                    Layout.alignment: sidebar.collapsed ? Qt.AlignHCenter : Qt.AlignLeft
                    Layout.leftMargin: 16
                    Layout.rightMargin: 16
                    Layout.topMargin: sidebar.collapsed ? 12 : 24
                    layout: sidebar.settings.sidebarLayout
                    router: sidebar.router
                    collapsed: sidebar.collapsed
                }
                SidebarPlaylistHeader {
                    visible: sidebar.settings.sidebarLayout.playlistAreaVisible
                    Layout.fillWidth: true
                    Layout.preferredHeight: implicitHeight
                    Layout.bottomMargin: 8
                    Layout.topMargin: sidebar.collapsed ? 12 : menu.visible ? 18 : 24
                    Layout.leftMargin: sidebar.collapsed ? 8 : 30
                    Layout.rightMargin: sidebar.collapsed ? 8 : 12
                    service: sidebar.libraryController
                    uiScale: sidebar.uiScale
                    collapsed: sidebar.collapsed
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
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: sidebar.collapsed ? 8 : 16
            Layout.rightMargin: sidebar.collapsed ? 8 : 16
            Layout.topMargin: 6
            Layout.bottomMargin: 10
            spacing: 8
            Item {
                Layout.fillWidth: true
            }
            EchoIconButton {
                glyph: sidebar.collapsed ? "circle-chevron-right" : "circle-chevron-left"
                text: sidebar.collapsed ? qsTr("展开侧栏") : qsTr("折叠侧栏")
                implicitWidth: 32
                implicitHeight: 32
                onClicked: sidebar.settings.toggleSidebar()
            }
            EchoIconButton {
                id: layoutTrigger
                objectName: "sidebarLayoutTrigger"
                visible: !sidebar.collapsed
                glyph: "layout-sidebar"
                text: qsTr("编辑侧栏布局")
                tooltipText: text
                onClicked: layoutPopupOwner.open()
                implicitWidth: 32
                implicitHeight: 32
            }
            EchoIconButton {
                visible: !sidebar.collapsed
                glyph: "settings"
                text: qsTr("设置")
                implicitWidth: 32
                implicitHeight: 32
                onClicked: sidebar.router.navigate("settings")
            }
            EchoIconButton {
                visible: !sidebar.collapsed
                glyph: "shirt"
                text: qsTr("主题中心")
                tooltipText: text
                implicitWidth: 32
                implicitHeight: 32
                onClicked: sidebar.router.navigate("themes")
            }
            Item {
                Layout.fillWidth: true
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
