pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Controls
import EchoMusic.Theme

// The directory intentionally differs from the metadata-rich search artist card.
ItemDelegate {
    id: entry
    required property string title
    required property string coverUrl
    required property string resourceId
    property bool resourceActive: true
    property bool navigationAvailable: false
    signal openRequested(string id)
    padding: 0
    implicitHeight: width + 40
    hoverEnabled: true
    Accessible.name: title
    background: null
    onClicked: if (navigationAvailable)
        openRequested(resourceId)
    contentItem: ColumnLayout {
        spacing: 12
        EchoCover {
            coverScope: "artist"
            objectName: "artistDirectoryAvatar"
            Layout.fillWidth: true
            Layout.preferredHeight: width
            source: entry.coverUrl
            circular: true
            resourceActive: entry.resourceActive
            Rectangle {
                anchors.fill: parent
                radius: width / 2
                color: "transparent"
                border.width: entry.visualFocus ? 2 : 0
                border.color: Theme.accent
            }
        }
        EchoTextTagRow {
            id: caption
            objectName: "artistDirectoryCaption"
            Layout.fillWidth: true
            Layout.preferredHeight: 28
            text: entry.title
            font: Theme.font(16, Theme.regularWeight)
            color: entry.hovered ? Theme.accentText : Theme.text
            fillTextWidth: true
            horizontalAlignment: Text.AlignHCenter
        }
    }
    EchoToolTip {
        parent: entry
        requestedVisible: entry.resourceActive && entry.hovered && caption.truncated
        text: entry.title
    }
}
