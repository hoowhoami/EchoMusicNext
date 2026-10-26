pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import EchoMusic.Theme
import EchoMusic.Controls
import EchoMusic.Playback
import EchoMusic.Shell

ApplicationWindow {
    id: window
    width: 1200
    height: 800
    visible: true
    required property PlaybackService playback
    required property ShellController controller
    required property RouteController router
    property real surfaceScale: 1
    readonly property alias page: page
    EchoCover {
        id: smallCover
        objectName: "transitionSourceCover"
        x: 24
        y: window.height - 80
        width: 56
        height: 56
        // This source sits outside the scaled page in the harness.
        minimumDecodeSize: 380 * window.surfaceScale
        source: window.playback.cover
        opacity: page.coverMoving ? 0 : 1
    }
    PlaybackPage {
        id: page
        controlsTopReserve: (page.controlsItem as PlayerBar)?.aboveControlsHeight ?? 0
        sourceCover: smallCover
        playback: window.playback
        uiScale: window.surfaceScale
        controlsComponent: Component {
            PlayerBar {
                controller: window.controller
                router: window.router
                lyricStyle: true
                uiScale: window.surfaceScale
                colors: page.colors
            }
        }
    }
    Connections {
        target: window.controller
        function onPlaybackPopupRequested(kind) {
            if (kind === "lyric-skins")
                page.openSkins();
        }
    }
    Component.onCompleted: page.open()
}
