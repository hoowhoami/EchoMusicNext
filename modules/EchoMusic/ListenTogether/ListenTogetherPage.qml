pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Shell
import EchoMusic.Controls
import EchoMusic.Theme
import EchoMusic.Playback

RoutePage {
    id: page
    objectName: "listenTogetherPage"
    required property ListenTogetherUi service
    property bool sessionMinimized: false
    readonly property bool roomJoined: room.joined
    readonly property real discoveryInset: responsiveWidth <= 820 ? 18 : 30
    onRoomJoinedChanged: if (!roomJoined)
        sessionMinimized = false
    readonly property ListenTogetherController room: service.room
    readonly property alias scrollContainer: roomViewport
    readonly property real responsiveWidth: Window.window ? Window.window.width / settings.uiScale : width
    readonly property bool resourcesActive: pageActive && Window.window !== null && Window.window.visible && Window.window.visibility !== Window.Minimized
    onPageActiveChanged: if (!pageActive)
        dialogs.closeAll()
    ListenRoomDialogs {
        id: dialogs
        host: page
        uiScale: page.settings.uiScale
        service: page.service
        onMinimizeRequested: {
            page.sessionMinimized = true;
            if (page.room.roomScope === 1)
                page.room.loadRooms();
            else
                page.room.roomScope = 1;
            roomGrid.positionViewAtBeginning();
        }
    }
    EchoPageScrollContainer {
        id: roomViewport
        anchors.fill: parent
        visible: !page.room.joined || page.sessionMinimized
        pageActive: page.pageActive
        scrollView: roomGrid
        GridView {
            id: roomGrid
            objectName: "listenRoomGrid"
            anchors.horizontalCenter: parent.horizontalCenter
            width: Math.min(1220, parent.width)
            height: parent.height
            clip: true
            model: page.room.rooms
            property int columns: page.responsiveWidth > 1050 ? 3 : page.responsiveWidth > 600 ? 2 : 1
            cellWidth: Math.max(1, (width - page.discoveryInset * 2 + 14) / columns)
            cellHeight: 230
            cacheBuffer: cellHeight
            boundsBehavior: Flickable.StopAtBounds
            header: ColumnLayout {
                width: roomGrid.width
                spacing: 20
                Rectangle {
                    id: hero
                    Layout.fillWidth: true
                    Layout.leftMargin: page.discoveryInset
                    Layout.rightMargin: page.discoveryInset
                    objectName: "listenDiscoveryHero"
                    readonly property bool compact: page.responsiveWidth <= 600
                    readonly property bool medium: page.responsiveWidth <= 820
                    readonly property real insetX: compact ? 22 : medium ? 26 : 32
                    readonly property real insetY: compact ? 22 : medium ? 23 : 25
                    Layout.topMargin: medium ? 18 : 20
                    implicitHeight: Math.max(compact ? 204 : medium ? 164 : 174, heroContent.implicitHeight + insetY * 2)
                    radius: Theme.dialogRadius
                    gradient: Gradient {
                        orientation: Gradient.Horizontal
                        GradientStop {
                            position: 0
                            color: Theme.mix(Theme.card, Theme.accent, 0.16)
                        }
                        GradientStop {
                            position: 0.58
                            color: Theme.card
                        }
                    }
                    EchoDashedFrame {
                        anchors.fill: parent
                        solid: true
                        strokeColor: Theme.mix(Theme.border, Theme.accent, 0.18)
                        radius: Theme.dialogRadius
                    }
                    ListenSurface {
                        anchors.fill: parent
                        anchors.margins: 1
                        radius: Theme.dialogRadius - 1
                    }
                    Rectangle {
                        anchors.right: parent.right
                        anchors.rightMargin: hero.compact ? 20 : hero.medium ? 26 : 42
                        anchors.verticalCenter: hero.compact ? undefined : parent.verticalCenter
                        anchors.top: hero.compact ? parent.top : undefined
                        anchors.topMargin: 20
                        width: hero.compact ? 68 : hero.medium ? 108 : 126
                        height: width
                        radius: width / 2
                        rotation: hero.compact ? 0 : -8
                        opacity: hero.medium ? .62 : .78
                        color: Theme.withAlpha(Theme.accent, .11)
                        EchoDashedFrame {
                            anchors.fill: parent
                            solid: true
                            strokeColor: Theme.withAlpha(Theme.accent, .18)
                            radius: width / 2
                            // The disc rotates; pixel snapping only covers
                            // axis-aligned geometry, so keep the unsnapped
                            // antialiased fallback for the rotated border.
                            pixelAligned: false
                        }
                        EchoIcon {
                            anchors.centerIn: parent
                            width: hero.compact ? 36 : 84
                            height: width
                            name: "headphones"
                            color: Theme.mix(Theme.accentText, "white", .28)
                        }
                    }
                    ColumnLayout {
                        id: heroContent
                        anchors.left: parent.left
                        anchors.leftMargin: hero.insetX
                        anchors.verticalCenter: hero.compact ? undefined : parent.verticalCenter
                        anchors.bottom: hero.compact ? parent.bottom : undefined
                        anchors.bottomMargin: hero.insetY
                        width: Math.min(hero.compact ? Number.MAX_VALUE : 600, Math.max(0, parent.width - hero.insetX * 2))
                        spacing: 0
                        EchoButtonContent {
                            glyph: "broadcast"
                            glyphSize: 14
                            text: "LISTEN TOGETHER"
                            color: Theme.accentText
                            font: Theme.spacedFont(11, 900, 1.4)
                            horizontalAlignment: Qt.AlignLeft
                        }
                        Label {
                            Layout.fillWidth: true
                            Layout.topMargin: 9
                            Layout.bottomMargin: 6
                            text: qsTr("此刻，一起听")
                            font: Theme.spacedFont(Math.min(37, Math.max(29, page.responsiveWidth * .032)), 950, -1.25)
                            lineHeightMode: Text.FixedHeight
                            lineHeight: font.pixelSize * 1.08
                            color: Theme.text
                        }
                        Label {
                            Layout.fillWidth: true
                            Layout.maximumWidth: hero.compact ? Math.max(0, heroContent.width - 46) : 560
                            text: qsTr("进入正在播放的房间，和喜欢同一种声音的人共享音乐。")
                            wrapMode: Text.Wrap
                            font: Theme.font(12, 600)
                            lineHeightMode: Text.FixedHeight
                            lineHeight: font.pixelSize * 1.6
                            color: Theme.secondaryText
                        }
                        GridLayout {
                            Layout.topMargin: 15
                            Layout.fillWidth: false
                            Layout.alignment: Qt.AlignLeft
                            columns: hero.compact ? 1 : 2
                            columnSpacing: 14
                            rowSpacing: 10
                            EchoButton {
                                objectName: "listenCreateAction"
                                text: qsTr("创建众乐房")
                                glyph: "plus"
                                onClicked: dialogs.createRoom()
                            }
                            ListenLiveBadge {
                                kind: "hero"
                                text: qsTr("%1 个房间正在播放").arg(page.room.rooms.count)
                            }
                        }
                    }
                }
                RowLayout {
                    visible: page.room.joined
                    Layout.fillWidth: true
                    Layout.leftMargin: page.discoveryInset
                    Layout.rightMargin: page.discoveryInset
                    Label {
                        Layout.fillWidth: true
                        text: qsTr("房间正在后台同步 · %1").arg(page.room.activeRoom.name || "")
                        color: Theme.text
                        font: Theme.font(14, 700)
                    }
                    EchoButton {
                        objectName: "listenRoomReturnAction"
                        text: qsTr("返回房间")
                        glyph: "headphones"
                        onClicked: page.sessionMinimized = false
                    }
                }
                EchoTabBar {
                    Layout.leftMargin: page.discoveryInset
                    Layout.preferredWidth: 260
                    labels: [qsTr("发现房间"), qsTr("我的房间")]
                    currentIndex: page.room.roomScope
                    onPicked: index => page.room.roomScope = index
                }
                RowLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: page.discoveryInset
                    Layout.rightMargin: page.discoveryInset
                    spacing: 12
                    EchoTextField {
                        objectName: "listenRoomFilter"
                        Layout.fillWidth: true
                        placeholderText: qsTr("筛选已加载的众乐房、房主或歌曲")
                        text: page.room.search
                        onTextEdited: page.room.search = text
                    }
                    EchoButton {
                        objectName: "listenRoomsRefresh"
                        implicitWidth: implicitHeight
                        leftPadding: 0
                        rightPadding: 0
                        accent: false
                        glyph: "refresh"
                        Accessible.name: qsTr("刷新房间")
                        enabled: !page.room.loadingRooms
                        onClicked: page.room.loadRooms()
                    }
                }
                ColumnLayout {
                    Layout.leftMargin: page.discoveryInset
                    Layout.bottomMargin: 4
                    spacing: 5
                    Label {
                        text: page.room.roomScope ? qsTr("我的众乐房") : qsTr("众乐房")
                        font: Theme.font(18, 900)
                        color: Theme.text
                    }
                    Label {
                        text: qsTr("%1 个结果").arg(page.room.rooms.count)
                        font: Theme.font(11, 650)
                        color: Theme.secondaryText
                    }
                }
                Loader {
                    Layout.fillWidth: true
                    Layout.leftMargin: page.discoveryInset
                    Layout.rightMargin: page.discoveryInset
                    active: page.room.loadingRooms && page.room.rooms.count === 0
                    visible: active
                    sourceComponent: Component {
                        GridLayout {
                            columns: roomGrid.columns
                            rowSpacing: 16
                            columnSpacing: 16
                            Repeater {
                                model: 6
                                Rectangle {
                                    Layout.fillWidth: true
                                    implicitHeight: 137
                                    color: Theme.card
                                    radius: Theme.cardRadius
                                    EchoDashedFrame {
                                        anchors.fill: parent
                                        solid: true
                                        strokeColor: Theme.border
                                        radius: Theme.cardRadius
                                    }
                                    ColumnLayout {
                                        anchors.fill: parent
                                        anchors.leftMargin: 12
                                        anchors.rightMargin: 12
                                        anchors.topMargin: 12
                                        anchors.bottomMargin: 18
                                        spacing: 14
                                        EchoSkeleton {
                                            Layout.fillWidth: true
                                            Layout.preferredHeight: 48
                                            animate: page.resourcesActive
                                        }
                                        EchoSkeleton {
                                            Layout.preferredWidth: parent.width * 0.7
                                            Layout.preferredHeight: 18
                                            radius: 99
                                            animate: page.resourcesActive
                                        }
                                        EchoSkeleton {
                                            Layout.preferredWidth: parent.width * 0.46
                                            Layout.preferredHeight: 13
                                            radius: 99
                                            animate: page.resourcesActive
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                Label {
                    visible: page.room.errorMessage.length > 0
                    Layout.leftMargin: page.discoveryInset
                    text: page.room.errorMessage
                    color: Theme.secondaryText
                    font: Theme.font(12, 500)
                }
            }
            delegate: Item {
                id: slot
                required property var rowData
                width: roomGrid.cellWidth
                height: roomGrid.cellHeight
                ListenRoomCard {
                    x: page.discoveryInset
                    width: Math.max(0, slot.width - 14)
                    height: implicitHeight
                    room: slot.rowData
                    mine: page.room.roomScope === 1
                    resourcesActive: page.resourcesActive && roomViewport.visible
                    onInspect: roomId => page.room.inspectRoom(roomId)
                }
            }
            footer: ColumnLayout {
                width: roomGrid.width
                spacing: 20
                Label {
                    visible: !page.room.loadingRooms && page.room.rooms.count === 0
                    Layout.alignment: Qt.AlignHCenter
                    Layout.topMargin: 30
                    text: page.room.loadingRooms ? qsTr("正在加载房间…") : page.room.roomScope ? qsTr("还没有可管理的众乐房") : qsTr("没有找到匹配的房间")
                    color: Theme.secondaryText
                    font: Theme.font(16, 700)
                }
                EchoButton {
                    visible: !page.room.roomScope && page.room.rooms.count > 0 && !page.room.roomsEnded && !page.room.search
                    Layout.alignment: Qt.AlignHCenter
                    text: qsTr("加载更多房间")
                    loading: page.room.loadingRooms
                    onClicked: page.room.loadRooms(false)
                }
                Item {
                    implicitHeight: 42
                }
            }
            ScrollBar.vertical: EchoPageScrollBar {
                view: roomGrid
                container: page.scrollContainer
            }
        }
    }
    ListenSession {
        anchors.fill: parent
        visible: page.room.joined && !page.sessionMinimized
        room: page.room
        sharing: page.service.playback.sharing
        viewportWidth: page.responsiveWidth
        resourcesActive: page.resourcesActive && visible
        onManage: dialogs.manageRoom()
        onPickSong: dialogs.pickSong()
        onShowOrders: dialogs.showOrders()
    }
}
