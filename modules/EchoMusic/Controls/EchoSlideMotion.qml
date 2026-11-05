pragma ComponentBehavior: Bound
import QtQuick
import EchoMusic.Theme

// Opt-in presentation transform, preserving the target's other transforms.
// Native placement, Flickable coordinates and layout remain with their owner.
QtObject {
    id: motion
    required property Item target
    property bool active: true
    property real fromX: 0
    property real fromY: 0
    property real fromOpacity: 1
    property int duration: Theme.normalDuration
    property var curve: Theme.easeEnter
    property real progress: 1
    readonly property bool running: animation.running
    readonly property bool allowed: scope.allowed
    function finish(): void {
        animation.stop();
        progress = 1;
    }
    function play(): void {
        finish();
        if (!allowed || duration <= 0)
            return;
        progress = 0;
        animation.start();
    }
    property EchoMotionScope scope: EchoMotionScope {
        target: motion.target
        active: motion.active
        onAllowedChanged: if (!allowed)
            motion.finish()
    }
    property EchoItemTranslation translation: EchoItemTranslation {
        target: motion.target
        x: motion.fromX * (1 - motion.progress)
        y: motion.fromY * (1 - motion.progress)
    }
    property Binding opacityBinding: Binding {
        target: motion.target
        property: "opacity"
        value: motion.fromOpacity + (1 - motion.fromOpacity) * motion.progress
        when: motion.target !== null && motion.fromOpacity !== 1
        restoreMode: Binding.RestoreBindingOrValue
    }
    property NumberAnimation animation: NumberAnimation {
        target: motion
        property: "progress"
        to: 1
        duration: motion.duration
        easing.type: Easing.BezierSpline
        easing.bezierCurve: motion.curve
    }
}
