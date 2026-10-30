pragma ComponentBehavior: Bound
import QtQuick
import QtQml.Models
import EchoMusic.Theme
import EchoMusic.Controls

// Viewport-owned presentation only. The caller supplies a single-row snapshot
// and commits its own model; this controller never owns persistent list order.
Item {
    id: control
    required property ListView view
    required property AbstractItemModel sourceModel
    required property Component dragContent
    property var snapshotForRow: null
    property real rowHeight: 52
    readonly property real rowStride: rowHeight + view.spacing
    property real handleLeft: 0
    property real handleRight: 0
    property bool resourceActive: true
    property bool blocked: false
    readonly property bool usable: resourceActive && !blocked && enabled && visible && sourceModel !== null && Window.window !== null && Window.window.visible && Window.window.visibility !== Window.Minimized
    // idle -> dragging -> settling -> idle; any external invalidation cancels.
    property string phase: "idle"
    readonly property bool dragging: phase === "dragging"
    readonly property bool settling: phase === "settling"
    readonly property bool busy: dragging || settling
    property bool committing: false
    property int fromIndex: -1
    property int toIndex: -1
    property int generation: 0
    property var snapshot: null
    property point scenePointer: Qt.point(0, 0)
    property real pointerX: -1
    property real pointerY: -1
    property real pressOffset: 0
    property real ghostY: 0
    property real slotY: 0
    property real ghostScale: 1
    property real ghostInset: 4
    property bool touchReady: false
    property bool edgeArmed: false
    readonly property real edgeBand: Math.min(36, height / 4)
    readonly property real maximumScrollSpeed: 180
    readonly property bool pointerInside: pointerX >= 0 && pointerX <= width && pointerY >= 0 && pointerY <= height
    readonly property bool pointerHeld: mouseDrag.active && (mouseDrag.centroid.pressedButtons & Qt.LeftButton) !== 0 || touchDrag.active && touchPoint.active
    readonly property int edgeDirection: !usable || !dragging || !pointerHeld || !pointerInside || view.contentHeight <= view.height ? 0 : pointerY < edgeBand && view.contentY > view.originY + 0.5 ? -1 : pointerY > height - edgeBand && view.contentY < view.originY + view.contentHeight - view.height - 0.5 ? 1 : 0
    readonly property real edgeDepth: edgeDirection < 0 ? Math.max(0, Math.min(1, (edgeBand - pointerY) / edgeBand)) : edgeDirection > 0 ? Math.max(0, Math.min(1, (pointerY - height + edgeBand) / edgeBand)) : 0
    readonly property real edgeSpeed: edgeArmed ? edgeDirection * maximumScrollSpeed * edgeDepth * edgeDepth : 0
    readonly property bool autoScrolling: scroll.running
    signal started(int index)
    signal moveRequested(int from, int to)
    signal canceled
    width: view.width
    height: view.height
    z: 20
    containmentMask: handleArea
    onUsableChanged: if (!usable)
        cancel()
    onSourceModelChanged: cancel()
    onWidthChanged: if (busy)
        cancel()
    onHeightChanged: if (busy)
        cancel()
    onEdgeDirectionChanged: {
        edgeArmed = false;
        edgeDelay.stop();
        if (edgeDirection !== 0)
            edgeDelay.restart();
    }
    Component.onDestruction: cancel()

    function cancel() {
        const wasBusy = busy;
        ++generation;
        phase = "idle";
        landing.stop();
        lift.stop();
        slotShift.complete();
        edgeDelay.stop();
        edgeArmed = false;
        touchReady = false;
        fromIndex = -1;
        toIndex = -1;
        snapshot = null;
        if (wasBusy)
            canceled();
    }
    function start(scenePress, scenePosition) {
        cancel();
        if (!usable || view.count < 2)
            return;
        const press = view.mapFromItem(null, scenePress.x, scenePress.y);
        const index = view.indexAt(view.width / 2, view.contentY + press.y);
        const row = view.itemAtIndex(index);
        if (!row)
            return;
        view.cancelFlick();
        fromIndex = toIndex = index;
        snapshot = snapshotForRow ? snapshotForRow(row) : ({});
        pressOffset = view.contentY + press.y - row.y;
        ghostScale = 1;
        ghostInset = 4;
        scenePointer = scenePosition;
        refreshPointer();
        slotY = row.mapToItem(view, 0, 0).y;
        phase = "dragging";
        if (Theme.store.motionEnabled)
            lift.restart();
        updateTarget();
        started(index);
    }
    function updatePointer(position) {
        if (!dragging)
            return;
        scenePointer = position;
        refreshPointer();
    }
    function refreshPointer() {
        // Recompute in the viewport, never in a translated/recycled row. The
        // unclamped pointer decides scrolling; only the visual card is clamped.
        const point = view.mapFromItem(null, scenePointer.x, scenePointer.y);
        pointerX = point.x;
        pointerY = point.y;
        ghostY = Math.max(0, Math.min(Math.max(0, height - rowHeight), pointerY - pressOffset));
        updateTarget();
    }
    function updateTarget() {
        if (!dragging || !pointerInside)
            return;
        const position = view.contentY + ghostY + rowHeight / 2;
        let index = view.indexAt(view.width / 2, position);
        // A short list leaves empty viewport space below its last row. Dropping
        // there still means "move to the end", without enabling auto-scroll.
        if (index < 0 && position >= view.originY + view.contentHeight)
            index = view.count - 1;
        if (index >= 0) {
            toIndex = index;
            const target = view.itemAtIndex(index);
            if (target)
                slotY = target.mapToItem(view, 0, 0).y;
        }
    }
    function release() {
        if (!dragging)
            return;
        lift.stop();
        phase = "settling"; // Stops scrolling in the release event itself.
        const token = generation;
        Qt.callLater(() => {
            if (token !== generation || !settling || !usable)
                return;
            const target = view.itemAtIndex(toIndex);
            const point = target ? target.mapToItem(view, 0, 0) : Qt.point(0, ghostY);
            if (!Theme.store.motionEnabled) {
                finish();
                return;
            }
            landY.to = point.y;
            landing.restart();
        });
    }
    function finish() {
        if (!settling || !usable)
            return;
        const from = fromIndex;
        const to = toIndex;
        committing = true;
        if (from !== to)
            moveRequested(from, to);
        // Complete the native move before removing the visual displacement;
        // otherwise the same movement can be rendered a second time at drop.
        view.forceLayout();
        phase = "idle";
        committing = false;
        fromIndex = toIndex = -1;
        snapshot = null;
    }
    function offsetFor(index) {
        if (!busy)
            return 0;
        if (index === fromIndex)
            return (toIndex - fromIndex) * rowStride;
        if (fromIndex < toIndex && index > fromIndex && index <= toIndex)
            return -rowStride;
        if (toIndex < fromIndex && index >= toIndex && index < fromIndex)
            return rowStride;
        return 0;
    }
    function handlerActive(handler) {
        if (handler.active)
            start(handler.centroid.scenePressPosition, handler.centroid.scenePosition);
        else
            release();
    }
    Connections {
        target: control.sourceModel
        function onModelAboutToBeReset() {
            control.cancel();
        }
        function onRowsAboutToBeInserted() {
            control.cancel();
        }
        function onRowsAboutToBeRemoved() {
            control.cancel();
        }
        function onRowsAboutToBeMoved() {
            if (!control.committing)
                control.cancel();
        }
        function onLayoutAboutToBeChanged() {
            control.cancel();
        }
    }
    Connections {
        target: control.view
        function onContentYChanged() {
            if (control.dragging)
                control.refreshPointer();
        }
    }
    Connections {
        target: control.Window.window
        function onActiveChanged() {
            if (!control.Window.window || !control.Window.window.active)
                control.cancel();
        }
    }
    Connections {
        target: Theme.store
        function onChanged() {
            if (!Theme.store.motionEnabled) {
                slotShift.complete();
                lift.stop();
                if (control.dragging)
                    control.ghostScale = 1;
            }
            if (control.settling && !Theme.store.motionEnabled) {
                landing.stop();
                control.finish();
            }
        }
    }
    Item {
        id: handleArea
        x: control.handleLeft
        width: Math.max(0, control.width - x - control.handleRight)
        height: control.height
        DragHandler {
            id: mouseDrag
            objectName: "listReorderMouse"
            enabled: control.usable && !control.settling && control.view.count > 1
            target: null
            acceptedButtons: Qt.LeftButton
            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad | PointerDevice.Stylus
            cursorShape: Qt.ArrowCursor
            xAxis.enabled: false
            grabPermissions: PointerHandler.CanTakeOverFromItems | PointerHandler.CanTakeOverFromHandlersOfDifferentType | PointerHandler.ApprovesCancellation
            onActiveChanged: control.handlerActive(mouseDrag)
            onCentroidChanged: if (active && (centroid.pressedButtons & Qt.LeftButton))
                control.updatePointer(centroid.scenePosition)
            onCanceled: control.cancel()
        }
        TapHandler {
            acceptedDevices: PointerDevice.TouchScreen
            enabled: control.usable && !control.settling
            longPressThreshold: 0.18
            dragThreshold: 8
            onLongPressed: control.touchReady = true
        }
        PointHandler {
            id: touchPoint
            acceptedDevices: PointerDevice.TouchScreen
            enabled: control.usable
            onActiveChanged: if (!active)
                control.touchReady = false
            onCanceled: control.cancel()
        }
        DragHandler {
            id: touchDrag
            enabled: control.usable && !control.settling && control.view.count > 1
            target: null
            acceptedDevices: PointerDevice.TouchScreen
            dragThreshold: control.touchReady ? 8 : 30000
            xAxis.enabled: false
            grabPermissions: PointerHandler.CanTakeOverFromItems | PointerHandler.CanTakeOverFromHandlersOfDifferentType | PointerHandler.ApprovesCancellation
            onActiveChanged: control.handlerActive(touchDrag)
            onCentroidChanged: if (active)
                control.updatePointer(centroid.scenePosition)
            onCanceled: control.cancel()
        }
    }
    Timer {
        id: edgeDelay
        interval: 180
        onTriggered: control.edgeArmed = control.dragging && control.edgeDirection !== 0
    }
    Timer {
        id: scroll
        interval: 16
        repeat: true
        running: control.usable && control.dragging && control.edgeArmed && control.edgeDirection !== 0
        onTriggered: {
            control.refreshPointer();
            if (!control.pointerHeld || !control.pointerInside || control.edgeSpeed === 0)
                return;
            const min = control.view.originY;
            const max = Math.max(min, min + control.view.contentHeight - control.view.height);
            control.view.contentY = Math.max(min, Math.min(max, control.view.contentY + control.edgeSpeed * interval / 1000));
        }
    }
    NumberAnimation {
        id: lift
        target: control
        property: "ghostScale"
        to: 1.015
        duration: Theme.fastDuration
        easing.type: Easing.BezierSpline
        easing.bezierCurve: Theme.easeEnter
    }
    ParallelAnimation {
        id: landing
        NumberAnimation {
            id: landY
            target: control
            property: "ghostY"
            duration: Theme.normalDuration
            easing.type: Easing.BezierSpline
            easing.bezierCurve: Theme.easeStandard
        }
        NumberAnimation {
            target: control
            property: "ghostScale"
            to: 1
            duration: Theme.normalDuration
            easing.type: Easing.BezierSpline
            easing.bezierCurve: Theme.easeStandard
        }
        NumberAnimation {
            target: control
            property: "ghostInset"
            to: 0
            duration: Theme.normalDuration
            easing.type: Easing.BezierSpline
            easing.bezierCurve: Theme.easeStandard
        }
        onFinished: control.finish()
    }
    Loader {
        active: control.busy
        x: 3
        y: control.slotY + 2
        width: Math.max(0, control.width - 6)
        height: Math.max(0, control.rowHeight - 4)
        sourceComponent: Rectangle {
            radius: Theme.controlRadius
            color: Theme.withAlpha(Theme.floating.accent, 0.045)
            border.width: 1
            border.color: Theme.withAlpha(Theme.floating.accent, 0.18)
        }
        Behavior on y {
            enabled: control.busy && Theme.store.motionEnabled
            NumberAnimation {
                id: slotShift
                duration: Theme.normalDuration
                easing.type: Easing.BezierSpline
                easing.bezierCurve: Theme.easeStandard
            }
        }
    }
    Loader {
        objectName: "listReorderGhostLoader"
        active: control.busy
        x: control.ghostInset
        y: control.ghostY
        width: Math.max(0, control.width - control.ghostInset * 2)
        height: control.rowHeight
        scale: control.ghostScale
        sourceComponent: EchoFloatingSurface {
            objectName: "listReorderGhostSurface"
            frostedEnabled: false
            radius: Theme.controlRadius
            color: Theme.mix(Theme.floating.card, Theme.floating.accent, 0.04)
            border.width: 1
            border.color: Theme.withAlpha(Theme.floating.accent, 0.24)
            Loader {
                anchors.fill: parent
                sourceComponent: control.dragContent
            }
        }
    }
}
