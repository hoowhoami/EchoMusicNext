pragma ComponentBehavior: Bound
import QtQuick
import QtMultimedia
import QtQuick.Effects
import EchoMusic.Theme
import EchoMusic.Controls
import EchoMusic.Playback

EchoCover {
    id: cover
    required property PlaybackService playback
    readonly property bool canPlay: resourceActive && playback.playing && playback.lyricAppearance.dynamicAlbumCover && Theme.store.motionEnabled
    readonly property AlbumVideoSurface videoSurface: videoLoader.item as AlbumVideoSurface
    source: playback.cover
    // Match the playerbar's current-track cache key before the flight starts.
    minimumDecodeSize: 380
    placeholderColor: colors.mutedSurface
    placeholderGlyphSize: 32
    function sync(): void {
        if (!canPlay && videoSurface)
            videoSurface.release();
        playback.dynamicCover.setActive(canPlay);
        if (canPlay && videoSurface)
            videoSurface.sync();
    }
    onCanPlayChanged: sync()
    Component.onCompleted: sync()
    Component.onDestruction: {
        if (videoSurface)
            videoSurface.release();
        playback.dynamicCover.setActive(false);
    }
    component AlbumVideoSurface: Item {
        id: surface
        required property url artworkSource
        property bool retiring: false
        function release(): void {
            if (!videoPlayer || retiring)
                return;
            retiring = true;
            // Close the decoder's input before revoking its managed HTTP URL.
            // Cancellation must not be treated as a broken artwork resource.
            videoPlayer.stop();
            videoPlayer.source = "";
        }
        Component.onDestruction: release()
        function sync(): void {
            if (!videoPlayer || retiring)
                return;
            if (cover.canPlay && videoPlayer.source.toString().length)
                videoPlayer.play();
            else
                videoPlayer.pause();
        }
        MediaPlayer {
            id: videoPlayer
            objectName: "playbackArtworkVideoPlayer"
            source: surface.artworkSource
            videoOutput: albumVideoOutput
            loops: MediaPlayer.Infinite
            // No audioOutput: optional artwork never creates another audio path.
            onSourceChanged: surface.sync()
            onErrorOccurred: if (!surface.retiring)
                cover.playback.dynamicCover.nextSource(surface.artworkSource)
        }
        Item {
            id: mask
            anchors.fill: parent
            visible: false
            layer.enabled: true
            layer.smooth: true
            Rectangle {
                anchors.fill: parent
                radius: cover.cornerRadius
                antialiasing: true
                color: "white"
            }
        }
        VideoOutput {
            id: albumVideoOutput
            objectName: "playbackArtworkVideoOutput"
            anchors.fill: parent
            visible: cover.canPlay && videoPlayer.mediaStatus === MediaPlayer.BufferedMedia
            fillMode: VideoOutput.PreserveAspectCrop
            endOfStreamPolicy: VideoOutput.ClearOutput
            layer.enabled: visible
            layer.effect: MultiEffect {
                maskEnabled: true
                maskThresholdMin: 0.5
                maskSpreadAtMin: 1
                autoPaddingEnabled: false
                maskSource: mask
            }
        }
    }
    Loader {
        id: videoLoader
        anchors.fill: parent
        // A closed page/default-off preference allocates no media player, video
        // sink or mask texture. Decoder objects leave with this visible surface.
        active: cover.resourceActive && cover.playback.lyricAppearance.dynamicAlbumCover && Theme.store.motionEnabled
        onLoaded: cover.sync()
        function replaceSurface(): void {
            if (active && cover.videoSurface && !cover.videoSurface.retiring && cover.videoSurface.artworkSource.toString() === cover.playback.dynamicCover.source.toString())
                return;
            sourceComponent = null;
            if (active && cover.playback.dynamicCover.source.toString().length)
                sourceComponent = videoComponent;
        }
        onActiveChanged: replaceSurface()
        Component {
            id: videoComponent
            AlbumVideoSurface {
                // Snapshot this decoder's resource. A changed URL gets a fresh
                // surface; queued errors cannot advance a newer track's backup.
                artworkSource: cover.playback.dynamicCover.source
                Component.onCompleted: artworkSource = cover.playback.dynamicCover.source
            }
        }
        Connections {
            target: cover.playback.dynamicCover
            function onChanged() {
                videoLoader.replaceSurface();
            }
        }
    }
}
