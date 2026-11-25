pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import EchoMusic.Theme
import EchoMusic.Controls
import EchoMusic.Playback

RowLayout {
    id: root
    Layout.fillHeight: false
    required property PlaybackService playback
    spacing: 6
    EchoIconButton {
        hoverBackground: false
        glyph: "player-skip-back"
        text: qsTr("上一首")
        implicitWidth: 38
        implicitHeight: 38
        enabled: root.playback.queue.count > 0
        onClicked: root.playback.previous()
    }
    EchoIconButton {
        id: toggle
        animatePlayPause: true
        playbackTooltipEnabled: true
        objectName: "playbackPagePlayToggle"
        glyph: root.playback.playing ? "pause" : "play"
        text: root.playback.playing ? qsTr("暂停") : qsTr("播放")
        implicitWidth: 38
        implicitHeight: 38
        enabled: root.playback.hasTrack
        onClicked: root.playback.toggle()
        background: Rectangle {
            radius: 19
            color: Theme.mutedSurface
        }
    }
    EchoIconButton {
        hoverBackground: false
        glyph: "player-skip-forward"
        text: qsTr("下一首")
        implicitWidth: 38
        implicitHeight: 38
        enabled: root.playback.queue.count > 0
        onClicked: root.playback.next()
    }
}
