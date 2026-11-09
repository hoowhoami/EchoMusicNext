pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Shapes
import EchoMusic.Controls

// The native path has no Canvas logical-pixel backing-size rounding. Keep
// placement on the outer item and align only the owned drawing surface.
Item {
    id: arrow
    property color fillColor: "black"
    property bool above: false
    property bool resourceActive: true
    readonly property bool snap: EchoPixelSnap.enabled
    readonly property point scene: EchoPixelSnap.scenePosition
    readonly property real dpr: EchoPixelSnap.devicePixelRatio
    readonly property real densityX: EchoPixelSnap.densityX
    readonly property real densityY: EchoPixelSnap.densityY
    Loader {
        active: arrow.resourceActive && arrow.visible && arrow.Window.window !== null && arrow.Window.window.visible && arrow.Window.window.visibility !== Window.Minimized
        sourceComponent: Shape {
            id: path
            objectName: "popupArrowAlignedSurface"
            preferredRendererType: Shape.CurveRenderer
            x: arrow.snap ? (Math.round(arrow.scene.x * arrow.dpr) - arrow.scene.x * arrow.dpr) / arrow.densityX : 0
            y: arrow.snap ? (Math.round(arrow.scene.y * arrow.dpr) - arrow.scene.y * arrow.dpr) / arrow.densityY : 0
            width: arrow.snap ? Math.max(0, Math.round(arrow.scene.x * arrow.dpr + arrow.width * arrow.densityX) - Math.round(arrow.scene.x * arrow.dpr)) / arrow.densityX : arrow.width
            height: arrow.snap ? Math.max(0, Math.round(arrow.scene.y * arrow.dpr + arrow.height * arrow.densityY) - Math.round(arrow.scene.y * arrow.dpr)) / arrow.densityY : arrow.height
            ShapePath {
                strokeWidth: -1
                fillColor: arrow.fillColor
                startX: 0
                startY: arrow.above ? 0 : path.height
                PathLine {
                    x: path.width / 2
                    y: arrow.above ? path.height : 0
                }
                PathLine {
                    x: path.width
                    y: arrow.above ? 0 : path.height
                }
                PathLine {
                    x: 0
                    y: arrow.above ? 0 : path.height
                }
            }
        }
    }
}
