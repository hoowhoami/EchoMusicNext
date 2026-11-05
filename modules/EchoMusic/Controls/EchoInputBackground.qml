pragma ComponentBehavior: Bound
import QtQuick
import EchoMusic.Theme

Rectangle {
    id: surface
    EchoMotionScope {
        id: motion
        target: surface
        animations: [fill, ring]
    }
    property ThemeColors colors: Theme.colors
    property bool focused: false
    // Floating composers need an opaque base to occlude scrolled content.
    property color baseColor: "transparent"
    radius: Theme.controlRadius
    color: Theme.mix(baseColor, colors.tone, focused ? 0.07 : 0.05)
    Behavior on color {
        enabled: motion.allowed
        ColorAnimation {
            id: fill
            duration: Theme.fastDuration
            easing.type: Easing.BezierSpline
            easing.bezierCurve: Theme.easeStandard
        }
    }
    Behavior on border.color {
        enabled: motion.allowed
        ColorAnimation {
            id: ring
            duration: Theme.fastDuration
            easing.type: Easing.BezierSpline
            easing.bezierCurve: Theme.easeStandard
        }
    }
}
