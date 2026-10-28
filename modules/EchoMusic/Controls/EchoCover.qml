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
    // Public static artwork for short, frozen compositor transitions.
    readonly property Item transitionArtwork: cover.status === Image.Ready ? cover : placeholder
    // A flight applies its own corner mask. Instantiate this square recipe only
    // during that flight, so a small cover's rounded corners are not baked in.
    readonly property Component transitionPlaceholder: Component {
        CoverPlaceholder {
            synchronous: true
        }
    }
    property string placeholderGlyph: "music"
    property real placeholderGlyphSize: 20
    property real placeholderOpacity: 0.3
    // Pooled/inactive delegates release decoded images and their mask textures.
    property bool resourceActive: true
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
        active: control.resourceActive && control.visible
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
        active: control.resourceActive && control.visible
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
    CoverPlaceholder {
        id: placeholder
        objectName: "coverPlaceholder"
        anchors.fill: parent
        radius: control.cornerRadius
        opacity: control.artworkOpacity
        // Transparent pixels in a real avatar must not reveal a fallback user
        // icon. Keep only the existing base surface beneath decoded artwork.
        glyphVisible: cover.status !== Image.Ready
    }
    Image {
        id: cover
        objectName: "coverImage"
        anchors.fill: parent
        source: metrics.resolvedSource
        asynchronous: true
        sourceSize: metrics.decodeSize
        onStatusChanged: {
            if (status === Image.Error)
                coverSource.reject(metrics.source);
        }
        fillMode: Image.PreserveAspectCrop
        visible: false
    }
    Loader {
        objectName: "coverMaskOwner"
        opacity: control.artworkOpacity
        anchors.fill: parent
        active: control.resourceActive && cover.status === Image.Ready
        // The effect and its texture provider share one lifetime. A pooled
        // delegate must not leave an effect referencing a destroyed mask.
        sourceComponent: Item {
            ShaderEffectSource {
                id: capture
                objectName: "coverCapture"
                sourceItem: cover
                sourceRect: Qt.rect(0, 0, control.width, control.height)
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
