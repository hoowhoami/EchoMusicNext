pragma ComponentBehavior: Bound
import QtQuick
import EchoMusic.Controls
import EchoMusic.Theme

// Desktop lyric controls use a dark translucent surface independently of the main theme.
EchoIconButton {
    id: control
    property bool unlock: false
    property bool compact: false
    implicitWidth: compact ? 30 : 32
    implicitHeight: compact ? 30 : 32
    padding: compact ? 5 : 6
    foreground: "white"
    background: Rectangle {
        radius: Theme.controlRadius
        color: control.unlock ? "#73000000" : control.hovered ? "#4dffffff" : "transparent"
        EchoDashedFrame {
            anchors.fill: parent
            solid: true
            strokeColor: "white"
            radius: Theme.controlRadius
            thickness: control.visualFocus ? 2 : 0
            visible: control.visualFocus
        }
    }
}
