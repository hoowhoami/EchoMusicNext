pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import EchoMusic.Controls
import EchoMusic.Theme

Item {
    id: handle
    required property string actionKey
    required property string glyph
    objectName: "shellDragHandle-" + actionKey
    // A toolbar button keeps native click/keyboard semantics; only an actual
    // reorder gesture takes its grab, across the complete button rectangle.
    property T.AbstractButton button: null
    readonly property bool dragging: ghost.Drag.active
    property string dragGroup: "titlebar-actions"
    // Optional unclipped layer in the same popup scene. Scrolling/list clips
    // would otherwise hide the ghost between drop targets.
    property Item dragLayer: null
    property bool resourceActive: true
    property bool gestureArmed: false
    property color foreground: Theme.floatingText
    property real iconOpacity: 1
    readonly property bool motionActive: resourceActive && visible && Window.window !== null && Window.window.visible && Window.window.visibility !== Window.Minimized && Theme.store.motionEnabled
    property real buttonIconOpacity: dragging ? 0.25 : iconOpacity
    Behavior on buttonIconOpacity {
        enabled: handle.button !== null && handle.motionActive
        NumberAnimation {
            id: opacityAnimation
            duration: Theme.fastDuration
            easing.type: Easing.BezierSpline
            easing.bezierCurve: Theme.easeStandard
        }
    }
    onMotionActiveChanged: if (!motionActive)
        opacityAnimation.complete()
    signal clicked
    implicitWidth: 20
    implicitHeight: 20
    // MouseArea.drag.active can survive pool/reuse. A cancelled press must
    // never reactivate when this handle becomes visible with different roles.
    onResourceActiveChanged: if (!resourceActive) {
        gestureArmed = false;
        ghost.Drag.cancel();
    }
    onActionKeyChanged: {
        gestureArmed = false;
        ghost.Drag.cancel();
    }
    EchoIcon {
        anchors.fill: parent
        visible: handle.button === null
        name: handle.glyph
        color: handle.foreground
        opacity: pointer.drag.active ? 0.25 : handle.iconOpacity
    }
    Binding {
        target: handle.button ? handle.button.contentItem : null
        property: "opacity"
        value: handle.buttonIconOpacity
        when: handle.button !== null
        restoreMode: Binding.RestoreBindingOrValue
    }
    Item {
        id: ghost
        // A pooled delegate may acquire new roles; the active gesture keeps the
        // key captured at press and is cancelled before the delegate is reused.
        property string actionKey: ""
        objectName: actionKey
        // Stay in the handle's popup delivery scene. Reparenting to a different
        // Overlay can make it invisible and exclude that popup's DropAreas; a
        // same-popup drag layer only lifts the ghost above list clipping.
        parent: handle.dragLayer ? handle.dragLayer : handle
        width: 24
        height: 24
        z: 10000
        visible: Drag.active
        Drag.active: (handle.button !== null || pointer.drag.active) && handle.resourceActive && handle.gestureArmed
        Drag.source: ghost
        Drag.keys: [handle.dragGroup]
        Drag.hotSpot.x: width / 2
        Drag.hotSpot.y: height / 2
        EchoIcon {
            anchors.fill: parent
            name: handle.glyph
            color: Theme.accentText
        }
    }
    DragHandler {
        id: buttonDrag
        objectName: "shellButtonDrag-" + handle.actionKey
        parent: handle.button
        enabled: handle.button !== null && handle.resourceActive
        target: null
        acceptedButtons: Qt.LeftButton
        dragThreshold: 5
        cursorShape: Qt.ArrowCursor
        onActiveChanged: {
            if (active) {
                ghost.actionKey = handle.actionKey;
                updateGhost();
                handle.gestureArmed = true;
            } else if (handle.gestureArmed) {
                ghost.Drag.drop();
                handle.gestureArmed = false;
            }
        }
        onCanceled: {
            handle.gestureArmed = false;
            ghost.Drag.cancel();
        }
        onCentroidChanged: if (active)
            updateGhost()
        function updateGhost() {
            const point = parent.mapToItem(ghost.parent, centroid.position.x, centroid.position.y);
            ghost.x = point.x - ghost.width / 2;
            ghost.y = point.y - ghost.height / 2;
        }
    }
    MouseArea {
        id: pointer
        objectName: "shellDragPointer-" + handle.actionKey
        anchors.fill: parent
        enabled: handle.resourceActive && handle.button === null
        preventStealing: true
        // Keep the pointer stable during both hover and reordering, as requested.
        cursorShape: Qt.ArrowCursor
        drag.target: ghost
        drag.threshold: 5
        onPressed: mouse => {
            handle.gestureArmed = true;
            ghost.actionKey = handle.actionKey;
            const point = mapToItem(ghost.parent, mouse.x, mouse.y);
            ghost.x = point.x - ghost.width / 2;
            ghost.y = point.y - ghost.height / 2;
        }
        onReleased: {
            if (ghost.Drag.active) {
                ghost.Drag.drop();
            } else if (handle.gestureArmed && handle.resourceActive)
                handle.clicked();
            handle.gestureArmed = false;
        }
        onCanceled: {
            handle.gestureArmed = false;
            ghost.Drag.cancel();
        }
    }
    Component.onDestruction: ghost.Drag.cancel()
}
