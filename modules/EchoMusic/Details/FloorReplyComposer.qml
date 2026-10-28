pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Controls
import EchoMusic.Theme

Rectangle {
    id: composer
    required property CommentsController controller
    required property var target
    property bool nested: false
    objectName: "floorReplyComposer"
    implicitHeight: content.implicitHeight + 28
    radius: Theme.cardRadius
    color: nested ? Theme.elevated : Theme.mutedSurface
    border.width: 1
    border.color: Theme.subtleBorder
    ColumnLayout {
        id: content
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 14
        spacing: 0
        RowLayout {
            Layout.fillWidth: true
            EchoTextTagRow {
                Layout.fillWidth: true
                text: qsTr("回复 %1").arg(composer.target.user_name || "")
                font: Theme.font(12, Theme.regularWeight)
                color: Theme.secondaryText
            }
            EchoIconButton {
                objectName: "cancelFloorReply"
                glyph: "x"
                text: qsTr("取消回复")
                implicitWidth: 36
                implicitHeight: 36
                padding: 10
                enabled: !composer.controller.busy
                onClicked: composer.controller.closeReply()
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 8
            Layout.bottomMargin: 12
            spacing: 10
            Rectangle {
                Layout.preferredWidth: 2
                Layout.fillHeight: true
                color: Theme.border
            }
            Label {
                Layout.fillWidth: true
                text: composer.target.content || ""
                elide: Text.ElideRight
                color: Theme.secondaryText
                font: Theme.font(12, Theme.regularWeight)
            }
        }
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(180, Math.max(80, draft.contentHeight + 20))
            EchoTextArea {
                id: draft
                anchors.fill: parent
                objectName: "replyDraft"
                font: Theme.font(13, Theme.regularWeight)
                clip: true
                background: Rectangle {
                    radius: Theme.controlRadius
                    color: Theme.elevated
                    border.width: 1
                    border.color: Theme.border
                }
                CommentTextStyle {
                    document: draft.textDocument
                    lineHeight: 20.8
                }
                text: composer.controller.replyDraft
                enabled: !composer.controller.busy
                placeholderText: composer.controller.signedIn ? qsTr("写下你的回复…") : qsTr("登录后即可回复")
                Accessible.name: qsTr("回复 %1").arg(composer.target.user_name || "")
                Accessible.description: qsTr("Enter 发送，Shift + Enter 换行")
                onTextChanged: composer.controller.replyDraft = text
                Keys.onReturnPressed: event => {
                    if (inputMethodComposing || event.modifiers !== Qt.NoModifier)
                        event.accepted = false;
                    else {
                        event.accepted = true;
                        if (!event.isAutoRepeat)
                            composer.controller.submitReply();
                    }
                }
            }
            Rectangle {
                anchors.fill: parent
                anchors.margins: -3
                visible: draft.activeFocus
                radius: Theme.controlRadius + 3
                color: "transparent"
                border.width: 2
                border.color: Theme.accent
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 10
            Label {
                Layout.fillWidth: true
                text: Array.from(draft.text).length + " / 200"
                font: Theme.font(11, Theme.regularWeight)
                color: Theme.secondaryText
            }
            EchoButton {
                objectName: "replySend"
                text: composer.controller.busy ? qsTr("发送中…") : qsTr("发送回复")
                enabled: composer.controller.signedIn && !composer.controller.busy && draft.text.trim().length > 0 && Array.from(draft.text).length <= 200
                onClicked: composer.controller.submitReply()
            }
        }
    }
}
