pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import EchoMusic.Theme

// Native Popup keeps placement/zoom and input ownership. Only its presentation
// item's extra transform moves; content and rounded background travel together.
QtObject {
    id: motion
    objectName: "popupMotion"
    required property T.Popup surface
    property real fromX: 0
    property real fromY: -4
    property bool fade: true
    property int enterDuration: Theme.normalDuration
    property int exitDuration: Theme.fastDuration
    property var enterCurve: Theme.easeEnter
    property EchoSlideMotion slide: EchoSlideMotion {
        target: motion.surface.visible ? motion.surface.contentItem.parent : null
        fromX: motion.fromX
        fromY: motion.fromY
    }
    property EchoMotionScope scope: EchoMotionScope {
        target: motion.surface.parent
        animations: [enterProgress, enterOpacity, exitOpacity]
    }
    property Transition enter: Transition {
        ParallelAnimation {
            NumberAnimation {
                id: enterProgress
                target: motion.slide
                property: "progress"
                from: 0
                to: 1
                duration: motion.scope.allowed ? motion.enterDuration : 0
                easing.type: Easing.BezierSpline
                easing.bezierCurve: motion.enterCurve
            }
            NumberAnimation {
                id: enterOpacity
                target: motion.surface
                property: "opacity"
                from: motion.fade ? 0 : 1
                to: 1
                duration: motion.scope.allowed ? motion.enterDuration : 0
                easing.type: Easing.BezierSpline
                easing.bezierCurve: motion.enterCurve
            }
        }
    }
    property Transition exit: Transition {
        NumberAnimation {
            id: exitOpacity
            target: motion.surface
            property: "opacity"
            to: 0
            duration: motion.scope.allowed && motion.fade ? motion.exitDuration : 0
            easing.type: Easing.BezierSpline
            easing.bezierCurve: Theme.easeExit
        }
    }
}
