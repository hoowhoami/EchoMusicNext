pragma ComponentBehavior: Bound
import QtQuick
import EchoMusic.Theme

Item {
    id: control
    property string name: "music"
    // Callers supply their actual surface/state foreground, like currentColor.
    property color color: Theme.text
    property bool resourceActive: true
    // A short glyph scale transition can rasterize at its stable endpoint
    // density instead of reloading the SVG on every animation frame.
    property Item rasterizationItem: control
    property alias asynchronous: image.asynchronous
    readonly property alias status: image.status
    // Rasterize glyphs 1:1 at device pixels: the artwork bucket floor (32px)
    // would make a small icon render at ~2x its display size, and the bilinear
    // downscale then aliases line-art strokes (circles and hearts read as
    // staircases on a fractional-DPI screen).
    readonly property int rasterSize: Math.max(1, metrics.deviceSize.width, metrics.deviceSize.height)
    function visualCenter(): point {
        return EchoIconSource.visualCenter(control.name, Qt.size(control.width, control.height));
    }
    implicitWidth: 20
    implicitHeight: 20
    EchoImageMetrics {
        id: metrics
        item: control.rasterizationItem
        active: control.resourceActive && control.visible
        source: image.source
    }
    // Device-pixel alignment, mirroring EchoCover: a glyph whose physical
    // position rides a half-device-pixel boundary flips a whole pixel on
    // every re-quantization. The attached scene position re-syncs wherever
    // layout carries the icon. Callers that animate `scale` still snap at
    // their rest state.
    readonly property point snapScene: EchoPixelSnap.scenePosition
    readonly property real snapDpr: EchoPixelSnap.devicePixelRatio
    Item {
        id: alignedSurface
        objectName: "iconAlignedSurface"
        x: Math.round(control.snapScene.x * control.snapDpr) / control.snapDpr
            - control.snapScene.x
        y: Math.round(control.snapScene.y * control.snapDpr) / control.snapDpr
            - control.snapScene.y
        width: Math.round(control.width * control.snapDpr) / control.snapDpr
        height: Math.round(control.height * control.snapDpr) / control.snapDpr
        Image {
            id: image
            objectName: "iconImage"
            anchors.fill: parent
            source: control.resourceActive && control.visible ? EchoIconSource.urlFor(control.name, control.color) : ""
            sourceSize: Qt.size(control.rasterSize, control.rasterSize)
            fillMode: Image.PreserveAspectFit
            asynchronous: true
        }
    }
}
