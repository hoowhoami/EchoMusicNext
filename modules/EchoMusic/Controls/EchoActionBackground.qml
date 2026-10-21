pragma ComponentBehavior: Bound
import QtQuick
import EchoMusic.Theme

// Shared soft-action colors and outer shadow.
// Transparent fills must not expose the shadow inside the surface.
Rectangle {
    id: actionSurface
    property EchoUiPart publicPart: null
    function visual(name: string, fallback: var): var {
        return publicPart ? publicPart.value(name, fallback) : fallback;
    }
    property ThemeColors colors: Theme.colors
    property bool accent: false
    property bool danger: false
    property bool pressed: false
    property bool highlighted: false
    radius: visual("radius", Theme.controlRadius)
    color: visual("background", danger ? Theme.withAlpha(colors.danger, pressed ? 0.16 : highlighted ? 0.12 : 0.08) : accent ? Theme.withAlpha(colors.accent, pressed ? 0.22 : highlighted ? 0.18 : 0.12) : pressed ? colors.neutralPressed : highlighted ? colors.hover : colors.mutedSurface)
    border.width: visual("borderWidth", 0)
    border.color: visual("borderColor", "transparent")
    Behavior on color {
        ColorAnimation {
            duration: Theme.fastDuration
            easing.type: Easing.BezierSpline
            easing.bezierCurve: Theme.easeStandard
        }
    }
    EchoSurfaceShadow {
        anchors.fill: parent
        surface: actionSurface
        z: -1
        radius: actionSurface.radius
        blur: 2.4
        offset: Qt.vector2d(0, 1)
        color: Qt.rgba(0, 0, 0, 0.06)
    }
}
