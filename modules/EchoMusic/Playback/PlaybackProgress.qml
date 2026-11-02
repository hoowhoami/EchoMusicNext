pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Controls
import EchoMusic.Theme
import EchoMusic.Playback

RowLayout {
    id: root
    Layout.fillHeight: false
    property PlaybackService playback: null
    onPlaybackChanged: if (slider)
        slider.submitted = false
    property bool compact: true
    property ThemeColors colors: Theme.colors
    property bool edgeToEdge: false
    implicitHeight: compact ? 16 : 24
    spacing: 12
    function time(value) {
        const s = Math.max(0, Math.floor(value / 1000));
        return String(Math.floor(s / 60)).padStart(2, "0") + ":" + String(s % 60).padStart(2, "0");
    }
    Label {
        objectName: "playbackPositionText"
        visible: !root.edgeToEdge
        text: root.time(slider.previewing ? slider.value : root.playback ? root.playback.positionMs : 0)
        color: root.colors.secondaryText
        font: Theme.font(root.compact ? 10 : 12, Theme.mediumWeight)
        Layout.preferredWidth: root.compact ? 36 : 46
        horizontalAlignment: Text.AlignRight
    }
    EchoSlider {
        id: slider
        colors: root.colors
        objectName: "playbackSeekSlider"
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        padding: 0
        progress: true
        markers: root.playback ? root.playback.climax : null
        markerOpacity: root.edgeToEdge ? 0.78 : Theme.dark ? 0.65 : 0.8
        busy: root.playback !== null && root.playback.progressBusyReason !== PlaybackService.ProgressReady
        opacity: enabled ? 1 : 0.5
        from: 0
        to: Math.max(1, root.playback ? root.playback.durationMs : 0)
        enabled: root.playback && root.playback.hasTrack && !root.playback.loading
        property bool held: false
        property bool submitted: false
        property string heldTrackSequence: ""
        readonly property bool previewing: pressed || held || submitted && root.playback !== null && root.playback.clock.displayState === "seeking"
        function commitSeek(): void {
            submitted = true;
            root.playback.seek(Math.round(value));
        }
        Connections {
            target: root.playback
            function onClockChanged(): void {
                if (!root.playback || root.playback.clock.displayState !== "seeking")
                    slider.submitted = false;
            }
        }
        Binding on value {
            when: !slider.previewing
            value: root.playback ? root.playback.positionMs : 0
            restoreMode: Binding.RestoreNone
        }
        implicitHeight: root.compact ? 16 : 24
        onPressedChanged: {
            if (pressed) {
                heldTrackSequence = root.playback ? root.playback.clock.trackSeq : "";
                held = true;
            } else if (held) {
                // Keep the drag value until seek() has synchronously published
                // its pending state. Native confirmation resumes the binding.
                if (enabled && visible && root.playback && heldTrackSequence === root.playback.clock.trackSeq)
                    commitSeek();
                held = false;
            }
        }
        onMoved: if (!pressed && root.playback)
            commitSeek()
        Accessible.name: !root.playback || root.playback.progressBusyReason === PlaybackService.ProgressReady ? qsTr("播放进度") : root.playback.progressBusyReason === PlaybackService.ProgressSeeking ? qsTr("播放进度，定位中") : qsTr("播放进度，缓冲中")
        EchoToolTip {
            parent: slider
            delay: 0
            requestedVisible: root.edgeToEdge && (slider.hovered || slider.pressed)
            text: root.time(slider.previewing ? slider.value : root.playback ? root.playback.positionMs : 0) + " / " + root.time(root.playback ? root.playback.durationMs : 0)
            anchorPosition: slider.visualPosition
        }
    }
    Label {
        visible: !root.edgeToEdge
        text: root.time(root.playback ? root.playback.durationMs : 0)
        color: root.colors.secondaryText
        font: Theme.font(root.compact ? 10 : 12, Theme.mediumWeight)
        Layout.preferredWidth: root.compact ? 36 : 46
    }
}
