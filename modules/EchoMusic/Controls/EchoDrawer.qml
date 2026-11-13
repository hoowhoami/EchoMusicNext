pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import EchoMusic.Theme

Popup {
    id: drawer
    EchoPopupLifecycle {
        surface: drawer
    }
    property real uiScale: 1
    property real preferredWidth: 460
    property real playerBottomInset: 96
    readonly property real restingX: parent ? parent.width - (width + (parent.width / uiScale <= 420 ? 8 : 12)) * uiScale : 0
    parent: Overlay.overlay
    scale: uiScale
    transformOrigin: Popup.TopLeft
    width: Math.min(preferredWidth, parent ? parent.width / uiScale - (parent.width / uiScale <= 420 ? 16 : 24) : preferredWidth)
    x: restingX
    y: (Math.max(46, 35 / uiScale) + (parent && parent.width / uiScale <= 420 ? 4 : 16)) * uiScale
    height: parent ? Math.max(0, parent.height / uiScale - y / uiScale - playerBottomInset - (parent.width / uiScale <= 420 ? 8 : 12)) : 480
    padding: 0
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    Overlay.modal: Rectangle {
        color: Theme.dark ? "#47000000" : "#33000000"
    }
    background: EchoFloatingSurface {
        radius: Theme.popoverRadius
        border.width: 0
        dialogShadow: true
    }
    enter: Transition {
        ParallelAnimation {
            NumberAnimation {
                property: "opacity"
                from: 0
                to: 1
                duration: Theme.panelDuration
                easing.type: Easing.BezierSpline
                easing.bezierCurve: Theme.easeEnter
            }
            NumberAnimation {
                property: "x"
                from: drawer.restingX + 16 * drawer.uiScale
                to: drawer.restingX
                duration: Theme.panelDuration
                easing.type: Easing.BezierSpline
                easing.bezierCurve: Theme.easeEnter
            }
        }
    }
    exit: Transition {
        ParallelAnimation {
            NumberAnimation {
                property: "opacity"
                from: 1
                to: 0
                duration: Theme.exitDuration
                easing.type: Easing.BezierSpline
                easing.bezierCurve: Theme.easeExit
            }
            NumberAnimation {
                property: "x"
                from: drawer.restingX
                to: drawer.restingX + 16 * drawer.uiScale
                duration: Theme.exitDuration
                easing.type: Easing.BezierSpline
                easing.bezierCurve: Theme.easeExit
            }
        }
    }
}
