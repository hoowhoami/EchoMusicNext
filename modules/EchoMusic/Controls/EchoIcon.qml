pragma ComponentBehavior: Bound
import QtQuick
import EchoMusic.Theme

Item {
    id: control
    property string name: "music"
    // Callers supply their actual surface/state foreground, like currentColor.
    property color color: Theme.text
    property bool resourceActive: true
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
        item: control
        active: control.resourceActive && control.visible
        source: image.source
    }
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
