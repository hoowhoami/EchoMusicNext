pragma ComponentBehavior: Bound
import QtQuick
import EchoMusic.Controls
import EchoMusic.Theme

// Profile action button with shared geometry and interaction states.
EchoIconButton {
    id: button
    property bool danger: false
    implicitWidth: 36
    implicitHeight: 36
    padding: 8
    scale: down && enabled && Theme.store.motionEnabled ? 0.98 : 1
    Behavior on scale {
        enabled: Theme.store.motionEnabled && button.visible
        NumberAnimation {
            duration: Theme.fastDuration
            easing.type: Easing.BezierSpline
            easing.bezierCurve: Theme.easeStandard
        }
    }
    foreground: danger && hovered ? Theme.colors.danger : Theme.text
    background: EchoActionBackground {
        publicPart: button.publicPart
        colors: button.colors
        pressed: button.down
        highlighted: button.enabled && (button.hovered || button.visualFocus)
        border.width: button.publicPart.value("borderWidth", button.visualFocus ? 2 : 0)
        border.color: button.publicPart.value("borderColor", Theme.accent)
    }
}
