pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Controls
import EchoMusic.Theme
import EchoMusic.Playback

Item {
    id: dialogs
    signal minimizeRequested
    required property Item host
    property real uiScale: 1
    required property ListenTogetherUi service
    readonly property ListenTogetherController room: service.room
    function createRoom(): void {
        if (room.signedIn)
            createOwner.open();
        else
            room.loadRooms();
    }
    function manageRoom(): void {
        manageOwner.open({
            targetRoomId: room.roomId,
            targetRoomName: String(room.activeRoom.name || qsTr("一起听房间")),
            owned: room.owner
        });
    }
    function pickSong(): void {
        pickerOwner.open();
    }
    function showOrders(): void {
        room.refreshOrders();
        orderOwner.open();
    }
    function closeAll(): void {
        previewOwner.close();
        createOwner.close();
        manageOwner.close();
        pickerOwner.close();
        orderOwner.close();
        room.closePreview();
    }
    Connections {
        target: dialogs.room
        function onPreviewRequested(): void {
            previewOwner.open();
        }
        function onPreviewClosed(): void {
            previewOwner.close();
        }
        function onSessionReady(): void {
            createOwner.close();
            previewOwner.close();
        }
        function onChanged(): void {
            if (!dialogs.room.joined) {
                manageOwner.close();
                pickerOwner.close();
                orderOwner.close();
            }
            if (!dialogs.room.signedIn)
                dialogs.closeAll();
        }
    }
    EchoPopupLoader {
        id: previewOwner
        objectName: "listenPreviewOwner"
        hostItem: dialogs.host
        sourceComponent: Component {
            EchoDialog {
                id: preview
                uiScale: dialogs.uiScale
                objectName: "listenRoomPreviewDialog"
                preferredWidth: 540
                heading: dialogs.room.previewRoom.name || qsTr("房间详情")
                body: Component {
                    ColumnLayout {
                        spacing: 16
                        Rectangle {
                            Layout.fillWidth: true
                            implicitHeight: 150
                            radius: Theme.dialogRadius
                            gradient: Gradient {
                                orientation: Gradient.Horizontal
                                GradientStop {
                                    position: 0
                                    color: Theme.mix("#14151c", Theme.accent, 0.24)
                                }
                                GradientStop {
                                    position: 1
                                    color: "#151821"
                                }
                            }
                            Label {
                                x: 18
                                y: 18
                                text: qsTr("● LIVE · 众乐房")
                                color: "white"
                                font: Theme.font(11, 800)
                            }
                            EchoIcon {
                                anchors.centerIn: parent
                                width: 56
                                height: 56
                                name: "headphones"
                                color: "#deffffff"
                            }
                        }
                        RowLayout {
                            spacing: 12
                            EchoCover {
                                Layout.preferredWidth: 42
                                Layout.preferredHeight: 42
                                circular: true
                                placeholderGlyph: "user"
                                source: dialogs.room.previewRoom.owner?.avatar_url || ""
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 5
                                Label {
                                    text: dialogs.room.previewRoom.owner?.name || qsTr("房主")
                                    font: Theme.font(14, 800)
                                    color: Theme.floatingText
                                }
                                Label {
                                    text: qsTr("房主 · %1 人在线").arg(dialogs.room.previewRoom.member_count ?? dialogs.room.previewMembers.count)
                                    font: Theme.font(11, 600)
                                    color: Theme.floatingSecondaryText
                                }
                            }
                        }
                        Row {
                            spacing: 6
                            Repeater {
                                model: Math.min(8, dialogs.room.previewMembers.count)
                                EchoCover {
                                    required property int index
                                    width: 30
                                    height: 30
                                    circular: true
                                    placeholderGlyph: "user"
                                    source: dialogs.room.previewMembers.item(index).avatar_url || ""
                                }
                            }
                        }
                    }
                }
                footerContent: Component {
                    RowLayout {
                        spacing: 8
                        EchoButton {
                            accent: false
                            text: qsTr("取消")
                            onClicked: preview.close()
                        }
                        Item {
                            Layout.fillWidth: true
                        }
                        EchoButton {
                            objectName: "listenPreviewShareAction"
                            visible: dialogs.service.playback.sharing !== null
                            accent: false
                            text: qsTr("分享")
                            glyph: "share"
                            onClicked: dialogs.service.playback.sharing.shareRoom(String(dialogs.room.previewRoom.id || ""), String(dialogs.room.previewRoom.name || ""))
                        }
                        EchoButton {
                            visible: dialogs.room.roomScope === 1
                            accent: false
                            objectName: "listenDismissSubmit"
                            text: qsTr("解散房间")
                            onClicked: {
                                manageOwner.open({
                                    targetRoomId: String(dialogs.room.previewRoom.id || ""),
                                    targetRoomName: String(dialogs.room.previewRoom.name || qsTr("这个房间")),
                                    owned: true
                                });
                                preview.close();
                            }
                        }
                        EchoButton {
                            objectName: "listenJoinAction"
                            text: qsTr("加入一起听")
                            glyph: "headphones"
                            loading: dialogs.room.phase === ListenTogetherController.Joining
                            onClicked: dialogs.room.joinRoom(String(dialogs.room.previewRoom.id || ""))
                        }
                    }
                }
                onClosed: dialogs.room.closePreview()
            }
        }
    }
    EchoPopupLoader {
        id: createOwner
        objectName: "listenCreateOwner"
        hostItem: dialogs.host
        sourceComponent: Component {
            EchoDialog {
                id: create
                uiScale: dialogs.uiScale
                objectName: "listenRoomCreateDialog"
                preferredWidth: 620
                heading: qsTr("创建众乐房")
                description: qsTr("设置房间名称与权限，创建时会沿用当前播放器队列。")
                property int privacy: 1
                readonly property int seedCount: {
                    const count = dialogs.service.playback.queue.count;
                    const track = dialogs.service.playback.title;
                    return dialogs.service.creationSongCount();
                }
                property string roomName: dialogs.room.nickname + qsTr("的众乐房")
                closable: dialogs.room.phase !== ListenTogetherController.Creating
                closePolicy: closable ? Popup.CloseOnEscape | Popup.CloseOnPressOutside : Popup.NoAutoClose
                body: Component {
                    ColumnLayout {
                        spacing: 18
                        Label {
                            text: qsTr("房间名称")
                            color: Theme.floatingSecondaryText
                            font: Theme.font(12, 750)
                        }
                        EchoTextField {
                            objectName: "listenCreateName"
                            Layout.fillWidth: true
                            colors: Theme.floating
                            placeholderText: qsTr("给房间起个名字")
                            text: create.roomName
                            onTextEdited: create.roomName = text
                        }
                        Label {
                            text: qsTr("房间权限")
                            color: Theme.floatingSecondaryText
                            font: Theme.font(12, 750)
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10
                            Repeater {
                                model: [
                                    {
                                        name: qsTr("公开房间"),
                                        note: qsTr("可在广场发现；账号需具备公开创房权限")
                                    },
                                    {
                                        name: qsTr("私密房间"),
                                        note: qsTr("仅限受邀成员，按 APP 默认最多 5 人")
                                    }
                                ]
                                EchoActionButton {
                                    id: privacyOption
                                    objectName: "listenPrivacyOption" + index
                                    required property var modelData
                                    required property int index
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 88
                                    padding: 14
                                    hoverEnabled: true
                                    background: Rectangle {
                                        radius: Theme.controlRadius
                                        color: create.privacy === privacyOption.index + 1 ? Theme.withAlpha(Theme.accent, 0.09) : Theme.floating.mutedSurface
                                        border.width: 1
                                        border.color: create.privacy === privacyOption.index + 1 ? Theme.withAlpha(Theme.accent, 0.44) : Theme.floatingBorder
                                    }
                                    contentItem: ColumnLayout {
                                        spacing: 6
                                        Label {
                                            text: privacyOption.modelData.name
                                            font: Theme.font(14, 900)
                                            color: Theme.floatingText
                                        }
                                        Label {
                                            Layout.fillWidth: true
                                            text: privacyOption.modelData.note
                                            font: Theme.font(10, 650)
                                            color: Theme.floatingSecondaryText
                                            wrapMode: Text.Wrap
                                        }
                                    }
                                    onClicked: create.privacy = index + 1
                                }
                            }
                        }
                        Label {
                            text: qsTr("播放队列")
                            color: Theme.floatingSecondaryText
                            font: Theme.font(12, 750)
                        }
                        EchoButtonContent {
                            Layout.fillWidth: true
                            text: create.seedCount > 0 ? qsTr("沿用当前播放队列 · %1").arg(dialogs.service.playback.title) : qsTr("当前没有可用播放队列，请先播放一首歌")
                            glyph: "music"
                            color: Theme.floatingText
                            font: Theme.font(13, 750)
                            horizontalAlignment: Qt.AlignLeft
                        }
                        Label {
                            Layout.fillWidth: true
                            text: qsTr("众乐房支持聊天与点歌，创建后可继续添加歌曲。")
                            font: Theme.font(11, 600)
                            color: Theme.floatingSecondaryText
                            wrapMode: Text.Wrap
                        }
                    }
                }
                footerContent: Component {
                    RowLayout {
                        Item {
                            Layout.fillWidth: true
                        }
                        EchoButton {
                            accent: false
                            text: qsTr("取消")
                            enabled: create.closable
                            onClicked: create.close()
                        }
                        EchoButton {
                            objectName: "listenCreateSubmit"
                            text: qsTr("创建并进入")
                            enabled: create.roomName.trim().length > 0 && create.seedCount > 0
                            loading: dialogs.room.phase === ListenTogetherController.Creating
                            onClicked: dialogs.room.createRoom(create.roomName, create.privacy)
                        }
                    }
                }
            }
        }
    }
    EchoPopupLoader {
        id: manageOwner
        objectName: "listenManageOwner"
        hostItem: dialogs.host
        sourceComponent: Component {
            EchoDialog {
                id: manage
                uiScale: dialogs.uiScale
                objectName: "listenRoomManageDialog"
                property string targetRoomId: ""
                property string targetRoomName: ""
                property bool owned: false
                readonly property bool currentRoom: targetRoomId === dialogs.room.roomId
                heading: owned ? currentRoom ? qsTr("房间管理") : qsTr("解散房间") : qsTr("离开房间")
                body: Component {
                    Label {
                        Layout.fillWidth: true
                        textFormat: Text.PlainText
                        text: manage.owned ? manage.currentRoom ? qsTr("返回列表后房间会继续在后台保持同步。只有结束一起听才会解散房间。") : qsTr("确定解散「%1」吗？解散后无法恢复。").arg(manage.targetRoomName || qsTr("这个房间")) : qsTr("确定离开当前房间吗？离开后会恢复之前的播放队列。")
                        wrapMode: Text.Wrap
                        color: Theme.floatingText
                        font: Theme.font(14, 500)
                    }
                }
                footerContent: Component {
                    RowLayout {
                        EchoButton {
                            accent: false
                            text: qsTr("取消")
                            enabled: dialogs.room.phase !== ListenTogetherController.Leaving
                            onClicked: manage.close()
                        }
                        Item {
                            Layout.fillWidth: true
                        }
                        EchoButton {
                            visible: manage.currentRoom
                            accent: true
                            objectName: "listenLeaveSubmit"
                            text: manage.owned ? qsTr("返回房间列表") : qsTr("离开房间")
                            loading: dialogs.room.phase === ListenTogetherController.Leaving
                            onClicked: {
                                if (manage.owned) {
                                    dialogs.minimizeRequested();
                                    manage.close();
                                } else
                                    dialogs.room.leaveRoom(false);
                            }
                        }
                        EchoSoftButton {
                            visible: manage.owned
                            danger: true
                            objectName: "listenDismissSubmit"
                            text: manage.currentRoom ? qsTr("结束一起听") : qsTr("确认解散")
                            loading: dialogs.room.phase === ListenTogetherController.Leaving
                            onClicked: {
                                if (manage.targetRoomId === dialogs.room.roomId)
                                    dialogs.room.leaveRoom(true);
                                else {
                                    dialogs.room.dismissOwnedRoom(manage.targetRoomId);
                                    manage.close();
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    EchoPopupLoader {
        id: pickerOwner
        objectName: "listenPickerOwner"
        hostItem: dialogs.host
        sourceComponent: Component {
            ListenSongPickerDialog {
                uiScale: dialogs.uiScale
                service: dialogs.service
            }
        }
    }
    EchoPopupLoader {
        id: orderOwner
        objectName: "listenOrdersOwner"
        hostItem: dialogs.host
        sourceComponent: Component {
            EchoDialog {
                id: orders
                uiScale: dialogs.uiScale
                objectName: "listenSongOrdersDialog"
                heading: qsTr("点播列表")
                headingCount: dialogs.room.orders.count
                preferredWidth: 620
                body: Component {
                    ListView {
                        id: orderList
                        implicitHeight: Math.min(360, Math.max(90, contentHeight))
                        clip: true
                        model: dialogs.room.orders
                        delegate: RowLayout {
                            id: orderRow
                            required property var rowData
                            width: orderList.width
                            height: 68
                            spacing: 10
                            EchoCover {
                                Layout.preferredWidth: 42
                                Layout.preferredHeight: 42
                                source: orderRow.rowData.catalog?.cover_url || ""
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 5
                                Label {
                                    Layout.fillWidth: true
                                    text: orderRow.rowData.catalog?.title || qsTr("歌曲信息同步中")
                                    color: Theme.floatingText
                                    font: Theme.font(12, 750)
                                    elide: Text.ElideRight
                                }
                                Label {
                                    Layout.fillWidth: true
                                    text: qsTr("%1 点播").arg(orderRow.rowData.requester?.name || qsTr("听众"))
                                    color: Theme.floatingSecondaryText
                                    font: Theme.font(10, 600)
                                    elide: Text.ElideRight
                                }
                            }
                            EchoButton {
                                accent: false
                                text: qsTr("忽略")
                                enabled: !dialogs.room.addingSongs
                                onClicked: dialogs.room.rejectOrder(String(orderRow.rowData.key))
                            }
                            EchoButton {
                                text: qsTr("添加")
                                enabled: !dialogs.room.addingSongs
                                onClicked: dialogs.room.approveOrder(String(orderRow.rowData.key))
                            }
                        }
                        Label {
                            anchors.centerIn: parent
                            visible: orderList.count === 0
                            text: dialogs.room.loadingOrders ? qsTr("正在加载点播…") : qsTr("暂无点播歌曲")
                            font: Theme.font(12, 500)
                            color: Theme.floatingSecondaryText
                        }
                        ScrollBar.vertical: EchoPageScrollBar {
                            view: orderList
                        }
                    }
                }
            }
        }
    }
}
