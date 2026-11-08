pragma ComponentBehavior: Bound
import QtQuick
import EchoMusic.Controls

// A divider with an integer device-pixel thickness. Fractional DPRs (Windows
// 125%) turn a 1-DIP line into 1.25 device pixels whose phase differs with
// the accumulated layout offsets, so some rows land whole while others
// straddle two and read thicker. EchoPixelSnap tracks the line's scene
// position (including the ancestor scale chain) and the density; the inner
// line parks on a whole device row and rounds its nominal DIP thickness,
// compensating in its own local coordinates - writing back to the
// layout-managed outer item would feed back.
Item {
    id: divider
    property int orientation: Qt.Horizontal
    property alias lineColor: line.color
    property real thickness: 1
    property bool rounded: false
    implicitHeight: divider.horizontal ? thickness : 0
    implicitWidth: divider.horizontal ? 0 : thickness
    readonly property bool horizontal: orientation === Qt.Horizontal
    readonly property bool snap: EchoPixelSnap.enabled
    readonly property point scene: EchoPixelSnap.scenePosition
    readonly property real dpr: EchoPixelSnap.devicePixelRatio
    readonly property real densityX: EchoPixelSnap.densityX
    readonly property real densityY: EchoPixelSnap.densityY
    Rectangle {
        id: line
        objectName: "dividerAlignedSurface"
        x: divider.snap ? (Math.round(divider.scene.x * divider.dpr) - divider.scene.x * divider.dpr) / divider.densityX : 0
        y: divider.snap ? (Math.round(divider.scene.y * divider.dpr) - divider.scene.y * divider.dpr) / divider.densityY : 0
        width: divider.horizontal ? (divider.snap ? Math.max(0, Math.round(divider.scene.x * divider.dpr + divider.width * divider.densityX) - Math.round(divider.scene.x * divider.dpr)) / divider.densityX : divider.width) : divider.thickness <= 0 ? 0 : divider.snap ? Math.max(1, Math.round(divider.thickness * divider.densityX)) / divider.densityX : divider.thickness
        height: !divider.horizontal ? (divider.snap ? Math.max(0, Math.round(divider.scene.y * divider.dpr + divider.height * divider.densityY) - Math.round(divider.scene.y * divider.dpr)) / divider.densityY : divider.height) : divider.thickness <= 0 ? 0 : divider.snap ? Math.max(1, Math.round(divider.thickness * divider.densityY)) / divider.densityY : divider.thickness
        radius: divider.rounded ? Math.min(width, height) / 2 : 0
    }
}
