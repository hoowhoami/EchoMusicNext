pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import EchoMusic.Controls
import EchoMusic.Theme

// Left-aligned option label with a reserved trailing check.
// Selection changes color and opacity without changing intrinsic geometry.
EchoSoftButton {
    id: option
    property bool selected: false
    implicitHeight: 40
    leftPadding: 10
    rightPadding: 10
    font: Theme.font(12, Theme.mediumWeight)
    accent: selected
    opacity: enabled ? 1 : 0.55
    scale: 1
    Accessible.checkable: true
    Accessible.checked: selected
    contentItem: RowLayout {
        spacing: 5
        EchoTextTagRow {
            objectName: "preferenceOptionLabel"
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            text: option.text
            font: option.font
            color: option.foreground
            fillTextWidth: true
        }
        EchoIcon {
            objectName: "preferenceOptionCheck"
            Layout.preferredWidth: 13
            Layout.preferredHeight: 13
            name: "check"
            color: option.foreground
            opacity: option.selected ? 1 : 0
            resourceActive: option.selected && option.visible
        }
    }
    background: Rectangle {
        id: surface
        radius: Theme.itemRadius
        color: option.selected ? Theme.withAlpha(option.colors.accent, 0.12) : option.enabled && option.hovered ? option.colors.hover : option.colors.mutedSurface
        EchoDashedFrame {
            anchors.fill: parent
            solid: true
            strokeColor: option.selected ? Theme.withAlpha(option.colors.accent, 0.5) : option.colors.controlBorder
            radius: Theme.itemRadius
        }
        EchoDashedFrame {
            anchors.fill: parent
            anchors.margins: -5
            radius: surface.radius + 5
            visible: option.visualFocus
            solid: true
            strokeColor: option.colors.accent
            thickness: 2
        }
    }
}
