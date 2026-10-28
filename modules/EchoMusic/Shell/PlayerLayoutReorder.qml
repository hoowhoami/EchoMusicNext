pragma ComponentBehavior: Bound
import QtQuick
import EchoMusic.Shell

// Presentation-only drag state. Keeping the source in its original model lets
// its pointer grab survive cross-zone previews; the registry changes on release.
QtObject {
    id: state
    required property ShellActionModel actions
    required property Item hostItem
    property bool resourceActive: true
    property bool rebasing: false
    property var panes: []
    property string key: ""
    property int sourceZone: -1
    property int sourceIndex: -1
    property point destination: Qt.point(-1, -1)
    readonly property int targetZone: destination.x
    readonly property int targetIndex: destination.y
    readonly property bool dragging: key.length > 0
    property int epoch: 0
    property var releasePositions: ({})
    property string releasedKey: ""
    onResourceActiveChanged: if (!resourceActive)
        cancel()

    function registerPane(pane: PlayerLayoutZone): void {
        panes = panes.concat([pane]);
    }
    function unregisterPane(pane: PlayerLayoutZone): void {
        cancel();
        panes = panes.filter(item => item !== pane);
    }
    function begin(actionKey: string, zone: int, index: int): void {
        cancel();
        sourceZone = zone;
        sourceIndex = index;
        destination = Qt.point(zone, index);
        key = actionKey;
    }
    function update(position: point): void {
        if (!dragging)
            return;
        for (const pane of panes) {
            const index = pane.insertionIndex(position);
            if (index >= 0) {
                destination = Qt.point(pane.zone, index);
                return;
            }
        }
        destination = Qt.point(-1, -1);
    }
    function visualIndex(zone: int, index: int, actionKey: string): int {
        if (!dragging || rebasing || targetZone < 0)
            return index;
        if (zone === sourceZone && actionKey === key)
            return targetZone === sourceZone ? targetIndex : index;
        let result = zone === sourceZone && index > sourceIndex ? index - 1 : index;
        if (zone === targetZone && result >= targetIndex)
            result++;
        return result;
    }
    function finish(position: point): void {
        update(position);
        const pane = panes.find(item => item.zone === targetZone);
        if (!dragging || !pane || targetZone === sourceZone && targetIndex === sourceIndex) {
            cancel();
            return;
        }
        const anchor = pane.insertionAnchor(targetIndex);
        if (!anchor) {
            cancel();
            return;
        }
        const revision = ++epoch;
        const actionKey = key, zone = targetZone;
        Qt.callLater(function () {
            if (!state.resourceActive || revision !== state.epoch || actionKey !== state.key)
                return;
            const positions = {};
            for (const item of state.panes)
                item.capturePositions(positions);
            // The newly inserted chip settles from the pointer, not from its
            // old zone. Badge and icon remain one fixed slot during this move.
            const point = state.hostItem.mapFromItem(null, position.x, position.y);
            positions[actionKey] = Qt.point(point.x - 17, point.y - 47);
            state.releasePositions = positions;
            state.releasedKey = actionKey;
            state.rebasing = true;
            state.actions.moveInPlayerZone(actionKey, zone, anchor.key, anchor.after);
            for (const item of state.panes)
                item.applyPositions();
            state.key = "";
            state.destination = Qt.point(-1, -1);
            state.rebasing = false;
            for (const item of state.panes)
                item.settlePositions();
        });
    }
    function takePosition(actionKey: string): var {
        const point = releasePositions[actionKey];
        if (point !== undefined)
            delete releasePositions[actionKey];
        return point;
    }
    function cancel(): void {
        epoch++;
        key = "";
        destination = Qt.point(-1, -1);
        releasePositions = ({});
        releasedKey = "";
        for (const pane of panes)
            pane.settlePositions();
    }
    property Connections registryEvents: Connections {
        target: state.dragging && !state.rebasing ? state.actions : null
        function onLayoutChanged() {
            state.cancel();
        }
        function onRowsRemoved() {
            state.cancel();
        }
    }
    // Snapshots only bridge the next rendered layout. They must never animate
    // a delegate that is created much later by horizontal virtualization.
    property Connections rendered: Connections {
        target: Object.keys(state.releasePositions).length > 0 && state.hostItem ? state.hostItem.Window.window : null
        function onFrameSwapped() {
            state.releasePositions = ({});
        }
    }
}
