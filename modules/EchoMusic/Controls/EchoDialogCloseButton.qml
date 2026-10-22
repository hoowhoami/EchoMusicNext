pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as Templates
import EchoMusic.Theme

// Dialog header layout owns placement; share the 32px target and 14px icon.
EchoIconButton {
    required property Templates.Popup surface
    implicitWidth: 32
    implicitHeight: 32
    padding: 9
    glyph: "x"
    text: qsTr("关闭")
    tooltipText: ""
    colors: Theme.floating
}
