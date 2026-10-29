pragma ComponentBehavior: Bound
import QtQuick
import EchoMusic.Theme
import EchoMusic.Playback

// Shared Apple Music line presentation. Layouts own their geometry and source
// timestamps; this object changes only native rendering uniforms/transforms.
QtObject {
    id: motion
    property bool activeLine: true
    property bool resourceActive: true
    property bool animationEnabled: true
    property bool springEnabled: true
    property bool shrinkInactive: true
    property bool blurInactive: true
    property real inactiveBlur: 1.6
    property real targetOpacity: 1
    readonly property bool canAnimate: ready && resourceActive && animationEnabled && Theme.store.motionEnabled
    readonly property real targetScale: shrinkInactive && !activeLine ? 97 : 100
    readonly property real targetBlur: blurInactive && !activeLine ? inactiveBlur : 0
    property real highlight: 1
    property real blur: 0
    property real opacity: 1
    readonly property real scale: spring.value / 100
    readonly property bool running: canAnimate && (spring.running || highlightAnimation.running || blurAnimation.running || opacityAnimation.running)
    property bool ready: false
    function retarget(animation: NumberAnimation, value: real, destination: real, duration: int): void {
        if (animation.running && animation.to === destination)
            return;
        animation.stop();
        if (Math.abs(value - destination) < .0001)
            return;
        animation.from = value;
        animation.to = destination;
        animation.duration = duration;
        animation.start();
    }
    function update(): void {
        if (!ready)
            return;
        if (!canAnimate) {
            settle();
            return;
        }
        retarget(highlightAnimation, highlight, activeLine ? 1 : 0, activeLine ? 300 : 450);
        retarget(blurAnimation, blur, targetBlur, 400);
        retarget(opacityAnimation, opacity, targetOpacity, 400);
        spring.retargetScale(targetScale, springEnabled);
    }
    function settle(): void {
        highlightAnimation.stop();
        blurAnimation.stop();
        opacityAnimation.stop();
        highlight = activeLine ? 1 : 0;
        blur = targetBlur;
        opacity = targetOpacity;
        spring.snap(targetScale);
    }
    // A compact layout replaces one bounded presentation snapshot. Seed its
    // outgoing/incoming ink from that snapshot, rather than fading the whole
    // newly singing line in from transparent.
    function startFrom(highlightValue: real, blurValue: real, scaleValue: real): void {
        if (!canAnimate) {
            settle();
            return;
        }
        highlightAnimation.stop();
        blurAnimation.stop();
        highlight = highlightValue;
        blur = blurValue;
        spring.snap(scaleValue * 100);
        update();
    }
    onSpringEnabledChanged: update()
    onActiveLineChanged: update()
    onTargetScaleChanged: update()
    onTargetBlurChanged: update()
    onTargetOpacityChanged: update()
    onCanAnimateChanged: update()
    Component.onCompleted: {
        ready = true;
        settle();
    }
    property LyricSpring spring: LyricSpring {}
    property FrameAnimation ticker: FrameAnimation {
        running: motion.canAnimate && motion.spring.running
        onTriggered: motion.spring.advance()
    }
    property NumberAnimation highlightAnimation: NumberAnimation {
        target: motion
        property: "highlight"
        easing.type: Easing.BezierSpline
        easing.bezierCurve: [0, 0, .58, 1, 1, 1]
    }
    property NumberAnimation blurAnimation: NumberAnimation {
        target: motion
        property: "blur"
        easing.type: Easing.BezierSpline
        easing.bezierCurve: [.25, .1, .25, 1, 1, 1]
    }
    property NumberAnimation opacityAnimation: NumberAnimation {
        target: motion
        property: "opacity"
        easing.type: Easing.BezierSpline
        easing.bezierCurve: [.25, .1, .25, 1, 1, 1]
    }
}
