pragma ComponentBehavior: Bound
import QtQuick
import EchoMusic.Controls

// Border for positive, axis-aligned UI scaling. Both the physical backing
// image and the visible bounds use the same integer pixel box. Moving the
// surface reuses its texture; only size, density or appearance repaints it.
Item {
    id: frame
    property color strokeColor: "black"
    property color fillColor: "transparent"
    property real radius: 0
    property real thickness: 1
    property real dashLength: 3
    property bool resourceActive: true
    readonly property bool snap: EchoPixelSnap.enabled
    readonly property point scene: EchoPixelSnap.scenePosition
    readonly property real dpr: EchoPixelSnap.devicePixelRatio
    readonly property real densityX: EchoPixelSnap.densityX
    readonly property real densityY: EchoPixelSnap.densityY
    Loader {
        active: frame.resourceActive && frame.visible && frame.Window.window !== null && frame.Window.window.visible && frame.Window.window.visibility !== Window.Minimized
        sourceComponent: EchoDashedFrameSurface {
            id: outline
            objectName: "dashedFrameAlignedSurface"
            x: frame.snap ? (Math.round(frame.scene.x * frame.dpr) - frame.scene.x * frame.dpr) / frame.densityX : 0
            y: frame.snap ? (Math.round(frame.scene.y * frame.dpr) - frame.scene.y * frame.dpr) / frame.densityY : 0
            width: frame.snap ? Math.max(0, Math.round(frame.scene.x * frame.dpr + frame.width * frame.densityX) - Math.round(frame.scene.x * frame.dpr)) / frame.densityX : frame.width
            height: frame.snap ? Math.max(0, Math.round(frame.scene.y * frame.dpr + frame.height * frame.densityY) - Math.round(frame.scene.y * frame.dpr)) / frame.densityY : frame.height
            pixelSize: frame.snap ? Qt.size(Math.max(1, Math.round(width * frame.densityX)), Math.max(1, Math.round(height * frame.densityY))) : Qt.size(0, 0)
            thickness: frame.thickness <= 0 ? 0 : frame.snap ? Math.max(1, Math.round(frame.thickness * frame.densityY)) / frame.densityY : frame.thickness
            radius: frame.radius
            dashLength: frame.dashLength
            strokeColor: frame.strokeColor
            backgroundColor: frame.fillColor
        }
    }
}
