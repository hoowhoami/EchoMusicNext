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
    // Render one continuous stroke (an even-odd ring with Qt border radii)
    // instead of dashes; takes precedence over dashLength when set.
    property bool solid: false
    // Keep the stroke band inside the logical rectangle so container clips
    // (ListView viewports, layer bounds) can never cut a stroke column that
    // straddles their rounded fractional edge; the fill still covers the
    // whole snapped box. Only top-level floating surfaces opt out. The four
    // insets stay scalar: packing them into one rect would tempt readers
    // into QRectF::right()/bottom(), which are absolute edges (x + width).
    property bool strokeInside: true
    readonly property bool strokeInsetActive: frame.snap && frame.strokeInside
    readonly property real strokeInsetLeft: frame.strokeInsetActive
        ? Math.ceil(frame.scene.x * frame.dpr) - Math.floor(frame.scene.x * frame.dpr) : 0
    readonly property real strokeInsetTop: frame.strokeInsetActive
        ? Math.ceil(frame.scene.y * frame.dpr) - Math.floor(frame.scene.y * frame.dpr) : 0
    readonly property real strokeInsetRight: frame.strokeInsetActive
        ? Math.ceil(frame.scene.x * frame.dpr + frame.width * frame.densityX)
            - Math.floor(frame.scene.x * frame.dpr + frame.width * frame.densityX) : 0
    readonly property real strokeInsetBottom: frame.strokeInsetActive
        ? Math.ceil(frame.scene.y * frame.dpr + frame.height * frame.densityY)
            - Math.floor(frame.scene.y * frame.dpr + frame.height * frame.densityY) : 0
    // Floating surfaces whose fill is drawn by an independent unsnapped
    // painter (smooth-corner dialogs) keep this false so the stroke tracks
    // the fill instead of the device grid.
    property bool pixelAligned: true
    property bool resourceActive: true
    readonly property bool snap: EchoPixelSnap.enabled && frame.pixelAligned
    readonly property point scene: EchoPixelSnap.scenePosition
    readonly property real dpr: EchoPixelSnap.devicePixelRatio
    readonly property real densityX: EchoPixelSnap.densityX
    readonly property real densityY: EchoPixelSnap.densityY
    Loader {
        active: frame.resourceActive && frame.visible && frame.Window.window !== null && frame.Window.window.visible && frame.Window.window.visibility !== Window.Minimized
        sourceComponent: EchoDashedFrameSurface {
            id: outline
            objectName: "dashedFrameAlignedSurface"
            // Snap outward (floor/ceil): the snapped box always covers the
            // logical rectangle, so a translucent stroke never leaves a
            // sliver of shadow or window background exposed beside the fill
            // (round-to-nearest could indent the box by up to half a pixel).
            x: frame.snap ? (Math.floor(frame.scene.x * frame.dpr) - frame.scene.x * frame.dpr) / frame.densityX : 0
            y: frame.snap ? (Math.floor(frame.scene.y * frame.dpr) - frame.scene.y * frame.dpr) / frame.densityY : 0
            width: frame.snap ? Math.max(0, Math.ceil(frame.scene.x * frame.dpr + frame.width * frame.densityX) - Math.floor(frame.scene.x * frame.dpr)) / frame.densityX : frame.width
            height: frame.snap ? Math.max(0, Math.ceil(frame.scene.y * frame.dpr + frame.height * frame.densityY) - Math.floor(frame.scene.y * frame.dpr)) / frame.densityY : frame.height
            pixelSize: frame.snap ? Qt.size(Math.max(1, Math.round(width * frame.densityX)), Math.max(1, Math.round(height * frame.densityY))) : Qt.size(0, 0)
            thickness: frame.thickness <= 0 ? 0 : frame.snap ? Math.max(1, Math.round(frame.thickness * frame.densityY)) / frame.densityY : frame.thickness
            radius: frame.radius
            dashLength: frame.dashLength
            solid: frame.solid
            strokeInsetLeft: frame.strokeInsetLeft
            strokeInsetTop: frame.strokeInsetTop
            strokeInsetRight: frame.strokeInsetRight
            strokeInsetBottom: frame.strokeInsetBottom
            strokeColor: frame.strokeColor
            backgroundColor: frame.fillColor
        }
    }
}
