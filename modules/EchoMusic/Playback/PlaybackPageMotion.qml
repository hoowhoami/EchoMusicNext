pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Effects
import EchoMusic.Theme
import EchoMusic.Controls

// Own only transient compositor resources; playback and layout remain elsewhere.
Item {
    id: motion
    property EchoCover sourceCover: null
    required property EchoCover targetCover
    property real hostOpacity: 0
    property real contentOpacity: 0
    property real contentOffset: 20
    property real progress: 1
    property real fromBlend: 0
    property real toBlend: 1
    property bool coverFlight: false
    property bool entering: true
    property rect fromRect
    property rect toRect
    property rect largeRect
    property real smallRadius: 0
    property real smallGlyphSize: 0
    property Item snapshotSource: null
    property Component snapshotPlaceholder: null
    readonly property real cornerBlend: fromBlend + (toBlend - fromBlend) * progress
    readonly property rect currentRect: Qt.rect(fromRect.x + (toRect.x - fromRect.x) * progress, fromRect.y + (toRect.y - fromRect.y) * progress, fromRect.width + (toRect.width - fromRect.width) * progress, fromRect.height + (toRect.height - fromRect.height) * progress)
    readonly property int travelDuration: !Theme.store.motionEnabled ? 0 : coverFlight ? (entering ? 460 : 360) : 400
    function bounds(item: Item): rect {
        return item.mapToItem(motion, 0, 0, item.width, item.height);
    }
    function blendColor(from: color, to: color): color {
        const t = cornerBlend;
        return Qt.rgba(from.r + (to.r - from.r) * t, from.g + (to.g - from.g) * t, from.b + (to.b - from.b) * t, from.a + (to.a - from.a) * t);
    }
    function prepare(forward: bool): void {
        const interrupted = coverFlight;
        const previous = currentRect;
        const blend = cornerBlend;
        entering = forward;
        // Popup has assigned its final size. Flush its native layout once,
        // never read layout geometry on an animation frame.
        targetCover.parent.parent.ensurePolished();
        targetCover.parent.ensurePolished();
        const small = sourceCover ? bounds(sourceCover) : Qt.rect(0, 0, 0, 0);
        largeRect = bounds(targetCover);
        coverFlight = Theme.store.motionEnabled && small.width > 0 && small.height > 0 && largeRect.width > 0 && largeRect.height > 0;
        if (coverFlight) {
            fromRect = interrupted ? previous : forward ? small : largeRect;
            toRect = forward ? largeRect : small;
            fromBlend = interrupted ? blend : forward ? 0 : 1;
            toBlend = forward ? 1 : 0;
            smallRadius = sourceCover.cornerRadius * largeRect.width / small.width;
            smallGlyphSize = sourceCover.placeholderGlyphSize * small.width / sourceCover.width;
            // Both artwork and placeholder use the same flight and corner masks.
            // Freeze the choice across interruptions without another decoder.
            if (!interrupted) {
                const cover = forward ? sourceCover : targetCover;
                snapshotSource = cover.transitionArtwork;
                snapshotPlaceholder = cover.status === Image.Ready ? null : cover.transitionPlaceholder;
            }
        }
        progress = 0;
        if (forward && !interrupted) {
            contentOpacity = 0;
            contentOffset = coverFlight ? 20 : 0;
        }
    }
    function finish(): void {
        coverFlight = false;
        snapshotSource = null;
        snapshotPlaceholder = null;
    }
    visible: coverFlight
    enabled: false
    EchoImageMetrics {
        id: flightMetrics
        item: motion.targetCover
        active: motion.coverFlight
    }
    // Keep the placeholder at its actual animated size. The shared mask owns
    // its corners, while the glyph travels between the bar and page sizes.
    Loader {
        id: placeholderSnapshot
        objectName: "playbackCoverFlightPlaceholder"
        active: motion.coverFlight && motion.snapshotPlaceholder !== null
        sourceComponent: motion.snapshotPlaceholder
        width: motion.currentRect.width
        height: motion.currentRect.height
        Binding {
            target: placeholderSnapshot.item
            property: "glyphSize"
            value: motion.smallGlyphSize + (motion.targetCover.placeholderGlyphSize - motion.smallGlyphSize) * motion.cornerBlend
            when: placeholderSnapshot.item !== null
        }
        Binding {
            target: placeholderSnapshot.item
            property: "color"
            value: motion.sourceCover ? motion.blendColor(motion.sourceCover.placeholderColor, motion.targetCover.placeholderColor) : motion.targetCover.placeholderColor
            when: placeholderSnapshot.item !== null
        }
        Binding {
            target: placeholderSnapshot.item
            property: "glyphColor"
            value: motion.sourceCover ? motion.blendColor(motion.sourceCover.colors.text, motion.targetCover.colors.text) : motion.targetCover.colors.text
            when: placeholderSnapshot.item !== null
        }
        Binding {
            target: placeholderSnapshot.item
            property: "glyphOpacity"
            value: motion.sourceCover ? motion.sourceCover.placeholderOpacity + (motion.targetCover.placeholderOpacity - motion.sourceCover.placeholderOpacity) * motion.cornerBlend : motion.targetCover.placeholderOpacity
            when: placeholderSnapshot.item !== null
        }
    }
    Loader {
        active: motion.coverFlight
        sourceComponent: Item {
            objectName: "playbackCoverFlight"
            width: motion.largeRect.width
            height: motion.largeRect.height
            x: motion.currentRect.x
            y: motion.currentRect.y
            transform: Scale {
                xScale: motion.currentRect.width / Math.max(1, motion.largeRect.width)
                yScale: motion.currentRect.height / Math.max(1, motion.largeRect.height)
            }
            ShaderEffectSource {
                id: snapshot
                objectName: "playbackCoverFlightCapture"
                anchors.fill: parent
                sourceItem: placeholderSnapshot.active ? (placeholderSnapshot.item as Item) : motion.snapshotSource
                hideSource: placeholderSnapshot.active
                // A frozen small-cover snapshot must retain destination density;
                // otherwise its source-item geometry downsamples the loaded art.
                textureSize: flightMetrics.pixelSize
                live: placeholderSnapshot.active
                visible: false
            }
            Item {
                id: largeMask
                anchors.fill: parent
                visible: false
                layer.enabled: true
                layer.smooth: true
                layer.textureSize: flightMetrics.pixelSize
                Rectangle {
                    anchors.fill: parent
                    radius: motion.targetCover.cornerRadius
                    antialiasing: true
                    color: "white"
                }
            }
            Item {
                id: smallMask
                anchors.fill: parent
                visible: false
                layer.enabled: true
                layer.smooth: true
                layer.textureSize: flightMetrics.pixelSize
                Rectangle {
                    anchors.fill: parent
                    radius: motion.smallRadius
                    antialiasing: true
                    color: "white"
                }
            }
            MultiEffect {
                anchors.fill: parent
                source: snapshot
                maskEnabled: true
                maskThresholdMin: 0.5
                maskSpreadAtMin: 1
                autoPaddingEnabled: false
                maskSource: smallMask
                opacity: Math.min(1, (1 - motion.cornerBlend) / .35)
            }
            MultiEffect {
                anchors.fill: parent
                source: snapshot
                maskEnabled: true
                maskThresholdMin: 0.5
                maskSpreadAtMin: 1
                autoPaddingEnabled: false
                maskSource: largeMask
                opacity: Math.min(1, motion.cornerBlend / .35)
            }
        }
    }
}
