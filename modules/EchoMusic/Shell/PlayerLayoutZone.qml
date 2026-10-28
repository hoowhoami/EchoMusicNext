pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import EchoMusic.Controls
import EchoMusic.Theme
import EchoMusic.Shell

Rectangle {
    id: pane
    required property ShellActionModel actions
    required property int zone
    required property string label
    property bool showLabel: true
    property bool resourceActive: true
    property Item dragLayer: null
    required property PlayerLayoutReorder reorder
    property real releaseScroll: 0
    readonly property int itemCount: chips.count
    readonly property bool motionActive: resourceActive && visible && Window.window !== null && Window.window.visible && Window.window.visibility !== Window.Minimized && Theme.store.motionEnabled
    readonly property bool insertionGap: reorder.dragging && reorder.targetZone === zone && reorder.sourceZone !== zone
    readonly property int previewCount: itemCount + (insertionGap ? 1 : 0) - (reorder.dragging && reorder.sourceZone === zone && reorder.targetZone >= 0 && reorder.targetZone !== zone ? 1 : 0)
    readonly property bool nativeMotionActive: motionActive && !reorder.dragging && !reorder.rebasing
    onNativeMotionActiveChanged: if (!nativeMotionActive) {
        nativeMove.complete();
        nativeDisplace.complete();
    }
    Component.onCompleted: reorder.registerPane(pane)
    Component.onDestruction: if (reorder)
        reorder.unregisterPane(pane)
    function insertionIndex(scenePosition: point): int {
        const local = mapFromItem(null, scenePosition.x, scenePosition.y);
        if (!resourceActive || !visible || local.x < 0 || local.x > width || local.y < 0 || local.y > height)
            return -1;
        const point = chips.mapFromItem(pane, local.x, local.y);
        const same = reorder.sourceZone === zone;
        const limit = Math.max(0, chips.count - (same ? 1 : 0));
        return Math.max(0, Math.min(limit, Math.floor((point.x + chips.contentX - chips.originX + (same ? 0 : 20)) / 40)));
    }
    function insertionAnchor(index: int): var {
        const nextIndex = index + (reorder.sourceZone === zone && reorder.sourceIndex <= index ? 1 : 0);
        const next = chips.itemAtIndex(nextIndex) as LayoutSlot;
        if (next && next.actionKey !== reorder.key)
            return {
                key: next.actionKey,
                after: false
            };
        const previous = chips.itemAtIndex(nextIndex - 1) as LayoutSlot;
        if (previous && previous.actionKey !== reorder.key)
            return {
                key: previous.actionKey,
                after: true
            };
        return nextIndex >= chips.count ? {
            key: "",
            after: false
        } : null;
    }
    function capturePositions(positions: var): void {
        releaseScroll = chips.contentX - chips.originX;
        for (const child of chips.contentItem.children) {
            const item = child as LayoutSlot;
            if (item && !item.pooled && item.visible)
                positions[item.actionKey] = item.mapToItem(reorder.hostItem, 0, 0);
        }
    }
    function applyPositions(): void {
        chips.forceLayout();
        // Inserting before the first visible row can shift ListView's origin.
        // Preserve the logical viewport so the dropped chip stays in its gap.
        chips.contentX = chips.originX + Math.max(0, Math.min(releaseScroll, chips.contentWidth - chips.width));
        chips.forceLayout();
        for (const child of chips.contentItem.children) {
            const item = child as LayoutSlot;
            if (item && !item.pooled && item.visible)
                item.applyPosition();
        }
    }
    function settlePositions(): void {
        for (const child of chips.contentItem.children) {
            const item = child as LayoutSlot;
            if (item)
                item.settlePosition();
        }
    }
    implicitHeight: (showLabel ? 18 : 0) + 64 + 16
    color: Theme.elevated
    radius: Theme.itemRadius
    Accessible.name: label + qsTr("区按钮")
    Label {
        x: 8
        y: 8
        text: pane.label
        visible: pane.showLabel
        color: Theme.floatingSecondaryText
        font: Theme.font(12, Theme.regularWeight)
    }
    ListView {
        id: chips
        objectName: "playerLayoutZone-" + pane.zone
        x: 8
        y: pane.showLabel ? 26 : 8
        width: parent.width - 16
        height: 64
        clip: true
        orientation: ListView.Horizontal
        spacing: 6
        reuseItems: pane.resourceActive
        cacheBuffer: 0
        model: ShellActionProjection {
            actions: pane.resourceActive ? pane.actions : null
            zone: pane.zone
            usePreferredPlacement: true
        }
        move: Transition {
            enabled: pane.nativeMotionActive
            NumberAnimation {
                id: nativeMove
                properties: "x,y"
                duration: Theme.normalDuration
                easing.type: Easing.BezierSpline
                easing.bezierCurve: Theme.easeStandard
            }
        }
        moveDisplaced: Transition {
            enabled: pane.nativeMotionActive
            NumberAnimation {
                id: nativeDisplace
                properties: "x,y"
                duration: Theme.normalDuration
                easing.type: Easing.BezierSpline
                easing.bezierCurve: Theme.easeStandard
            }
        }
        delegate: LayoutSlot {}
        Canvas {
            parent: chips.contentItem
            objectName: "playerLayoutGap-" + pane.zone
            x: chips.originX + (pane.insertionGap ? pane.reorder.targetIndex * 40 : 0)
            visible: pane.insertionGap || pane.previewCount === 0
            width: 32
            height: 32
            y: 30
            property color strokeColor: pane.insertionGap ? Theme.withAlpha(Theme.floatingAccentText, 0.35) : Theme.withAlpha(Theme.store.floatingTone, 0.16)
            onStrokeColorChanged: requestPaint()
            onPaint: {
                const context = getContext("2d");
                context.reset();
                context.strokeStyle = strokeColor;
                context.lineWidth = 1;
                context.setLineDash([3, 3]);
                context.roundedRect(0.5, 0.5, width - 1, height - 1, Theme.cardRadius, Theme.cardRadius);
                context.stroke();
            }
        }
    }

    component LayoutSlot: Item {
        id: slot
        required property int index
        required property string actionKey
        required property string actionTitle
        required property string actionIcon
        required property bool actionEnabled
        required property bool badgeSupported
        required property bool badgePreferred
        objectName: "playerLayoutSlot-" + actionKey
        property real settleX: 0
        property real settleY: 0
        property real settleOpacity: 1
        property bool applying: false
        readonly property bool animate: pane.motionActive && !pane.reorder.rebasing && !pooled && !applying
        readonly property real previewX: (pane.reorder.visualIndex(pane.zone, index, actionKey) - index) * 40
        opacity: pane.reorder.dragging && !pane.reorder.rebasing && pane.zone === pane.reorder.sourceZone && pane.reorder.key === actionKey && pane.reorder.targetZone >= 0 && pane.reorder.targetZone !== pane.zone ? 0 : settleOpacity
        transform: Translate {
            x: slot.previewX + slot.settleX
            y: slot.settleY
            Behavior on x {
                enabled: slot.animate
                NumberAnimation {
                    id: moveX
                    duration: Theme.normalDuration
                    easing.type: Easing.BezierSpline
                    easing.bezierCurve: Theme.easeStandard
                }
            }
            Behavior on y {
                enabled: slot.animate
                NumberAnimation {
                    id: moveY
                    duration: Theme.normalDuration
                    easing.type: Easing.BezierSpline
                    easing.bezierCurve: Theme.easeStandard
                }
            }
        }
        Behavior on opacity {
            enabled: slot.animate
            NumberAnimation {
                id: fade
                duration: Theme.fastDuration
                easing.type: Easing.BezierSpline
                easing.bezierCurve: Theme.easeStandard
            }
        }
        onAnimateChanged: if (!animate) {
            moveX.complete();
            moveY.complete();
            fade.complete();
        }
        Component.onCompleted: Qt.callLater(applyPosition)
        function applyPosition(): void {
            if (pooled || !visible)
                return;
            const point = pane.reorder.takePosition(actionKey);
            if (point === undefined)
                return;
            applying = true;
            const current = mapToItem(pane.reorder.hostItem, 0, 0);
            settleX += point.x - current.x;
            settleY += point.y - current.y;
            settleOpacity = actionKey === pane.reorder.releasedKey ? 0.25 : 1;
            applying = false;
            if (!pane.reorder.rebasing)
                settlePosition();
        }
        function settlePosition(): void {
            settleX = 0;
            settleY = 0;
            settleOpacity = 1;
        }
        property bool pooled: false
        ListView.onPooled: pooled = true
        ListView.onReused: {
            pooled = false;
            Qt.callLater(applyPosition);
        }
        width: 34
        height: 64
        // ListView owns the slot position. Keep the icon and badge inside
        // this full-height slot so the badge is not clipped above the view.
        ToolButton {
            id: chip
            objectName: "playerLayoutChip-" + slot.actionKey
            width: 34
            height: 34
            y: 30
            padding: 8
            hoverEnabled: true
            Accessible.name: slot.actionTitle
            EchoToolTip {
                parent: chip
                text: slot.actionTitle
                requestedVisible: chip.hovered && pane.resourceActive && !slot.pooled && !pane.reorder.dragging
            }
            contentItem: EchoIcon {
                name: slot.actionIcon
                color: chip.hovered || chip.visualFocus ? Theme.floatingAccentText : Theme.floatingText
                opacity: slot.actionEnabled ? 1 : 0.56
            }
            ShellActionDragHandle {
                anchors.fill: parent
                button: chip
                actionKey: slot.actionKey
                glyph: slot.actionIcon
                dragGroup: "playerbar-actions"
                dragLayer: pane.dragLayer
                resourceActive: pane.resourceActive && !slot.pooled
                foreground: chip.hovered || chip.visualFocus ? Theme.floatingAccentText : Theme.floatingText
                iconOpacity: slot.actionEnabled ? 1 : 0.56
                onReorderStarted: key => pane.reorder.begin(key, pane.zone, slot.index)
                onReorderMoved: position => pane.reorder.update(position)
                onReorderReleased: position => pane.reorder.finish(position)
                onReorderCanceled: if (!pane.reorder.rebasing && pane.reorder.key === slot.actionKey)
                    pane.reorder.cancel()
            }
            background: Rectangle {
                radius: Theme.itemRadius
                color: pane.reorder.dragging && pane.reorder.key === slot.actionKey ? Theme.withAlpha(Theme.accent, 0.08) : chip.hovered || chip.visualFocus ? Theme.floating.hover : Theme.floating.mutedSurface
                border.width: chip.visualFocus ? 2 : 0
                border.color: Theme.accent
            }
            Keys.onPressed: event => {
                if ((event.modifiers & Qt.AltModifier) && (event.key === Qt.Key_Up || event.key === Qt.Key_Down)) {
                    pane.actions.shiftInPlayerZone(slot.actionKey, event.key === Qt.Key_Up ? -1 : 1);
                    event.accepted = true;
                }
            }
            Button {
                id: badge
                objectName: "playerBadgePreference-" + slot.actionKey
                width: 24
                height: 24
                x: (chip.width - width) / 2
                y: -28
                visible: slot.badgeSupported
                Accessible.role: Accessible.CheckBox
                Accessible.checked: slot.badgePreferred
                Accessible.name: (slot.badgePreferred ? qsTr("隐藏") : qsTr("显示")) + slot.actionTitle + qsTr("徽标")
                onClicked: pane.actions.setBadgeVisible(slot.actionKey, !slot.badgePreferred)
                // Override Basic Button's extra horizontal padding to keep the 14px checkbox square.
                padding: 5
                horizontalPadding: 5
                verticalPadding: 5
                background: Rectangle {
                    radius: Theme.controlRadius
                    color: badge.hovered ? Theme.floating.hover : "transparent"
                }
                contentItem: Rectangle {
                    radius: Theme.microRadius
                    color: slot.badgePreferred ? Theme.accent : Theme.floating.mutedSurface
                    border.width: 1
                    border.color: slot.badgePreferred ? Theme.accent : Theme.floatingBorder
                    EchoIcon {
                        anchors.fill: parent
                        anchors.margins: 1
                        visible: slot.badgePreferred
                        name: "check"
                        color: Theme.store.onAccent
                    }
                }
            }
        }
    }
}
