pragma ComponentBehavior: Bound
import QtQuick
import EchoMusic.Theme
import EchoMusic.Playback

Item {
    id: background
    required property PlaybackService playback
    property bool resourceActive: visible
    property bool windowBackdropActive: true
    property color baseColor: "#1a1d22"
    property real phase: 0
    property color displayedBaseColor: baseColor
    Behavior on displayedBaseColor {
        ColorAnimation {
            duration: Theme.store.motionEnabled ? 500 : 0
        }
    }
    readonly property real materialOpacity: 1 - Theme.store.transparency / 100
    readonly property bool windowBackdropEnabled: Theme.store.windowFrosted && background.windowBackdropActive
    readonly property bool blurActive: resourceActive && !background.windowBackdropEnabled && materialOpacity > 0 && playback.lyricAppearance.backgroundBlur && playback.cover.toString().length > 0
    readonly property bool blurReady: effect.item !== null && (effect.item as PlaybackBlurredBackground).ready
    // Keep the preference, but never render/animate rhythm without blur.
    readonly property bool rhythmActive: blurActive && playback.lyricAppearance.backgroundRhythm
    clip: true
    Rectangle {
        anchors.fill: parent
        // Exactly one final background quad is visible. Applying opacity to
        // overlapping base/artwork/scrim Items would accumulate their alpha.
        visible: !background.blurReady
        color: background.windowBackdropEnabled ? Qt.rgba(0, 0, 0, .55) : Theme.withAlpha(background.displayedBaseColor, background.materialOpacity)
    }
    Loader {
        id: effect
        anchors.fill: parent
        active: background.blurActive
        sourceComponent: PlaybackBlurredBackground {
            objectName: "playbackBackgroundSurface"
            coverSource: background.playback.cover
            rhythm: background.rhythmActive
            phase: background.phase
            baseColor: background.displayedBaseColor
            backgroundOpacity: background.materialOpacity
        }
    }
    FrameAnimation {
        running: background.rhythmActive && effect.item && (effect.item as PlaybackBlurredBackground).ready && background.playback.playing && Theme.store.motionEnabled
        property real accumulated: 0
        onRunningChanged: accumulated = 0
        onTriggered: {
            accumulated += Math.min(frameTime, .1);
            if (accumulated >= 1 / 30) {
                const remainder = accumulated % (1 / 30);
                background.phase += accumulated - remainder;
                accumulated = remainder;
            }
        }
    }
}
