pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Window
import EchoMusic.Controls
import EchoMusic.Theme

Rectangle {
    id: root
    property ThemeColors colors: Theme.colors
    property bool busy: false
    readonly property bool reducedMotion: !Theme.store.motionEnabled
    readonly property bool animating: busy && visible && width > 0 && height > 0 && Window.window !== null && Window.window.visible && Window.window.visibility !== Window.Minimized
    property real phase: 0
    property real pulse: 0.35
    objectName: "progressBusyOverlay"
    visible: busy
    radius: 2
    clip: true
    color: Theme.withAlpha(colors.text, 0.12)
    EchoDashedFrame {
        anchors.fill: parent
        solid: true
        strokeColor: Theme.withAlpha(colors.accent, 0.18)
        radius: 2
    }
    // Keep the busy sweep inside the played range.
    Rectangle {
        id: wave
        objectName: "progressBusyWave"
        x: root.reducedMotion ? 0 : (-0.42 + 3.4 * 0.46 * root.phase) * root.width
        width: root.width * (root.reducedMotion ? 1 : 0.46)
        height: root.height
        radius: root.radius
        opacity: root.reducedMotion ? root.pulse : 1
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop {
                position: 0
                color: "transparent"
            }
            GradientStop {
                position: 0.36
                color: root.colors.accent
            }
            GradientStop {
                position: 0.5
                color: Qt.tint("white", Theme.withAlpha(root.colors.accent, 0.36))
            }
            GradientStop {
                position: 0.64
                color: root.colors.accent
            }
            GradientStop {
                position: 1
                color: "transparent"
            }
        }
        EchoSurfaceShadow {
            anchors.fill: parent
            surface: wave
            radius: wave.radius
            blur: 5
            color: Theme.withAlpha(root.colors.accent, 0.72)
            z: -1
        }
    }
    NumberAnimation on phase {
        running: root.animating && !root.reducedMotion
        from: 0
        to: 1
        duration: 1100
        loops: Animation.Infinite
        easing.type: Easing.BezierSpline
        easing.bezierCurve: [0.42, 0, 0.58, 1, 1, 1]
    }
    SequentialAnimation on pulse {
        running: root.animating && root.reducedMotion
        loops: Animation.Infinite
        NumberAnimation {
            from: 0.35
            to: 0.85
            duration: 1200
            easing.type: Easing.BezierSpline
            easing.bezierCurve: [0.42, 0, 0.58, 1, 1, 1]
        }
        NumberAnimation {
            from: 0.85
            to: 0.35
            duration: 1200
            easing.type: Easing.BezierSpline
            easing.bezierCurve: [0.42, 0, 0.58, 1, 1, 1]
        }
    }
}
