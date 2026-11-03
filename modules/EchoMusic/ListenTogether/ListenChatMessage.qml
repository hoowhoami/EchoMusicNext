pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import EchoMusic.Controls
import EchoMusic.Theme

Item {
    id: message
    objectName: "listenChatMessage"
    required property var rowData
    required property string currentUserId
    property bool resourcesActive: true
    readonly property bool system: rowData.kind_code === 4001 || rowData.kind_code === 4002
    readonly property bool self: !system && currentUserId.length > 0 && String(rowData.author?.id || "") === currentUserId
    readonly property real bodyWidth: Math.max(0, width - 22 - 27 - 8)
    implicitHeight: system ? systemBubble.implicitHeight : content.implicitHeight
    height: implicitHeight
    RowLayout {
        id: content
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: 11
        anchors.rightMargin: 11
        visible: !message.system
        layoutDirection: message.self ? Qt.RightToLeft : Qt.LeftToRight
        spacing: 8
        EchoCover {
            objectName: "listenChatAvatar"
            Layout.preferredWidth: 27
            Layout.preferredHeight: 27
            Layout.alignment: Qt.AlignTop
            circular: true
            placeholderGlyph: "user"
            source: message.rowData.author?.avatar_url || ""
            resourceActive: message.resourcesActive && visible
        }
        ColumnLayout {
            Layout.maximumWidth: message.bodyWidth
            Layout.minimumWidth: 0
            spacing: 3
            RowLayout {
                objectName: "listenChatMeta"
                Layout.alignment: message.self ? Qt.AlignRight : Qt.AlignLeft
                Layout.fillWidth: false
                Layout.maximumWidth: message.bodyWidth
                spacing: 7
                EchoTextTagRow {
                    objectName: "listenChatName"
                    Layout.fillWidth: false
                    Layout.maximumWidth: Math.max(0, message.bodyWidth - timestamp.implicitWidth - 7)
                    text: message.rowData.author?.name || qsTr("听众")
                    font: Theme.font(9, 850)
                    lineHeight: font.pixelSize * 1.5
                    color: Theme.text
                }
                EchoTextTagRow {
                    id: timestamp
                    objectName: "listenChatTime"
                    Layout.fillWidth: false
                    text: message.rowData.sent_at_seconds > 0 ? Qt.formatDateTime(new Date(message.rowData.sent_at_seconds * 1000), "hh:mm") : ""
                    font: Theme.font(8, 650)
                    lineHeight: font.pixelSize * 1.5
                    color: Theme.secondaryText
                }
            }
            Rectangle {
                objectName: "listenChatBubble"
                Layout.alignment: message.self ? Qt.AlignRight : Qt.AlignLeft
                Layout.maximumWidth: message.bodyWidth
                implicitWidth: Math.min(message.bodyWidth, body.implicitWidth + 18)
                implicitHeight: body.implicitHeight + 14
                radius: Theme.cardRadius
                color: message.self ? Theme.accent : Theme.mutedSurface
                EchoTextTagRow {
                    id: body
                    objectName: "listenChatBody"
                    x: 9
                    y: 7
                    width: parent.width - 18
                    text: String(message.rowData.text || "")
                    wrap: true
                    font: Theme.font(10, 650)
                    lineHeight: font.pixelSize * 1.55
                    color: message.self ? Theme.accentForeground : Theme.text
                }
            }
        }
        Item {
            Layout.fillWidth: true
        }
    }
    Rectangle {
        id: systemBubble
        objectName: "listenChatSystemBubble"
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.alignWhenCentered: false
        visible: message.system
        implicitWidth: Math.min(Math.max(0, message.width - 22), systemText.implicitWidth + 16)
        implicitHeight: systemText.implicitHeight + 8
        radius: height / 2
        color: Theme.mutedSurface
        EchoTextTagRow {
            id: systemText
            objectName: "listenChatSystemText"
            x: 8
            y: 4
            width: parent.width - 16
            text: String(message.rowData.author?.name || "") + " " + String(message.rowData.text || "")
            wrap: true
            horizontalAlignment: Text.AlignHCenter
            font: Theme.font(9, 650)
            lineHeight: font.pixelSize * 1.5
            color: Theme.secondaryText
        }
    }
}
