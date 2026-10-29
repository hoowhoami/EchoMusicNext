pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Templates as T
import EchoMusic.Account
import EchoMusic.Controls
import EchoMusic.Theme

ColumnLayout {
    id: panel
    required property ProfileService profile
    required property T.Popup surface
    spacing: 0
    Item {
        Layout.fillWidth: true
        Layout.fillHeight: true
        ListView {
            id: messages
            objectName: "profileChatList"
            anchors.fill: parent
            clip: true
            topMargin: 4
            bottomMargin: 12
            spacing: 12
            cacheBuffer: 80
            reuseItems: true
            EchoViewPoolGuard {
                targetView: messages
            }
            boundsBehavior: Flickable.StopAtBounds
            model: panel.profile.chatMessages
            onCountChanged: positionViewAtEnd()
            ScrollBar.vertical: EchoPopupScrollBar {
                surface: panel.surface
                view: messages
                resourceActive: panel.visible
            }
            delegate: Item {
                id: messageRow
                required property var modelData
                width: messages.width
                height: content.implicitHeight
                RowLayout {
                    id: content
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.leftMargin: 14
                    anchors.rightMargin: 14
                    layoutDirection: messageRow.modelData.isSelf ? Qt.RightToLeft : Qt.LeftToRight
                    spacing: 9
                    AccountAvatar {
                        objectName: "profileChatAvatar"
                        Layout.alignment: Qt.AlignTop
                        Layout.preferredWidth: 30
                        Layout.preferredHeight: 30
                        source: String(messageRow.modelData.avatar || "")
                        resourceActive: panel.visible && messageRow.y + messageRow.height > messages.contentY - messages.cacheBuffer && messageRow.y < messages.contentY + messages.height + messages.cacheBuffer
                        colors: Theme.floating
                    }
                    ColumnLayout {
                        Layout.maximumWidth: messages.width * .76
                        Layout.minimumWidth: 0
                        spacing: 4
                        RowLayout {
                            objectName: "profileChatMeta"
                            Layout.alignment: messageRow.modelData.isSelf ? Qt.AlignRight : Qt.AlignLeft
                            spacing: 7
                            Label {
                                text: messageRow.modelData.nickname
                                color: Theme.floatingSecondaryText
                                font: Theme.font(10, 800)
                                elide: Text.ElideRight
                                Layout.maximumWidth: messages.width * .4
                            }
                            Label {
                                text: messageRow.modelData.time
                                color: Theme.floatingSecondaryText
                                font: Theme.font(9, 700)
                            }
                        }
                        Rectangle {
                            objectName: "profileChatBubble"
                            Layout.alignment: messageRow.modelData.isSelf ? Qt.AlignRight : Qt.AlignLeft
                            Layout.maximumWidth: messages.width * .76
                            implicitWidth: Math.min(messages.width * .76, body.implicitWidth + 22)
                            implicitHeight: body.implicitHeight + 18
                            radius: Theme.cardRadius
                            color: messageRow.modelData.isSelf ? Theme.accent : Theme.floating.mutedSurface
                            Label {
                                id: body
                                width: parent.width - 22
                                x: 11
                                y: 9
                                text: messageRow.modelData.text
                                textFormat: Text.PlainText
                                wrapMode: Text.Wrap
                                font: Theme.font(12, 650)
                                lineHeight: 1.55
                                color: messageRow.modelData.isSelf ? Theme.accentForeground : Theme.floatingText
                            }
                        }
                    }
                    Item {
                        Layout.fillWidth: true
                    }
                }
            }
        }
        ColumnLayout {
            anchors.centerIn: parent
            width: Math.min(parent.width - 48, 320)
            spacing: 10
            visible: panel.profile.chatLoading || panel.profile.chatError.length > 0 || messages.count === 0
            Label {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                text: panel.profile.chatLoading ? qsTr("正在获取私信记录") : panel.profile.chatError || qsTr("暂无私信记录")
                color: Theme.floatingSecondaryText
                font: Theme.font(12, Theme.boldWeight)
            }
            EchoSoftButton {
                Layout.alignment: Qt.AlignHCenter
                visible: panel.profile.chatError.length > 0 && !panel.profile.chatLoading
                text: qsTr("重试")
                colors: Theme.floating
                onClicked: panel.profile.refreshChat()
            }
        }
    }
    Rectangle {
        Layout.fillWidth: true
        Layout.margins: 12
        Layout.topMargin: 10
        implicitHeight: composer.implicitHeight + 20
        radius: Theme.cardRadius
        color: Theme.floating.elevated
        border.color: draft.activeFocus ? Theme.withAlpha(Theme.accent, .55) : Theme.floatingBorder
        RowLayout {
            id: composer
            anchors.fill: parent
            anchors.margins: 10
            anchors.leftMargin: 12
            anchors.rightMargin: 12
            spacing: 12
            TextArea {
                id: draft
                objectName: "profileChatDraft"
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                Layout.preferredHeight: Math.max(32, Math.min(112, contentHeight + 12))
                padding: 6
                leftPadding: 4
                rightPadding: 4
                wrapMode: TextEdit.Wrap
                selectByMouse: true
                placeholderText: qsTr("发送一条私信")
                Accessible.name: placeholderText
                color: Theme.floatingText
                placeholderTextColor: Theme.floatingSecondaryText
                selectionColor: Theme.accent
                font: Theme.font(13, Theme.mediumWeight)
                enabled: !panel.profile.chatSending
                background: null
                onTextChanged: if (!inputMethodComposing && text.length > 200)
                    remove(200, text.length)
                Keys.onPressed: event => {
                    if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && event.modifiers === Qt.NoModifier && !inputMethodComposing) {
                        if (text.trim().length)
                            panel.profile.sendChat(text);
                        event.accepted = true;
                    } else
                        event.accepted = false;
                }
                Connections {
                    target: panel.profile
                    function onChatSent(submitted: string): void {
                        if (draft.text.trim() === submitted)
                            draft.clear();
                    }
                }
            }
            EchoTextTagRow {
                Layout.alignment: Qt.AlignBottom
                lineHeight: 36
                text: draft.text.length + " / 200"
                color: Theme.floatingSecondaryText
                font: Theme.font(12, Theme.mediumWeight)
            }
            EchoButton {
                objectName: "profileChatSend"
                Layout.alignment: Qt.AlignBottom
                text: panel.profile.chatSending ? qsTr("发送中…") : qsTr("发送")
                loading: panel.profile.chatSending
                enabled: !panel.profile.chatSending && draft.text.trim().length > 0 && draft.text.length <= 200
                accent: true
                colors: Theme.floating
                onClicked: panel.profile.sendChat(draft.text)
            }
        }
    }
}
