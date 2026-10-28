pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import EchoMusic.Controls
import EchoMusic.Theme
import EchoMusic.Shell

Item {
    id: card
    required property SidebarLayoutModel layout
    required property RouteController router
    required property Item dragLayer
    required property string routeName
    required property string entryKey
    required property string title
    required property string glyph
    property string cover: ""
    property string resourceId: ""
    required property int entryIndex
    property bool collapsed: false
    property bool resourceActive: true
    property bool gestureArmed: false
    property bool dragging: false
    readonly property bool hovered: enabled && resourceActive && (pointer.containsMouse || remove.hovered || activate.visualFocus || remove.visualFocus)
    readonly property bool selected: router.current && router.current.name === routeName && router.current.resourceId === resourceId
    readonly property bool canRemove: layout.shortcuts.length > 1 && !collapsed
    readonly property alias activation: activate
    signal removed(int previousIndex, bool keyboard)
    signal reorderRequested(string key, string target, bool after)
    objectName: "sidebarShortcut-" + entryKey
    implicitHeight: collapsed ? 40 : 46

    function cancelDrag() {
        if (dragging) {
            ghost.Drag.cancel();
            layout.endShortcutDrag(false);
        }
        dragging = false;
        gestureArmed = false;
    }
    Rectangle {
        objectName: "sidebarShortcutSurface"
        anchors.fill: parent
        radius: Theme.cardRadius
        color: card.selected ? Theme.selected : card.hovered ? Theme.hover : Theme.mutedSurface
        opacity: card.dragging ? 0.45 : 1
        Behavior on color {
            ColorAnimation {
                duration: Theme.normalDuration
                easing.type: Easing.BezierSpline
                easing.bezierCurve: Theme.easeStandard
            }
        }
    }
    Button {
        id: activate
        anchors.fill: parent
        enabled: card.layout.isPlugin(card.entryKey) ? card.layout.entryEnabled(card.entryKey) : card.router.available(card.routeName)
        Accessible.name: card.title + (card.entryIndex === 0 ? qsTr("（首页）") : "")
        leftPadding: 8
        rightPadding: 8
        topPadding: 4
        bottomPadding: 4
        background: null
        onClicked: if (!card.dragging)
            card.layout.openShortcut(card.router, card.entryKey)
        Keys.onEscapePressed: card.cancelDrag()
        // Window navigation shortcuts run before ordinary key handlers.
        // A focused card must consume Alt+arrows itself.
        Keys.onShortcutOverride: event => {
            if ((event.modifiers & Qt.AltModifier) && [Qt.Key_Left, Qt.Key_Right, Qt.Key_Up, Qt.Key_Down].includes(event.key) || card.dragging && event.key === Qt.Key_Escape)
                event.accepted = true;
        }
        Keys.onPressed: event => {
            if (!(event.modifiers & Qt.AltModifier))
                return;
            const offset = event.key === Qt.Key_Left || event.key === Qt.Key_Up ? -1 : event.key === Qt.Key_Right || event.key === Qt.Key_Down ? 1 : 0;
            if (!offset)
                return;
            const keys = card.layout.shortcuts;
            const to = card.entryIndex + offset;
            if (to >= 0 && to < keys.length)
                card.reorderRequested(card.entryKey, keys[to].key, offset > 0);
            event.accepted = true;
        }
        contentItem: Item {
            opacity: activate.enabled ? card.dragging ? 0.45 : 1 : 0.5
            EchoIcon {
                objectName: "sidebarShortcutIcon"
                anchors.centerIn: parent
                width: 20
                height: 20
                visible: !card.resourceId
                name: card.glyph
                color: card.selected ? Theme.accentText : Theme.iconMain
                opacity: card.hovered && !card.collapsed ? 0 : 1
                Behavior on opacity {
                    NumberAnimation {
                        duration: Theme.normalDuration
                        easing.type: Easing.BezierSpline
                        easing.bezierCurve: Theme.easeStandard
                    }
                }
            }
            Loader {
                anchors.centerIn: parent
                width: 20
                height: 20
                active: !!card.resourceId && card.resourceActive
                opacity: card.hovered && !card.collapsed ? 0 : 1
                sourceComponent: EchoCover {
                    source: card.cover
                    coverAlt: card.title
                    circular: card.routeName === "artist-detail"
                    cornerRadius: circular ? width / 2 : 5
                    placeholderGlyph: card.routeName === "artist-detail" ? "user" : "playlist-add"
                    placeholderGlyphSize: 16
                    resourceActive: card.resourceActive
                }
                Behavior on opacity {
                    NumberAnimation {
                        duration: Theme.normalDuration
                        easing.type: Easing.BezierSpline
                        easing.bezierCurve: Theme.easeStandard
                    }
                }
            }
            Label {
                objectName: "sidebarShortcutLabel"
                anchors.verticalCenter: parent.verticalCenter
                width: Math.max(0, parent.width - (card.canRemove ? 24 : 0))
                height: parent.height
                visible: !card.collapsed
                opacity: card.hovered ? 1 : 0
                text: card.title
                color: Theme.text
                font: Theme.font(13, Theme.mediumWeight)
                lineHeightMode: Text.FixedHeight
                lineHeight: 13 * 1.35
                maximumLineCount: 2
                elide: Text.ElideRight
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                wrapMode: Text.WrapAnywhere
                Behavior on opacity {
                    NumberAnimation {
                        duration: Theme.normalDuration
                        easing.type: Easing.BezierSpline
                        easing.bezierCurve: Theme.easeStandard
                    }
                }
            }
        }
    }
    Item {
        id: ghost
        // Keep the drag visual outside the moving positioner: model reorders
        // must not shift the pointer's hotspot or animate the drag source.
        parent: card.dragLayer
        property string capturedKey: ""
        objectName: capturedKey
        width: card.width
        height: card.height
        visible: Drag.active
        z: 10000
        Drag.keys: ["sidebar-shortcuts"]
        Drag.active: card.dragging && card.resourceActive
        Drag.source: ghost
        Drag.hotSpot.x: width / 2
        Drag.hotSpot.y: height / 2
        Rectangle {
            anchors.fill: parent
            radius: Theme.cardRadius
            color: card.selected ? Theme.selected : Theme.hover
            opacity: 0.85
        }
        EchoIcon {
            anchors.centerIn: parent
            width: 20
            height: 20
            name: card.glyph
            color: card.selected ? Theme.accentText : Theme.iconMain
            visible: card.collapsed
        }
        Label {
            anchors.verticalCenter: parent.verticalCenter
            x: 8
            width: Math.max(0, parent.width - 16 - (card.canRemove ? 24 : 0))
            visible: !card.collapsed
            text: card.title
            font: Theme.font(13, Theme.mediumWeight)
            color: Theme.text
            horizontalAlignment: Text.AlignHCenter
            maximumLineCount: 2
            lineHeightMode: Text.FixedHeight
            lineHeight: 13 * 1.35
            wrapMode: Text.WrapAnywhere
            elide: Text.ElideRight
        }
        EchoIcon {
            x: parent.width - 3 - 24 + 5.5
            y: 3 + 5.5
            width: 13
            height: 13
            name: "x"
            color: Theme.secondaryText
            visible: card.canRemove
        }
    }
    DropArea {
        id: dropTarget
        property bool previewed: false
        anchors.fill: parent
        keys: ["sidebar-shortcuts"]
        function preview(event) {
            // Use the center band, avoiding edge oscillation while neighbors
            // animate into their new native Flow positions.
            if (!previewed && event.source && event.source.objectName !== card.entryKey && event.x > width * 0.1 && event.x < width * 0.9 && event.y > height * 0.1 && event.y < height * 0.9) {
                // One crossing, one move. Pointer jitter must not reverse the
                // swap while this same neighbor animates away underneath it.
                previewed = true;
                card.layout.previewShortcutMove(event.source.objectName, card.entryKey);
            }
        }
        onEntered: event => {
            previewed = false;
            preview(event);
        }
        onPositionChanged: event => preview(event)
        onExited: previewed = false
        onDropped: event => event.acceptProposedAction()
    }
    MouseArea {
        id: pointer
        objectName: "sidebarShortcutPointer-" + card.entryKey
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.ArrowCursor
        enabled: card.resourceActive
        preventStealing: true
        drag.target: ghost
        drag.threshold: 5
        drag.onActiveChanged: {
            if (drag.active && card.gestureArmed)
                card.dragging = card.layout.beginShortcutDrag(card.entryKey);
        }
        onPressed: mouse => {
            card.gestureArmed = true;
            if (activate.enabled)
                activate.forceActiveFocus(Qt.MouseFocusReason);
            ghost.capturedKey = card.entryKey;
            const position = mapToItem(ghost.parent, mouse.x, mouse.y);
            ghost.x = position.x - ghost.width / 2;
            ghost.y = position.y - ghost.height / 2;
        }
        onReleased: {
            if (card.dragging) {
                ghost.Drag.drop();
                card.layout.endShortcutDrag(true);
            } else if (card.gestureArmed && activate.enabled) {
                card.layout.openShortcut(card.router, card.entryKey);
            }
            card.dragging = false;
            card.gestureArmed = false;
        }
        onCanceled: card.cancelDrag()
    }
    EchoIconButton {
        id: remove
        objectName: "sidebarShortcutRemove-" + card.entryKey
        x: parent.width - width - 3
        y: 3
        width: 24
        height: 24
        padding: 5.5
        focusPolicy: Qt.StrongFocus
        // Keep the close button in the keyboard focus chain.
        // Gating its visibility on previous focus interrupts Qt focus transfer.
        visible: card.canRemove
        enabled: card.canRemove && !card.dragging
        opacity: card.hovered ? 1 : 0
        glyph: "x"
        foreground: hovered || visualFocus ? Theme.text : Theme.secondaryText
        background: Rectangle {
            radius: Theme.controlRadius
            color: remove.hovered ? Theme.selected : "transparent"
            Behavior on color {
                ColorAnimation {
                    duration: Theme.fastDuration
                    easing.type: Easing.BezierSpline
                    easing.bezierCurve: Theme.easeStandard
                }
            }
        }
        Behavior on foreground {
            ColorAnimation {
                duration: Theme.fastDuration
                easing.type: Easing.BezierSpline
                easing.bezierCurve: Theme.easeStandard
            }
        }
        Behavior on opacity {
            NumberAnimation {
                duration: Theme.fastDuration
                easing.type: Easing.BezierSpline
                easing.bezierCurve: Theme.easeStandard
            }
        }
        text: qsTr("移除") + card.title + qsTr("卡片")
        onClicked: {
            const previous = card.entryIndex;
            const keyboard = remove.visualFocus;
            card.removed(previous, keyboard);
            card.layout.toggleShortcut(card.entryKey);
        }
    }
    EchoToolTip {
        parent: activate
        text: activate.Accessible.name
        requestedVisible: card.collapsed && card.hovered && activate.enabled && !card.dragging
    }
    onResourceActiveChanged: if (!resourceActive)
        cancelDrag()
    onCollapsedChanged: cancelDrag()
    Component.onDestruction: cancelDrag()
}
