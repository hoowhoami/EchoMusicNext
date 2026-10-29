pragma ComponentBehavior: Bound
import QtQuick
import EchoMusic.Theme
import EchoMusic.Controls

// ListView owns this root's position. Only bounded visual content moves during
// preview; the model commits once, after the floating card lands in its slot.
Item {
    id: row
    required property int index
    required property EchoListReorder reorder
    property bool pooled: false
    default property alias rowContent: body.data
    readonly property bool dragged: reorder.busy && index === reorder.fromIndex
    readonly property bool contentActive: reorder.usable && !pooled && !dragged
    readonly property bool animateShift: reorder.busy && visible && !pooled && Theme.store.motionEnabled
    width: reorder.view.width
    height: reorder.rowHeight
    ListView.onPooled: pooled = true
    ListView.onReused: pooled = false
    onAnimateShiftChanged: if (!animateShift)
        shift.complete()
    property Item presentation: Item {
        id: body
        parent: row
        objectName: "reorderRowBody"
        width: row.width
        height: row.height
        transform: Translate {
            y: row.reorder.offsetFor(row.index)
            Behavior on y {
                enabled: row.animateShift && !row.dragged
                NumberAnimation {
                    id: shift
                    duration: Theme.normalDuration
                    easing.type: Easing.BezierSpline
                    easing.bezierCurve: Theme.easeStandard
                }
            }
        }
        opacity: row.dragged ? 0 : 1
    }
}
