pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Controls
import EchoMusic.Theme

Rectangle {
    id: root
    required property CommentsController controller
    property bool compact: false
    property string label: qsTr("发表评论")
    readonly property int characterCount: Array.from(editor.text).length
    implicitHeight: row.implicitHeight + 20
    radius: Theme.cardRadius
    color: Theme.elevated
    border.width: 1
    border.color: editor.activeFocus ? Theme.withAlpha(Theme.accent, .55) : Theme.border
    RowLayout {
        id: row
        anchors.fill: parent
        anchors.margins: 10
        anchors.leftMargin: 12
        anchors.rightMargin: 12
        spacing: 12
        TextArea {
            id: editor
            objectName: "commentDraft"
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignBottom
            Layout.preferredHeight: Math.min(112, Math.max(32, contentHeight + 12))
            font: Theme.font(13, Theme.regularWeight)
            color: Theme.text
            placeholderTextColor: Theme.secondaryText
            wrapMode: TextEdit.Wrap
            text: root.controller.draft
            enabled: !root.controller.busy
            padding: 4
            topPadding: 6
            bottomPadding: 6
            background: null
            placeholderText: root.controller.signedIn ? qsTr("写下你的评论…") : qsTr("登录后即可发送")
            Accessible.name: root.label
            Accessible.description: qsTr("Enter 发送，Shift + Enter 换行")
            CommentTextStyle {
                document: editor.textDocument
                lineHeight: 20
            }
            onTextChanged: root.controller.draft = text
            Keys.onReturnPressed: event => {
                if (inputMethodComposing || event.modifiers !== Qt.NoModifier)
                    event.accepted = false;
                else if (!event.isAutoRepeat) {
                    event.accepted = true;
                    root.controller.send();
                }
            }
            Keys.onEnterPressed: event => {
                if (inputMethodComposing || event.modifiers !== Qt.NoModifier)
                    event.accepted = false;
                else if (!event.isAutoRepeat) {
                    event.accepted = true;
                    root.controller.send();
                }
            }
        }
        Label {
            Layout.alignment: Qt.AlignBottom
            text: root.characterCount + " / 200"
            font: Theme.font(12, Theme.regularWeight)
            lineHeightMode: Text.FixedHeight
            lineHeight: 18
            color: root.characterCount > 200 ? Theme.colors.dangerText : Theme.secondaryText
        }
        EchoButton {
            objectName: "commentSend"
            Layout.alignment: Qt.AlignBottom
            Layout.minimumWidth: 60
            text: root.controller.busy ? qsTr("发送中…") : qsTr("发送")
            enabled: root.controller.signedIn && !root.controller.busy && editor.text.trim().length > 0 && root.characterCount <= 200
            onClicked: root.controller.send()
        }
    }
}
