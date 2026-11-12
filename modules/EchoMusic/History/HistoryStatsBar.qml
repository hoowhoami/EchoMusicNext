pragma ComponentBehavior: Bound
import QtQuick
import EchoMusic.Controls
import EchoMusic.Theme

// Animate a bounded scalar, so resizing the layout never starts a new clock.
Rectangle {
    id: bar
    property real ratio: 0
    property bool vertical: false
    property color tint: Theme.accent
    property bool resourceActive: true
    property bool animate: true
    property real presentedRatio: Math.max(0, Math.min(1, ratio))
    radius: Math.min(width, height) / 2
    color: Theme.withAlpha(Theme.text, .045)
    EchoMotionScope {
        id: motion
        target: bar
        active: bar.resourceActive && bar.animate
        animations: [transition]
    }
    Behavior on presentedRatio {
        enabled: motion.allowed
        NumberAnimation {
            id: transition
            objectName: "historyBarMotion"
            duration: Theme.panelDuration
            easing.type: Easing.BezierSpline
            easing.bezierCurve: Theme.easeStandard
        }
    }
    Rectangle {
        objectName: "historyBarFill"
        x: 0
        y: bar.vertical ? bar.height - height : 0
        width: bar.vertical ? bar.width : bar.width * bar.presentedRatio
        height: bar.vertical ? bar.height * bar.presentedRatio : bar.height
        radius: Math.min(width, height) / 2
        color: bar.tint
        visible: bar.presentedRatio > 0
    }
}
