pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import EchoMusic.Theme

Switch {
    id: control
    EchoUiPart {
        id: formPart
        target: control
        role: "control.switch"
        key: control.objectName
        properties: ["foreground", "background", "accent", "borderColor", "borderWidth", "radius"]
    }
    property ThemeColors colors: Theme.colors
    property bool compact: false
    implicitWidth: compact ? 36 : 44
    implicitHeight: compact ? 20 : 24
    padding: 0
    indicator: Rectangle {
        width: control.implicitWidth
        height: control.implicitHeight
        radius: formPart.value("radius", height / 2)
        color: formPart.value("background", control.checked ? formPart.value("accent", control.colors.accent) : control.colors.mutedSurface)
        border.width: formPart.value("borderWidth", 1)
        border.color: formPart.value("borderColor", control.checked ? formPart.value("accent", control.colors.accent) : control.colors.border)
        opacity: control.enabled ? 1 : 0.6
        Rectangle {
            x: control.checked ? parent.width - width - (control.compact ? 3 : 4) : (control.compact ? 3 : 4)
            y: control.compact ? 3 : 4
            width: control.compact ? 14 : 16
            height: width
            radius: width / 2
            color: formPart.value("foreground", "white")
            Behavior on x {
                NumberAnimation {
                    duration: Theme.fastDuration
                }
            }
        }
    }
    background: Rectangle {
        color: "transparent"
        radius: formPart.value("radius", height / 2)
        border.width: formPart.value("borderWidth", control.visualFocus ? 2 : 0)
        border.color: formPart.value("borderColor", formPart.value("accent", control.colors.accent))
    }
}
