pragma ComponentBehavior: Bound
import QtQuick
import EchoMusic.Theme
import EchoMusic.Controls

// Non-visual content entry: fades an existing item after activation. The page
// keeps geometry and layout ownership; this object only animates opacity, so
// virtual lists, sticky headers and scroll positions are unaffected. A stagger
// order uses the shared 32ms motion token, capped like EchoMusic's section rule.
QtObject {
    id: root
    required property Item target
    property bool active: true
    property int order: 0
    property int duration: Theme.normalDuration
    property var curve: Theme.easeStandard
    readonly property bool running: delay.running || animation.running
    function visibleWindow(): bool {
        const window = root.target ? root.target.Window.window : null;
        return window !== null && window.visible && window.visibility !== Window.Minimized;
    }
    function stop(): void {
        delay.stop();
        animation.stop();
    }
    function showImmediately(): void {
        stop();
        if (root.target)
            root.target.opacity = 1;
    }
    function play(): void {
        if (!root.target)
            return;
        const presentation = root.target.EchoPageState.presentation;
        if (presentation && presentation.suppressEntry) {
            showImmediately();
            return;
        }
        if (!root.active) {
            stop();
            root.target.opacity = 0;
            return;
        }
        if (root.duration <= 0 || !Theme.store.motionEnabled || !visibleWindow()) {
            showImmediately();
            return;
        }
        stop();
        root.target.opacity = 0;
        delay.interval = Math.min(96, Math.max(0, root.order) * Theme.staggerDelay);
        delay.start();
    }
    onActiveChanged: play()
    onTargetChanged: play()
    Component.onCompleted: play()
    property Connections windowEvents: Connections {
        target: root.target ? root.target.Window.window : null
        function onVisibilityChanged(): void {
            if (!root.visibleWindow())
                root.showImmediately();
        }
    }
    property Connections targetEvents: Connections {
        target: root.target
        function onVisibleChanged(): void {
            if (root.target && !root.target.visible)
                root.showImmediately();
        }
    }
    property Connections motionEvents: Connections {
        target: Theme.store
        function onChanged(): void {
            if (!Theme.store.motionEnabled && root.running)
                root.showImmediately();
        }
    }
    property Timer delay: Timer {
        repeat: false
        onTriggered: {
            if (root.active && root.visibleWindow())
                root.animation.start();
            else
                root.showImmediately();
        }
    }
    property NumberAnimation animation: NumberAnimation {
        target: root.target
        property: "opacity"
        to: 1
        duration: root.duration
        easing.type: Easing.BezierSpline
        easing.bezierCurve: root.curve
    }
}
