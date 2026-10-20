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
        delegate: Item {
            id: slot
            required property string actionKey
            required property string actionTitle
            required property string actionIcon
            required property bool actionEnabled
            required property bool badgeSupported
            required property bool badgePreferred
            property bool pooled: false
            ListView.onPooled: pooled = true
            ListView.onReused: pooled = false
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
                    requestedVisible: chip.hovered && pane.resourceActive && !slot.pooled
                }
                contentItem: ShellActionDragHandle {
                    actionKey: slot.actionKey
                    glyph: slot.actionIcon
                    dragGroup: "playerbar-actions"
                    dragLayer: pane.dragLayer
                    resourceActive: pane.resourceActive && !slot.pooled
                    foreground: chip.hovered || chip.visualFocus ? Theme.floatingAccentText : Theme.floatingText
                    iconOpacity: slot.actionEnabled ? 1 : 0.56
                }
                background: Rectangle {
                    radius: Theme.itemRadius
                    color: chip.hovered || chip.visualFocus ? Theme.floating.hover : Theme.floating.mutedSurface
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
                DropArea {
                    anchors.fill: parent
                    keys: ["playerbar-actions"]
                    onDropped: drop => {
                        const source = drop.source.objectName, target = slot.actionKey, after = drop.x > width / 2;
                        const actions = pane.actions, zone = pane.zone;
                        Qt.callLater(() => actions.moveInPlayerZone(source, zone, target, after));
                        drop.acceptProposedAction();
                    }
                }
            }
        }
        Canvas {
            visible: chips.count === 0
            width: 32
            height: 32
            y: 30
            property color strokeColor: Theme.withAlpha(Theme.store.floatingTone, 0.16)
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
    DropArea {
        anchors.fill: parent
        z: -0.5 // Chip targets handle ordering; empty space appends to this zone.
        keys: ["playerbar-actions"]
        onDropped: drop => {
            const source = drop.source.objectName, actions = pane.actions, zone = pane.zone;
            Qt.callLater(() => actions.moveInPlayerZone(source, zone));
            drop.acceptProposedAction();
        }
    }
}
