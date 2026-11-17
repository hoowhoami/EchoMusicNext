pragma ComponentBehavior: Bound
import QtQuick
import EchoMusic.Controls
import EchoMusic.Theme

// Always-filled neutral locate action with shared pointer and keyboard states.
EchoIconButton {
    id: button
    implicitWidth: 36
    implicitHeight: 36
    padding: 9
    glyph: "current-location"
    text: qsTr("定位当前播放")
    tooltipText: text
    foreground: colors.text
    background: EchoActionBackground {
        objectName: "songLocateBackground"
        colors: button.colors
        pressed: button.down
        highlighted: button.enabled && (button.hovered || button.visualFocus)
        EchoDashedFrame {
            anchors.fill: parent
            solid: true
            strokeColor: button.colors.accent
            radius: Theme.controlRadius
            thickness: 2
            visible: button.visualFocus
        }
    }
}
