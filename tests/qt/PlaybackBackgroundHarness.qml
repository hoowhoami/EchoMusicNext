pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import EchoMusic.Playback
import EchoMusic.Shell

ApplicationWindow {
    id: window
    width: 1280
    height: 800
    visible: true
    color: "transparent"
    required property PlaybackService playback
    required property ShellController controller
    required property RouteController router
    property bool resourceActive: true
    readonly property alias backgroundSurface: background
    PlaybackBackground {
        id: background
        anchors.fill: parent
        playback: window.playback
        resourceActive: window.resourceActive && window.visible && window.visibility !== Window.Minimized
    }
}
