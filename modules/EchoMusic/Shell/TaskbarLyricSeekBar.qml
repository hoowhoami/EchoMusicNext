pragma ComponentBehavior: Bound
import QtQuick
import EchoMusic.Theme
import EchoMusic.Playback

// 展开态的进度条与拖拽跳转。从 TaskbarLyricWindow 拆出：进度手势有自己的
// 取消规则（参考实现 useTaskbarSeek），与歌词排版互不相干。
Item {
    id: seekArea
    required property PlaybackService playback
    required property color fg
    required property color accent
    /// Whether the bar belongs to the current layout (expanded and wide enough).
    required property bool active

    property bool dragging: false
    property real dragValue: 0
    // The track the gesture started on. A song change in the middle of the drag
    // cancels the pending seek instead of seeking the track that replaced it
    // (reference useTaskbarSeek: "A canceled gesture (including a song change)
    // must not seek the new song").
    property string dragTrackId: ""

    opacity: active ? 1 : 0
    visible: opacity > 0.01
    enabled: active
    Behavior on opacity {
        NumberAnimation {
            duration: Theme.store.motionEnabled ? 180 : 0
        }
    }

    Rectangle {
        id: track
        anchors.verticalCenter: parent.verticalCenter
        anchors.left: parent.left
        anchors.right: parent.right
        height: 2
        radius: 1
        color: Qt.rgba(seekArea.fg.r, seekArea.fg.g, seekArea.fg.b, 0.25)
    }
    Rectangle {
        id: range
        anchors.verticalCenter: parent.verticalCenter
        anchors.left: parent.left
        height: 2
        radius: 1
        width: track.width * (seekArea.dragging ? (seekArea.playback.durationMs > 0 ? seekArea.dragValue / seekArea.playback.durationMs : 0) : (seekArea.playback.durationMs > 0 ? seekArea.playback.positionMs / seekArea.playback.durationMs : 0))
        color: seekArea.accent
    }
    Rectangle {
        id: thumb
        width: 8
        height: 8
        radius: 4
        color: seekArea.accent
        visible: seekArea.dragging
        x: range.width - width / 2
        y: (parent.height - height) / 2
    }
    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        onPressed: function (mouse) {
            seekArea.dragging = true;
            seekArea.dragTrackId = seekArea.playback.trackId;
            seekArea.dragValue = Math.max(0, Math.min(seekArea.playback.durationMs, mouse.x / Math.max(1, seekArea.width) * seekArea.playback.durationMs));
        }
        onPositionChanged: function (mouse) {
            if (!seekArea.dragging)
                return;
            seekArea.dragValue = Math.max(0, Math.min(seekArea.playback.durationMs, mouse.x / Math.max(1, seekArea.width) * seekArea.playback.durationMs));
        }
        onReleased: {
            if (!seekArea.dragging)
                return;
            seekArea.dragging = false;
            // A track that changed under the gesture (or a stream that reports
            // no duration) has nothing to seek: drop the pending value.
            if (seekArea.dragTrackId !== seekArea.playback.trackId || seekArea.playback.durationMs <= 0)
                return;
            // No qint64() in QML: the invokable converts.
            seekArea.playback.seek(Math.floor(seekArea.dragValue));
        }
        // Leaving the thin strip must not cancel the drag; the reference
        // cancels on track change instead.
    }
}
