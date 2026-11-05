pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Controls
import EchoMusic.Theme
import EchoMusic.Playback
import EchoMusic.Music

Item {
    id: session
    required property ListenTogetherController room
    property bool resourcesActive: true
    property real viewportWidth: width
    readonly property real nowPlayingHeight: viewportWidth <= 600 ? 150 : 202
    signal manage
    signal pickSong
    signal showOrders
    property SharingCommands sharing: null
    Flickable {
        id: sessionScroll
        anchors.fill: parent
        anchors.leftMargin: session.viewportWidth <= 600 ? 14 : 24
        anchors.rightMargin: session.viewportWidth <= 600 ? 14 : 24
        anchors.topMargin: 18
        anchors.bottomMargin: 20
        readonly property bool narrow: session.viewportWidth <= 820
        clip: true
        interactive: narrow
        contentWidth: width
        contentHeight: body.height
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: EchoPageScrollBar {
            view: sessionScroll
            policy: sessionScroll.narrow ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
        }
        ColumnLayout {
            id: body
            width: sessionScroll.width
            height: sessionScroll.narrow ? implicitHeight : sessionScroll.height
            spacing: 14
            RowLayout {
                Layout.fillWidth: true
                Layout.preferredHeight: 58
                spacing: 14
                ListenLiveBadge {
                    kind: "session"
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    Label {
                        Layout.fillWidth: true
                        text: session.room.activeRoom.name || qsTr("一起听房间")
                        font: Theme.font(20, 950)
                        color: Theme.text
                        elide: Text.ElideRight
                    }
                    Label {
                        text: qsTr("%1 · %2 人在线").arg(session.room.activeRoom.owner?.name || qsTr("房主")).arg(session.room.onlineCount)
                        font: Theme.font(11, 700)
                        color: Theme.secondaryText
                    }
                }
                EchoButton {
                    accent: false
                    objectName: "listenRoomShareAction"
                    visible: session.sharing !== null
                    glyph: "share"
                    text: qsTr("分享房间")
                    onClicked: session.sharing.shareRoom(session.room.roomId, String(session.room.activeRoom.name || ""))
                }
                EchoButton {
                    objectName: "listenRoomManageAction"
                    accent: false
                    text: session.room.owner ? qsTr("房间管理") : qsTr("离开房间")
                    glyph: "door-exit"
                    onClicked: session.manage()
                }
            }
            RowLayout {
                visible: session.room.playbackDetached
                Layout.fillWidth: true
                Label {
                    Layout.fillWidth: true
                    text: qsTr("当前正在播放其他内容，房间仍在后台同步")
                    color: Theme.secondaryText
                    font: Theme.font(12, 600)
                }
                EchoButton {
                    text: qsTr("恢复房间播放")
                    onClicked: session.room.resumeRoomPlayback()
                }
            }
            GridLayout {
                columns: sessionScroll.narrow ? 1 : 2
                Layout.fillWidth: true
                Layout.fillHeight: !sessionScroll.narrow
                rowSpacing: 14
                columnSpacing: 14
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumWidth: 0
                    Layout.minimumHeight: sessionScroll.narrow ? 520 : 0
                    Layout.horizontalStretchFactor: 165
                    spacing: 14
                    Rectangle {
                        objectName: "listenNowPlayingCard"
                        Layout.fillWidth: true
                        Layout.fillHeight: false
                        Layout.minimumHeight: implicitHeight
                        Layout.maximumHeight: implicitHeight
                        implicitHeight: session.nowPlayingHeight
                        radius: Theme.dialogRadius
                        color: Theme.mix(Theme.card, Theme.accent, 0.08)
                        border.width: 1
                        border.color: Theme.mix(Theme.border, Theme.accent, 0.22)
                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: 18
                            spacing: session.viewportWidth <= 600 ? 14 : 24
                            EchoCover {
                                Layout.preferredWidth: session.viewportWidth <= 600 ? 100 : session.viewportWidth <= 1050 ? 140 : 176
                                Layout.preferredHeight: session.viewportWidth <= 600 ? 110 : session.viewportWidth <= 1050 ? 140 : 166
                                source: session.room.currentSong.cover_url || ""
                                resourceActive: session.resourcesActive
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 9
                                EchoButtonContent {
                                    text: "NOW PLAYING"
                                    glyph: "headphones"
                                    glyphSize: 14
                                    color: Theme.accentText
                                    font: Theme.spacedFont(11, 900, 1.4)
                                    horizontalAlignment: Qt.AlignLeft
                                }
                                Label {
                                    Layout.fillWidth: true
                                    text: session.room.currentSong.title || qsTr("正在同步房间播放")
                                    font: Theme.spacedFont(Math.max(21, Math.min(34, session.viewportWidth * 0.03)), 950, -1)
                                    color: Theme.text
                                    elide: Text.ElideRight
                                }
                                Label {
                                    Layout.fillWidth: true
                                    text: session.room.currentSong.artist || qsTr("一起听")
                                    color: Theme.secondaryText
                                    font: Theme.font(13, 700)
                                    elide: Text.ElideRight
                                }
                                Label {
                                    Layout.fillWidth: true
                                    Layout.topMargin: 13
                                    visible: session.viewportWidth > 600
                                    text: session.room.owner ? qsTr("你可以控制房间播放，操作会同步到其他设备和成员。") : qsTr("播放由房主控制；可仅在本机暂停，再次播放会追上房间进度。")
                                    wrapMode: Text.Wrap
                                    font: Theme.font(10, 650)
                                    color: Theme.secondaryText
                                }
                            }
                        }
                    }
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        radius: Theme.popoverRadius
                        color: Theme.card
                        border.width: 1
                        border.color: Theme.border
                        ColumnLayout {
                            anchors.fill: parent
                            spacing: 0
                            RowLayout {
                                Layout.fillWidth: true
                                Layout.margins: 16
                                spacing: 8
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 4
                                    Label {
                                        Layout.fillWidth: true
                                        text: qsTr("房间歌单")
                                        font: Theme.font(10, 800)
                                        color: Theme.secondaryText
                                    }
                                    Label {
                                        Layout.fillWidth: true
                                        text: qsTr("%1 首歌曲").arg(session.room.songs.count)
                                        font: Theme.font(14, 900)
                                        color: Theme.text
                                    }
                                }
                                EchoButton {
                                    objectName: "listenOrdersAction"
                                    visible: session.room.owner
                                    accent: false
                                    text: qsTr("点播列表%1").arg(session.room.orders.count ? " " + session.room.orders.count : "")
                                    onClicked: session.showOrders()
                                }
                                EchoButton {
                                    objectName: "listenPickSongAction"
                                    text: session.room.owner ? qsTr("添加歌曲") : qsTr("点歌")
                                    glyph: "plus"
                                    onClicked: session.pickSong()
                                }
                                EchoButton {
                                    objectName: "listenSongsRefresh"
                                    accent: false
                                    implicitWidth: implicitHeight
                                    leftPadding: 0
                                    rightPadding: 0
                                    glyph: "refresh"
                                    Accessible.name: qsTr("刷新房间歌单")
                                    enabled: !session.room.loadingSongs
                                    onClicked: session.room.refreshSongs()
                                }
                            }
                            ListView {
                                id: songList
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                clip: true
                                model: session.room.songs
                                EchoListInsertMotion {
                                    view: songList
                                    active: session.resourcesActive
                                    rowHeight: 62
                                }
                                spacing: 2
                                cacheBuffer: 128
                                delegate: Item {
                                    id: songDelegate
                                    required property var rowData
                                    required property int index
                                    width: songList.width
                                    height: 62
                                    Button {
                                        id: songRow
                                        readonly property bool current: session.room.currentSong.hash === rowData.hash
                                        objectName: "listenRoomSongButton"
                                        property var rowData: songDelegate.rowData
                                        property int index: songDelegate.index
                                        anchors.fill: parent
                                        anchors.leftMargin: 7
                                        anchors.rightMargin: 7
                                        padding: 8
                                        hoverEnabled: session.room.owner
                                        enabled: session.room.owner
                                        background: Rectangle {
                                            radius: Theme.cardRadius
                                            color: songRow.current ? Theme.withAlpha(Theme.accent, 0.12) : songRow.hovered ? Theme.colors.hover : "transparent"
                                        }
                                        contentItem: RowLayout {
                                            spacing: 12
                                            Label {
                                                Layout.preferredWidth: 22
                                                text: songRow.current ? "▶" : String(songRow.index + 1)
                                                color: songRow.current ? Theme.accentText : Theme.secondaryText
                                                font: Theme.font(11, 700)
                                                horizontalAlignment: Text.AlignHCenter
                                            }
                                            EchoCover {
                                                Layout.preferredWidth: 42
                                                Layout.preferredHeight: 42
                                                source: songRow.rowData.cover_url || ""
                                                resourceActive: session.resourcesActive
                                            }
                                            ColumnLayout {
                                                Layout.fillWidth: true
                                                spacing: 4
                                                Label {
                                                    Layout.fillWidth: true
                                                    text: songRow.rowData.title || qsTr("歌曲信息同步中")
                                                    font: Theme.font(12, 750)
                                                    color: songRow.current ? Theme.accentText : Theme.text
                                                    elide: Text.ElideRight
                                                }
                                                Label {
                                                    Layout.fillWidth: true
                                                    text: songRow.rowData.artist || qsTr("正在获取歌曲信息")
                                                    font: Theme.font(10, 650)
                                                    color: Theme.secondaryText
                                                    elide: Text.ElideRight
                                                }
                                            }
                                            Label {
                                                text: songRow.current ? qsTr("播放中") : songRow.hovered ? qsTr("点击播放") : ""
                                                font: Theme.font(10, 700)
                                                color: Theme.secondaryText
                                            }
                                        }
                                        onClicked: session.room.playRoomSong(String(rowData.id || ""))
                                    }
                                }
                                Label {
                                    anchors.centerIn: parent
                                    visible: session.room.songs.count === 0
                                    text: session.room.loadingSongs ? qsTr("正在获取房间歌单…") : qsTr("房间暂无歌曲")
                                    font: Theme.font(12, 600)
                                    color: Theme.secondaryText
                                }
                                ScrollBar.vertical: EchoPageScrollBar {
                                    view: songList
                                }
                            }
                        }
                    }
                }
                ColumnLayout {
                    Layout.preferredWidth: sessionScroll.narrow ? -1 : Math.max(session.viewportWidth <= 1050 ? 270 : 300, session.width * 0.34)
                    Layout.fillWidth: sessionScroll.narrow
                    Layout.minimumHeight: sessionScroll.narrow ? 584 : 0
                    Layout.fillHeight: true
                    spacing: 14
                    Rectangle {
                        objectName: "listenMembersCard"
                        Layout.fillWidth: true
                        Layout.fillHeight: false
                        Layout.minimumHeight: implicitHeight
                        Layout.maximumHeight: implicitHeight
                        implicitHeight: session.nowPlayingHeight
                        radius: Theme.popoverRadius
                        color: Theme.card
                        border.width: 1
                        border.color: Theme.border
                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 14
                            spacing: 10
                            Label {
                                text: qsTr("在线成员 · %1 人").arg(session.room.onlineCount)
                                font: Theme.font(14, 900)
                                color: Theme.text
                            }
                            ListView {
                                id: memberList
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                clip: true
                                model: session.room.members
                                delegate: RowLayout {
                                    id: member
                                    required property var rowData
                                    width: memberList.width
                                    height: 52
                                    spacing: 10
                                    EchoCover {
                                        Layout.preferredWidth: 34
                                        Layout.preferredHeight: 34
                                        circular: true
                                        placeholderGlyph: "user"
                                        source: member.rowData.avatar_url || ""
                                        resourceActive: session.resourcesActive
                                    }
                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 3
                                        Label {
                                            Layout.fillWidth: true
                                            text: member.rowData.name || qsTr("听众")
                                            font: Theme.font(12, 750)
                                            color: Theme.text
                                            elide: Text.ElideRight
                                        }
                                        Label {
                                            text: qsTr("在线")
                                            font: Theme.font(10, 600)
                                            color: Theme.secondaryText
                                        }
                                    }
                                    Label {
                                        text: "●"
                                        color: "#22c55e"
                                        font: Theme.font(8, 400)
                                    }
                                }
                                footer: Item {
                                    width: memberList.width
                                    height: session.room.onlineCount > memberList.count ? 52 : 0
                                    visible: height > 0
                                    RowLayout {
                                        anchors.fill: parent
                                        spacing: 10
                                        EchoIcon {
                                            Layout.preferredWidth: 34
                                            Layout.preferredHeight: 34
                                            name: "users"
                                            color: Theme.secondaryText
                                        }
                                        ColumnLayout {
                                            Layout.fillWidth: true
                                            spacing: 3
                                            Label {
                                                Layout.fillWidth: true
                                                text: qsTr("另有 %1 位听众").arg(Math.max(0, session.room.onlineCount - memberList.count))
                                                font: Theme.font(12, 750)
                                                color: Theme.text
                                                elide: Text.ElideRight
                                            }
                                            Label {
                                                text: qsTr("在线身份未公开")
                                                font: Theme.font(10, 600)
                                                color: Theme.secondaryText
                                            }
                                        }
                                    }
                                }
                                ScrollBar.vertical: EchoPageScrollBar {
                                    view: memberList
                                }
                            }
                        }
                    }
                    ListenChatPanel {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.minimumHeight: sessionScroll.narrow ? 360 : 160
                        room: session.room
                        resourcesActive: session.resourcesActive
                    }
                }
            }
        }
    }
}
