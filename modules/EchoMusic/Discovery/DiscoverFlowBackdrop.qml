pragma ComponentBehavior: Bound
import QtQuick
import EchoMusic.Theme

// Retain the displayed cover until the next image is ready. At most two images
// exist during the 400ms crossfade; hidden and idle slots own no image or effect.
Item {
    id: backdrop
    property url source
    property bool resourceActive: true
    // Carries the URL whose decode failed, so the owner can retire it from its
    // own gallery (the artist-portrait controller drops it and rotates on).
    signal failed(url imageUrl)
    property url firstSource: ""
    property url secondSource: ""
    property int currentSlot: -1
    property int pendingSlot: -1
    property int fadeDuration: 0
    readonly property int imageCount: (first.item ? 1 : 0) + (second.item ? 1 : 0)
    readonly property bool running: crossfade.running
    function clear(): void {
        crossfade.stop();
        pendingSlot = -1;
        currentSlot = -1;
        firstSource = "";
        secondSource = "";
        first.opacity = 0;
        second.opacity = 0;
    }
    function finish(): void {
        if (pendingSlot < 0)
            return;
        currentSlot = pendingSlot;
        pendingSlot = -1;
        if (currentSlot === 0) {
            secondSource = "";
            first.opacity = 1;
            second.opacity = 0;
        } else {
            firstSource = "";
            second.opacity = 1;
            first.opacity = 0;
        }
    }
    function updateSource(): void {
        if (!resourceActive || source.toString().length === 0) {
            clear();
            return;
        }
        if (crossfade.running) {
            crossfade.stop();
            finish();
        }
        if ((currentSlot === 0 && firstSource === source) || (currentSlot === 1 && secondSource === source))
            return;
        pendingSlot = currentSlot === 0 ? 1 : 0;
        if (pendingSlot === 0) {
            first.opacity = 0;
            firstSource = source;
        } else {
            second.opacity = 0;
            secondSource = source;
        }
    }
    function imageReady(slot: int): void {
        if (slot !== pendingSlot)
            return;
        if (!Theme.store.motionEnabled)
            finish();
        else {
            fadeDuration = Theme.mediaFadeDuration;
            crossfade.restart();
        }
    }
    function imageFailed(slot: int): void {
        if (slot !== pendingSlot)
            return;
        failed(pendingSlot === 0 ? firstSource : secondSource);
        clear();
    }
    onSourceChanged: updateSource()
    onResourceActiveChanged: updateSource()
    Component.onCompleted: updateSource()
    Loader {
        id: first
        anchors.fill: parent
        active: backdrop.resourceActive && backdrop.firstSource.toString().length > 0
        opacity: 0
        sourceComponent: DiscoverFlowArtwork {
            imageUrl: backdrop.firstSource
            onReady: backdrop.imageReady(0)
            onFailed: backdrop.imageFailed(0)
        }
    }
    Loader {
        id: second
        anchors.fill: parent
        active: backdrop.resourceActive && backdrop.secondSource.toString().length > 0
        opacity: 0
        sourceComponent: DiscoverFlowArtwork {
            imageUrl: backdrop.secondSource
            onReady: backdrop.imageReady(1)
            onFailed: backdrop.imageFailed(1)
        }
    }
    ParallelAnimation {
        id: crossfade
        NumberAnimation {
            target: first
            property: "opacity"
            to: backdrop.pendingSlot === 0 ? 1 : 0
            duration: backdrop.fadeDuration
            easing.type: Easing.BezierSpline
            easing.bezierCurve: Theme.easeDefault
        }
        NumberAnimation {
            target: second
            property: "opacity"
            to: backdrop.pendingSlot === 1 ? 1 : 0
            duration: backdrop.fadeDuration
            easing.type: Easing.BezierSpline
            easing.bezierCurve: Theme.easeDefault
        }
        onFinished: backdrop.finish()
    }
    Connections {
        target: Theme.store
        function onChanged(): void {
            if (!Theme.store.motionEnabled && crossfade.running) {
                crossfade.stop();
                backdrop.finish();
            }
        }
    }
}
