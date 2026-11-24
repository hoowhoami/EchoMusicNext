pragma ComponentBehavior: Bound
import QtQuick
import EchoMusic.Theme
import EchoMusic.Playback

// A quiet, persistent status mark. It denotes software gain, not measured clipping.
Item {
    id: mark
    objectName: "playbackVolumeBoostMark"
    required property PlaybackService playback
    property ThemeColors colors: Theme.colors
    visible: playback !== null && playback.volume > 100 && !playback.cast.remote
    implicitWidth: 6
    implicitHeight: 6
    Accessible.ignored: true
    Rectangle {
        anchors.centerIn: parent
        width: 5
        height: 5
        radius: 2.5
        color: mark.colors.warning
    }
}
