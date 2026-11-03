pragma ComponentBehavior: Bound
import QtQuick
import EchoMusic.Controls

// A divider that renders exactly one device pixel. Fractional DPRs (Windows
// 125%) turn a 1-DIP line into 1.25 device pixels whose phase differs with
// the accumulated layout offsets, so some rows land whole while others
// straddle two and read thicker. EchoPixelSnap tracks the line's scene
// position (including the ancestor scale chain) and the density; the inner
// line parks on a whole device row and measures exactly one device pixel,
// compensating in its own local coordinates - writing back to the
// layout-managed outer item would feed back.
Item {
    id: divider
    property int orientation: Qt.Horizontal
    property alias lineColor: line.color
    implicitHeight: divider.horizontal ? 1 : 0
    implicitWidth: divider.horizontal ? 0 : 1
    readonly property bool horizontal: orientation === Qt.Horizontal
    readonly property bool snap: EchoPixelSnap.enabled
    readonly property point scene: EchoPixelSnap.scenePosition
    readonly property real dpr: EchoPixelSnap.devicePixelRatio
    readonly property real densityX: EchoPixelSnap.densityX
    readonly property real densityY: EchoPixelSnap.densityY
    Rectangle {
        id: line
        x: !divider.horizontal && divider.snap
                ? (Math.round(divider.scene.x * divider.dpr) - divider.scene.x * divider.dpr)
                        / divider.densityX
                : 0
        y: divider.horizontal && divider.snap
                ? (Math.round(divider.scene.y * divider.dpr) - divider.scene.y * divider.dpr)
                        / divider.densityY
                : 0
        width: divider.horizontal ? parent.width
                                  : divider.snap ? Math.round(1 * divider.densityX) / divider.densityX : 1
        height: divider.horizontal
                ? (divider.snap ? Math.round(1 * divider.densityY) / divider.densityY : 1)
                : parent.height
        color: divider.lineColor
    }
}
