pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Controls
import EchoMusic.Theme

EchoInputBackground {
    id: root
    required property CommentsController controller
    property bool compact: false
    property string label: qsTr("发表评论")
    readonly property int characterCount: Array.from(editor.text).length
    implicitHeight: row.implicitHeight + 20
    radius: Theme.cardRadius
    baseColor: Theme.elevated
    focused: editor.activeFocus
    EchoDashedFrame {
        anchors.fill: parent
        solid: true
        strokeColor: Theme.border
        radius: Theme.cardRadius
    }
    RowLayout {
        id: row
        anchors.fill: parent
        anchors.margins: 10
        anchors.leftMargin: 12
        anchors.rightMargin: 12
        spacing: 12
        EchoTextArea {
            id: editor
            EchoInputFocus.boundary: root
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
            leftPadding: 4
            rightPadding: 4
            topPadding: 6
            bottomPadding: 6
            background: null
            placeholderText: root.controller.signedIn ? qsTr("写下你的评论…") : qsTr("登录后即可发送")
            placeholderBaseline: paragraphStyle.firstLineBaseline
            Accessible.name: root.label
            Accessible.description: qsTr("Enter 发送，Shift + Enter 换行")
            CommentTextStyle {
                id: paragraphStyle
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
        EchoSendButton {
            objectName: "commentSend"
            Layout.alignment: Qt.AlignBottom
            actionLabel: qsTr("发送评论")
            loading: root.controller.busy
            cooldownSeconds: root.controller.sendCooldownSeconds
            enabled: root.controller.signedIn && !root.controller.busy && cooldownSeconds === 0 && editor.text.trim().length > 0 && root.characterCount <= 200
            onClicked: root.controller.send()
        }
    }
}
