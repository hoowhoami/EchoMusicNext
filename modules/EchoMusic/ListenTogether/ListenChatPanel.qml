pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Controls
import EchoMusic.Theme
import EchoMusic.Playback

Rectangle {
    id: panel
    objectName: "listenChatPanel"
    required property ListenTogetherController room
    property bool resourcesActive: true
    readonly property int messageLimit: 200
    radius: Theme.popoverRadius
    color: Theme.card
    border.width: 1
    border.color: Theme.border
    property string sentDraft: ""
    function send(): void {
        if (!composer.text.trim() || room.sendingMessage || room.sendCooldownSeconds > 0)
            return;
        sentDraft = composer.text;
        room.sendMessage(composer.text);
    }
    Connections {
        target: panel.room
        function onMessageSent(): void {
            if (composer.text === panel.sentDraft)
                composer.clear();
        }
        function onChanged(): void {
            if (!panel.room.joined)
                composer.clear();
        }
    }
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 1
        spacing: 0
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 15
            Layout.rightMargin: 15
            Layout.topMargin: 13
            Layout.bottomMargin: 10
            spacing: 12
            ColumnLayout {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                spacing: 2
                EchoTextTagRow {
                    objectName: "listenChatKicker"
                    Layout.fillWidth: true
                    text: qsTr("房间聊天")
                    font: Theme.spacedFont(9, 900, .7)
                    color: Theme.secondaryText
                    lineHeight: font.pixelSize * 1.5
                }
                EchoTextTagRow {
                    Layout.fillWidth: true
                    text: panel.room.activeRoom.chat_code !== 1 ? qsTr("房主已关闭聊天") : qsTr("和大家打个招呼")
                    color: Theme.text
                    font: Theme.font(14, 900)
                    lineHeight: font.pixelSize * 1.5
                }
            }
            EchoButton {
                visible: panel.room.owner
                accent: false
                text: panel.room.activeRoom.chat_code !== 1 ? qsTr("开启聊天") : qsTr("关闭聊天")
                enabled: !panel.room.updatingChat
                onClicked: panel.room.setChatEnabled(panel.room.activeRoom.chat_code !== 1)
            }
        }
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 1
            color: Theme.border
        }
        ListView {
            id: chatList
            objectName: "listenChatList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: panel.room.messages
            spacing: 10
            topMargin: 10
            bottomMargin: 14
            reuseItems: true
            EchoViewPoolGuard {
                targetView: chatList
            }
            property bool nearBottom: true
            onContentYChanged: nearBottom = contentHeight - height - contentY < 64
            onCountChanged: if (nearBottom)
                Qt.callLater(() => chatList.positionViewAtEnd())
            delegate: ListenChatMessage {
                id: messageRow
                property bool pooled: false
                width: chatList.width
                currentUserId: panel.room.currentUserId
                resourcesActive: panel.resourcesActive && !messageRow.pooled && messageRow.y + messageRow.height > chatList.contentY - chatList.cacheBuffer && messageRow.y < chatList.contentY + chatList.height + chatList.cacheBuffer
                ListView.onPooled: messageRow.pooled = true
                ListView.onReused: messageRow.pooled = false
            }
            EchoTextTagRow {
                objectName: "listenChatEmpty"
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.topMargin: 10
                height: 76
                visible: chatList.count === 0
                text: qsTr("暂无房间消息")
                fillTextWidth: true
                horizontalAlignment: Text.AlignHCenter
                font: Theme.font(9, 650)
                color: Theme.secondaryText
            }
            ScrollBar.vertical: EchoPageScrollBar {
                view: chatList
            }
        }
        EchoInputBackground {
            id: composerSurface
            objectName: "listenComposerSurface"
            Layout.fillWidth: true
            Layout.leftMargin: 9
            Layout.rightMargin: 9
            Layout.bottomMargin: 9
            implicitHeight: composerRow.implicitHeight + 16
            radius: Theme.cardRadius
            focused: composer.activeFocus
            border.width: 1
            border.color: Theme.border
            opacity: panel.room.activeRoom.chat_code === 1 ? 1 : 0.55
            TapHandler {
                onTapped: composer.forceActiveFocus()
            }
            RowLayout {
                id: composerRow
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 8
                anchors.topMargin: 8
                anchors.bottomMargin: 8
                spacing: 7
                EchoTextArea {
                    id: composer
                    EchoInputFocus.boundary: composerSurface
                    objectName: "listenMessageComposer"
                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.min(72, Math.max(30, contentHeight + 14))
                    Layout.alignment: Qt.AlignBottom
                    topPadding: 7
                    bottomPadding: 7
                    leftPadding: 0
                    rightPadding: 0
                    selectByMouse: true
                    selectionColor: Theme.accent
                    selectedTextColor: Theme.text
                    clip: true
                    enabled: panel.room.activeRoom.chat_code === 1 && !panel.room.sendingMessage
                    font: Theme.font(10, 650)
                    color: Theme.text
                    placeholderTextColor: Theme.secondaryText
                    wrapMode: TextEdit.Wrap
                    placeholderText: panel.room.activeRoom.chat_code === 1 ? qsTr("输入消息，Enter 发送，Shift+Enter 换行") : qsTr("当前房间不允许聊天")
                    background: Item {}
                    onTextChanged: if (text.length > panel.messageLimit)
                        remove(panel.messageLimit, text.length)
                    Keys.onReturnPressed: event => {
                        if (!(event.modifiers & Qt.ShiftModifier)) {
                            panel.send();
                            event.accepted = true;
                        } else
                            event.accepted = false;
                    }
                    Keys.onEnterPressed: event => {
                        if (!(event.modifiers & Qt.ShiftModifier)) {
                            panel.send();
                            event.accepted = true;
                        } else
                            event.accepted = false;
                    }
                }
                EchoTextTagRow {
                    objectName: "listenChatCharacterCount"
                    Layout.alignment: Qt.AlignBottom
                    Layout.leftMargin: 5
                    Layout.rightMargin: 5
                    text: qsTr("%1 / %2").arg(composer.text.length).arg(panel.messageLimit)
                    font: Theme.font(12, Theme.regularWeight)
                    lineHeight: 18
                    color: Theme.secondaryText
                }
                EchoSendButton {
                    objectName: "listenSendMessage"
                    Layout.alignment: Qt.AlignBottom
                    actionLabel: qsTr("发送消息")
                    loading: panel.room.sendingMessage
                    cooldownSeconds: panel.room.sendCooldownSeconds
                    enabled: composer.enabled && composer.text.trim().length > 0 && cooldownSeconds === 0
                    onClicked: panel.send()
                }
            }
        }
    }
}
