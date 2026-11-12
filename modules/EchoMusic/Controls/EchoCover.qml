pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Effects
import EchoMusic.Theme

Item {
    id: control
    property url source
    property string coverScope: "cover"
    property string coverAlt: ""
    property bool circular: false
    property real cornerRadius: circular ? width / 2 : Theme.mediaRadius
    // Hide only artwork during a compositor flight; attached surfaces such as
    // a cover shadow can keep their existing scene-graph/material ownership.
    property real artworkOpacity: 1
    readonly property int status: cover.status
    // Theme-generated artwork represents the same cover while its color is
    // re-rendered. Ordinary/recycled song covers keep their placeholder policy.
    property bool retainWhileLoading: false
    readonly property bool hasArtwork: cover.status === Image.Ready || retainWhileLoading && cover.status === Image.Loading && cover.previouslyReady
    // Public static artwork for short, frozen compositor transitions.
    readonly property Item transitionArtwork: hasArtwork ? cover : placeholder
    // A flight applies its own corner mask. Instantiate this square recipe only
    // during that flight, so a small cover's rounded corners are not baked in.
    readonly property Component transitionPlaceholder: Component {
        CoverPlaceholder {
            synchronous: true
        }
    }
    property string placeholderGlyph: "music"
    // Keep the fallback balanced from compact rows to full playback artwork.
    property real placeholderGlyphSize: Math.max(0, Math.min(width, height)) * 0.5
    property real placeholderOpacity: 0.3
    // Pooled/inactive delegates release decoded images and their mask textures.
    property bool resourceActive: true
    property bool transitionSourceActive: false
    property real revealOpacity: 1
    EchoMotionScope {
        id: revealScope
        target: control
        active: control.resourceActive && control.artworkOpacity > 0
        animations: [reveal]
    }
    NumberAnimation {
        id: reveal
        target: control
        property: "revealOpacity"
        from: 0
        to: 1
        duration: Theme.normalDuration
        easing.type: Easing.BezierSpline
        easing.bezierCurve: Theme.easeStandard
    }
    // An existing visible image may also supply a larger compositor transition.
    // Reserve only that bounded destination, without creating another surface.
    property real minimumDecodeSize: 0
    property ThemeColors colors: Theme.colors
    property color placeholderColor: colors.hover
    implicitWidth: 48
    implicitHeight: 48
    EchoCoverSource {
        id: coverSource
        objectName: "coverSource"
        source: control.source
        active: control.resourceActive && (control.visible || control.transitionSourceActive)
        context: ({
                scope: control.coverScope,
                alt: control.coverAlt,
                size: Math.ceil(Math.max(control.width, control.height) / 32) * 32,
                accentColor: control.colors.accent.toString(),
                accentColorRgb: [Math.round(control.colors.accent.r * 255), Math.round(control.colors.accent.g * 255), Math.round(control.colors.accent.b * 255)]
            })
    }
    EchoImageMetrics {
        id: metrics
        objectName: "coverMetrics"
        item: control
        active: control.resourceActive && (control.visible || control.transitionSourceActive)
        source: coverSource.resolvedSource
        minimumSize: Qt.size(control.minimumDecodeSize, control.minimumDecodeSize)
    }
    component CoverPlaceholder: Rectangle {
        id: surface
        property bool synchronous: false
        property real glyphSize: control.placeholderGlyphSize
        property color glyphColor: control.colors.text
        property real glyphOpacity: control.placeholderOpacity
        // Explicit flight placeholders keep their glyph independently of the
        // source image status. The ordinary fallback opts out once decoded.
        property bool glyphVisible: true
        color: control.placeholderColor
        EchoIcon {
            objectName: "coverPlaceholderGlyph"
            // Resizing keeps the previous raster until the same glyph is ready
            // at its new density, avoiding a blank placeholder between frames.
            retainWhileLoading: true
            // The small bundled glyph must exist on the first flight frame.
            asynchronous: !surface.synchronous
            anchors.centerIn: parent
            width: surface.glyphSize
            height: surface.glyphSize
            opacity: surface.glyphOpacity
            visible: surface.glyphVisible
            color: surface.glyphColor
            name: control.placeholderGlyph
        }
    }
    // Device-pixel alignment. Qt serves the scene at an integral logical size
    // while the native client rect moves in single device pixels, so an edge
    // riding a half-device-pixel boundary flips a whole visible pixel whenever
    // rendering re-quantizes it (the mask below thresholds coverage at 0.5).
    // Parking the artwork surface on whole device pixels — position and size —
    // removes the fraction that rounding feeds on. Scene position is converted
    // with the window DPR, but the compensation is written in this item's local
    // coordinates, so the width/height divisor also includes the ancestor scale
    // chain (density = DPR × scale). The attached properties re-sync wherever
    // layout carries this cover; callers that animate `scale` still snap at
    // their rest state.
    readonly property bool snapEnabled: EchoPixelSnap.enabled
    readonly property point snapScene: EchoPixelSnap.scenePosition
    readonly property real snapDpr: EchoPixelSnap.devicePixelRatio
    readonly property real snapDensityX: EchoPixelSnap.densityX
    readonly property real snapDensityY: EchoPixelSnap.densityY
    Item {
        id: alignedSurface
        objectName: "coverAlignedSurface"
        x: control.snapEnabled ? (Math.round(control.snapScene.x * control.snapDpr) - control.snapScene.x * control.snapDpr) / control.snapDensityX : 0
        y: control.snapEnabled ? (Math.round(control.snapScene.y * control.snapDpr) - control.snapScene.y * control.snapDpr) / control.snapDensityY : 0
        width: control.snapEnabled ? Math.round(control.width * control.snapDensityX) / control.snapDensityX : control.width
        height: control.snapEnabled ? Math.round(control.height * control.snapDensityY) / control.snapDensityY : control.height
        CoverPlaceholder {
            id: placeholder
            objectName: "coverPlaceholder"
            anchors.fill: parent
            radius: control.cornerRadius
            opacity: control.artworkOpacity
            // Transparent pixels in a real avatar must not reveal a fallback user
            // icon. Keep only the existing base surface beneath decoded artwork.
            glyphVisible: !control.hasArtwork
        }
        Image {
            id: cover
            objectName: "coverImage"
            anchors.fill: parent
            source: metrics.resolvedSource
            asynchronous: true
            retainWhileLoading: control.retainWhileLoading
            property bool previouslyReady: false
            property bool revealPending: false
            onSourceChanged: {
                reveal.stop();
                control.revealOpacity = 1;
                revealPending = source.toString().length > 0;
            }
            sourceSize: metrics.decodeSize
            onStatusChanged: {
                if (status === Image.Ready) {
                    previouslyReady = true;
                    if (revealPending && !control.retainWhileLoading && revealScope.allowed)
                        reveal.restart();
                    revealPending = false;
                } else if (status !== Image.Loading)
                    previouslyReady = false;
                if (status === Image.Error)
                    coverSource.reject(metrics.source);
            }
            fillMode: Image.PreserveAspectCrop
            visible: false
        }
        Loader {
            objectName: "coverMaskOwner"
            opacity: control.artworkOpacity * control.revealOpacity
            anchors.fill: parent
            active: control.resourceActive && control.hasArtwork
            // The effect and its texture provider share one lifetime. A pooled
            // delegate must not leave an effect referencing a destroyed mask.
            sourceComponent: Item {
                ShaderEffectSource {
                    id: capture
                    objectName: "coverCapture"
                    sourceItem: cover
                    sourceRect: Qt.rect(0, 0, alignedSurface.width, alignedSurface.height)
                    textureSize: metrics.pixelSize
                    visible: false
                    live: true
                    recursive: false
                }
                Item {
                    id: mask
                    objectName: "coverRoundedMask"
                    anchors.fill: parent
                    visible: false
                    layer.enabled: true
                    layer.smooth: true
                    layer.textureSize: metrics.pixelSize
                    Rectangle {
                        anchors.fill: parent
                        radius: control.cornerRadius
                        antialiasing: true
                        color: "white"
                    }
                }
                MultiEffect {
                    objectName: "coverMaskEffect"
                    anchors.fill: parent
                    source: capture
                    // Default thresholds turn the mask's fractional coverage into
                    // a hard edge. Preserve Qt Rectangle AA at every cover size.
                    maskEnabled: true
                    maskThresholdMin: 0.5
                    maskSpreadAtMin: 1
                    autoPaddingEnabled: false
                    maskSource: mask
                }
            }
        }
    }
}
