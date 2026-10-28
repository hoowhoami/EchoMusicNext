pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Window
import EchoMusic.Theme

Item {
    id: root
    property bool playing: false
    property bool covered: false
    property color color: Theme.text
    property real playOffset: 0
    property real phase: playing ? 1 : 0
    property bool initialized: false
    property bool revealPending: false
    readonly property bool ready: playIcon.status === Image.Ready && pauseIcon.status === Image.Ready
    readonly property bool canAnimate: !covered && visible && ready && Theme.store.motionEnabled && Window.window !== null && Window.window.visible && Window.window.visibility !== Window.Minimized
    readonly property bool transitioning: transition.running
    implicitWidth: 20
    implicitHeight: 20
    objectName: "playPauseIcon"
    // Only presentation interpolates; the playback command and accessible name
    // change immediately. A reversal starts at the current phase, without a queue.
    function synchronize() {
        if (!initialized)
            return;
        transition.stop();
        if (canAnimate && !revealPending) {
            transition.from = phase;
            transition.to = playing ? 1 : 0;
            transition.start();
        } else {
            phase = playing ? 1 : 0;
        }
        if (!covered && playing)
            revealPending = false;
    }
    Component.onCompleted: {
        phase = playing ? 1 : 0;
        initialized = true;
    }
    onPlayingChanged: synchronize()
    // File-loaded and the native playing acknowledgement can arrive in
    // separate turns. Do not replay play->pause when that first state arrives
    // after the spinner has gone; ordinary subsequent toggles still animate.
    onCoveredChanged: {
        if (covered)
            revealPending = true;
        if (!initialized)
            return;
        if (covered || revealPending && playing) {
            transition.stop();
            phase = playing ? 1 : 0;
        }
        if (!covered && playing)
            revealPending = false;
    }
    onCanAnimateChanged: if (initialized && !canAnimate) {
        transition.stop();
        phase = playing ? 1 : 0;
    }
    EchoIcon {
        id: playIcon
        objectName: "playPausePlayIcon"
        anchors.fill: parent
        // anchors.fill cannot carry an offset, so translate only the glyph.
        transform: Translate {
            x: root.playOffset
        }
        name: "play"
        rasterizationItem: root
        color: root.color
        opacity: 1 - root.phase
        scale: 1 - 0.18 * root.phase
    }
    EchoIcon {
        id: pauseIcon
        objectName: "playPausePauseIcon"
        anchors.fill: parent
        name: "pause"
        rasterizationItem: root
        color: root.color
        opacity: root.phase
        scale: 0.82 + 0.18 * root.phase
    }
    NumberAnimation {
        id: transition
        target: root
        property: "phase"
        duration: 180
        easing.type: Easing.OutCubic
    }
}
