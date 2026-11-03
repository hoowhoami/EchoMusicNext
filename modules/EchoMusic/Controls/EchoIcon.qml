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
    // Scale the already aligned raster, without re-quantizing its geometry or
    // requesting another SVG texture at every frame of hover feedback.
    property real visualScale: 1
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
    // layout carries the icon; the density divisor includes the ancestor scale
    // chain because the compensation is applied in local coordinates. Feedback
    // scales the inner surface after alignment; layout zoom remains tracked.
    readonly property bool snapEnabled: EchoPixelSnap.enabled
    readonly property point snapScene: EchoPixelSnap.scenePosition
    readonly property real snapDpr: EchoPixelSnap.devicePixelRatio
    readonly property real snapDensityX: EchoPixelSnap.densityX
    readonly property real snapDensityY: EchoPixelSnap.densityY
    Item {
        id: alignedSurface
        objectName: "iconAlignedSurface"
        scale: control.visualScale
        x: control.snapEnabled ? (Math.round(control.snapScene.x * control.snapDpr) - control.snapScene.x * control.snapDpr) / control.snapDensityX : 0
        y: control.snapEnabled ? (Math.round(control.snapScene.y * control.snapDpr) - control.snapScene.y * control.snapDpr) / control.snapDensityY : 0
        width: control.snapEnabled ? Math.round(control.width * control.snapDensityX) / control.snapDensityX : control.width
        height: control.snapEnabled ? Math.round(control.height * control.snapDensityY) / control.snapDensityY : control.height
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
