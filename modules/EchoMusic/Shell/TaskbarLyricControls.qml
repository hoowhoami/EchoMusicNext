pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import EchoMusic.Theme
import EchoMusic.Controls
import EchoMusic.Playback

// 展开态的控制按钮行。从 TaskbarLyricWindow 拆出：按钮集合、按钮几何与
// 播放动作各自变化，与歌词条的排版无关（参考实现 .controls）。
// 参考实现的控制按钮：14px 图标、6px 圆角、悬停底色，播放键用强调色。
RowLayout {
    id: controls
    required property PlaybackService playback
    required property color fg
    required property color accent
    required property int controlSize
    /// Whether the row belongs to the current layout (the strip is expanded).
    required property bool active

    // A fixed width keeps every button in place: a row that animates between 0
    // and its size squeezes the buttons on the way in and shifts them on the
    // way out.
    Layout.preferredWidth: 4 * controlSize + 20
    Layout.alignment: Qt.AlignVCenter
    spacing: 4
    visible: active
    enabled: active

    component TaskbarIconButton: Rectangle {
        id: button
        required property string glyph
        property color glyphColor: controls.fg
        property color fillColor: "transparent"
        property color hoverColor: Qt.rgba(controls.fg.r, controls.fg.g, controls.fg.b, 0.1)
        signal clicked
        implicitWidth: controls.controlSize
        implicitHeight: controls.controlSize
        radius: 6
        // Hover stays declarative on purpose: assigning color imperatively in
        // onEntered/onExited would break the fillColor binding, so a fill that
        // follows the accent (the play/pause key) would keep the stale song's
        // color until the next hover cycle.
        color: area.containsMouse ? hoverColor : fillColor
        // 按压反馈（参考实现 .controls button:active）：按下缩到 0.94，
        // 松开回弹，与悬停底色的 180ms 过渡同速。
        scale: area.pressed ? 0.94 : 1
        Behavior on scale {
            NumberAnimation {
                duration: Theme.store.motionEnabled ? 180 : 0
                easing.type: Easing.OutCubic
            }
        }
        EchoIcon {
            anchors.centerIn: parent
            name: button.glyph
            width: 14
            height: 14
            color: button.glyphColor
        }
        MouseArea {
            id: area
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: button.clicked()
        }
    }

    TaskbarIconButton {
        objectName: "taskbarPrevious"
        glyph: "player-skip-back"
        onClicked: controls.playback.previous()
    }
    TaskbarIconButton {
        objectName: "taskbarPlayPause"
        glyph: controls.playback.playing ? "pause" : "play"
        glyphColor: controls.accent
        fillColor: Qt.rgba(controls.accent.r, controls.accent.g, controls.accent.b, 0.16)
        hoverColor: Qt.rgba(controls.accent.r, controls.accent.g, controls.accent.b, 0.24)
        onClicked: controls.playback.toggle()
    }
    TaskbarIconButton {
        objectName: "taskbarNext"
        glyph: "player-skip-forward"
        onClicked: controls.playback.next()
    }
    TaskbarIconButton {
        objectName: "taskbarFavorite"
        glyph: controls.playback.libraryActions.favorite ? "heart-filled" : "heart"
        glyphColor: controls.playback.libraryActions.favorite ? "#ef4444" : controls.fg
        onClicked: controls.playback.libraryActions.toggleFavorite()
    }
}
