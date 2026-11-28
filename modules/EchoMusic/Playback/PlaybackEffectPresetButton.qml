pragma ComponentBehavior: Bound
import QtQuick
import EchoMusic.Controls
import EchoMusic.Theme

// Shared song/EQ/library action reserves the same two-line name slot.
EchoSoftButton {
    id: button
    property real minimumHeight: 38
    colors: Theme.floating
    font: Theme.font(12, Theme.semiboldWeight)
    leftPadding: 10
    rightPadding: 10
    topPadding: 8
    bottomPadding: 8
    implicitHeight: Math.max(minimumHeight, 2 * label.lineHeight + topPadding + bottomPadding)
    EchoToolTip {
        objectName: "effectPresetToolTip"
        parent: button
        text: button.text
        requestedVisible: button.hovered && label.truncated
    }
    contentItem: Item {
        implicitWidth: label.implicitWidth
        implicitHeight: label.implicitHeight
        clip: true
        PlaybackEffectName {
            id: label
            objectName: "effectPresetLabel"
            anchors.centerIn: parent
            width: Math.min(implicitWidth, parent.width)
            text: button.text
            font: button.font
            color: button.foreground
            horizontalAlignment: Text.AlignHCenter
            wrap: true
        }
    }
}
