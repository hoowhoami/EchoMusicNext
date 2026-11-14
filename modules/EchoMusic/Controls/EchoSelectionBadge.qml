pragma ComponentBehavior: Bound
import QtQuick
import EchoMusic.Controls
import EchoMusic.Theme

// Shared floating selection surface with a subtle accent.
Rectangle {
    id: badge
    implicitWidth: 20
    implicitHeight: 20
    radius: Theme.detailRadius
    color: Theme.elevated
    Rectangle {
        anchors.fill: parent
        radius: badge.radius
        color: Theme.selected
        EchoDashedFrame {
            anchors.fill: parent
            solid: true
            strokeColor: Qt.rgba(Theme.accent.r * 0.35 + Theme.border.r * 0.65, Theme.accent.g * 0.35 + Theme.border.g * 0.65, Theme.accent.b * 0.35 + Theme.border.b * 0.65, 1)
            radius: badge.radius
        }
    }
    EchoIcon {
        objectName: "selectionCheck"
        anchors.centerIn: parent
        width: 14
        height: 14
        name: "check"
        color: Theme.accentText
    }
}
