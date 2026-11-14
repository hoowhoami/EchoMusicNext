pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import EchoMusic.Controls
import EchoMusic.Theme

EchoActionButton {
    id: vinyl
    property url artwork
    property bool resourceActive: true
    property bool spinning: false
    property bool loading: false
    property string trackId: ""
    readonly property bool animateDisc: resourceActive && spinning && visible && Theme.store.motionEnabled && Window.window !== null && Window.window.visible && Window.window.visibility !== Window.Minimized
    implicitWidth: 176
    implicitHeight: 176
    padding: 7
    hoverEnabled: true
    enabled: !loading
    Accessible.name: text
    transform: Translate {
        y: vinyl.hovered ? -2 : 0
        Behavior on y {
            enabled: vinyl.resourceActive
            NumberAnimation {
                duration: Theme.normalDuration
                easing.type: Easing.BezierSpline
                easing.bezierCurve: Theme.easeStandard
            }
        }
    }
    // Cache the surface, grooves and central highlight in one SVG texture.
    // Theme changes regenerate it; spinning only transforms the cover image.
    readonly property color discStart: Qt.tint(Theme.main, "#6b000000")
    readonly property color discMiddle: Qt.tint(Theme.main, "#ad000000")
    readonly property color discEnd: Qt.tint(Theme.main, "#80000000")
    readonly property string discSvg: '<svg xmlns="http://www.w3.org/2000/svg" width="176" height="176" viewBox="0 0 176 176"><defs><linearGradient id="surface" x1="0" y1="0" x2="1" y2="1"><stop stop-color="' + discStart + '"/><stop offset=".54" stop-color="' + discMiddle + '"/><stop offset="1" stop-color="' + discEnd + '"/></linearGradient><radialGradient id="grooves" gradientUnits="userSpaceOnUse" cx="88" cy="88" r="3" spreadMethod="repeat"><stop stop-color="' + Theme.text + '" stop-opacity=".10"/><stop offset=".25" stop-color="' + Theme.text + '" stop-opacity=".10"/><stop offset=".333333" stop-color="' + Theme.text + '" stop-opacity="0"/><stop offset="1" stop-color="' + Theme.text + '" stop-opacity="0"/></radialGradient><radialGradient id="center" gradientUnits="userSpaceOnUse" cx="88" cy="88" r="124.450793"><stop stop-color="' + Theme.text + '" stop-opacity=".18"/><stop offset=".07" stop-color="' + Theme.text + '" stop-opacity=".18"/><stop offset=".08" stop-color="' + Theme.text + '" stop-opacity=".04"/><stop offset=".18" stop-color="' + Theme.text + '" stop-opacity=".04"/><stop offset=".19" stop-color="' + Theme.text + '" stop-opacity="0"/><stop offset="1" stop-color="' + Theme.text + '" stop-opacity="0"/></radialGradient></defs><circle cx="88" cy="88" r="87.5" fill="url(#surface)"/><circle cx="88" cy="88" r="87.5" fill="url(#grooves)"/><circle cx="88" cy="88" r="87.5" fill="url(#center)" stroke="' + Theme.text + '" stroke-opacity=".2"/></svg>'
    background: Item {
        Image {
            objectName: "personalFmVinylTexture"
            anchors.fill: parent
            source: vinyl.resourceActive ? "data:image/svg+xml;utf8," + encodeURIComponent(vinyl.discSvg) : ""
            asynchronous: true
            retainWhileLoading: true
            sourceSize: Qt.size(Math.ceil(vinyl.width * Screen.devicePixelRatio), Math.ceil(vinyl.height * Screen.devicePixelRatio))
        }
        EchoDashedFrame {
            anchors.fill: parent
            solid: true
            strokeColor: Theme.accent
            radius: width / 2
            thickness: 2
            visible: vinyl.visualFocus
        }
    }
    Loader {
        anchors.fill: parent
        z: -1
        active: vinyl.resourceActive && vinyl.visible
        sourceComponent: EchoSurfaceShadow {
            anchors.fill: parent
            surface: vinyl
            radius: width / 2
            blur: 18
            offset: Qt.vector2d(0, 8)
            color: "#2e050c14"
        }
    }
    contentItem: EchoCover {
        id: cover
        source: vinyl.artwork
        circular: true
        resourceActive: vinyl.resourceActive
        EchoSkeleton {
            anchors.fill: parent
            radius: width / 2
            visible: vinyl.loading
            animate: visible && vinyl.resourceActive
        }
    }
    function updateSpin(): void {
        turn.stop();
        cover.rotation = 0;
        if (animateDisc)
            turn.start();
    }
    onAnimateDiscChanged: updateSpin()
    onTrackIdChanged: updateSpin()
    NumberAnimation {
        id: turn
        objectName: "personalFmDiscTurn"
        target: cover
        property: "rotation"
        from: 0
        to: 120
        duration: 3000
        easing.type: Easing.OutCubic
    }
}
