pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Effects
import EchoMusic.Theme

Item {
    id: control
    property url source
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
    EchoImageMetrics {
        id: metrics
        objectName: "coverMetrics"
        item: control
        active: control.resourceActive && control.visible
        source: control.source
        minimumSize: Qt.size(control.minimumDecodeSize, control.minimumDecodeSize)
    }
    component CoverPlaceholder: Rectangle {
        id: surface
        property bool synchronous: false
        property real glyphSize: control.placeholderGlyphSize
        property color glyphColor: control.colors.text
        property real glyphOpacity: control.placeholderOpacity
        color: control.placeholderColor
        EchoIcon {
            // The small bundled glyph must exist on the first flight frame.
            asynchronous: !surface.synchronous
            anchors.centerIn: parent
            width: surface.glyphSize
            height: surface.glyphSize
            opacity: surface.glyphOpacity
            color: surface.glyphColor
            name: control.placeholderGlyph
        }
    }
    CoverPlaceholder {
        id: placeholder
        anchors.fill: parent
        radius: control.cornerRadius
        opacity: control.artworkOpacity
    }
    Image {
        id: cover
        objectName: "coverImage"
        anchors.fill: parent
        source: metrics.resolvedSource
        asynchronous: true
        sourceSize: metrics.decodeSize
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
