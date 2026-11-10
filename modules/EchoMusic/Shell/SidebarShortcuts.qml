pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import EchoMusic.Controls
import EchoMusic.Theme
import EchoMusic.Shell

Item {
    id: shortcuts
    required property SidebarLayoutModel layout
    required property RouteController router
    required property bool collapsed
    property real collapseProgress: collapsed ? 1 : 0
    property real railProgress: collapseProgress
    property bool resourceActive: true
    property bool modelRepositionPending: false
    readonly property bool repositionEnabled: resourceActive && Theme.store.motionEnabled
    signal addRequested
    // Natural width establishes the inline header height before the parent
    // assigns its final expanded or collapsed width.
    implicitWidth: 194
    implicitHeight: virtualLoader.active ? virtualLoader.height : grid.implicitHeight * (1 - railProgress) + (cards.count * 48 + add.height) * railProgress
    // Only model edits request a move transition. Width/collapse changes belong
    // to the outer sidebar animation and must not restart per-card animations.
    Connections {
        target: shortcuts.layout.shortcutModel
        function onRowsAboutToBeInserted(): void {
            shortcuts.modelRepositionPending = shortcuts.repositionEnabled;
        }
        function onRowsAboutToBeRemoved(): void {
            shortcuts.modelRepositionPending = shortcuts.repositionEnabled;
        }
        function onRowsAboutToBeMoved(): void {
            shortcuts.modelRepositionPending = shortcuts.repositionEnabled;
        }
    }
    onWidthChanged: modelRepositionPending = false
    onCollapsedChanged: modelRepositionPending = false
    onRepositionEnabledChanged: {
        if (!repositionEnabled) {
            modelRepositionPending = false;
            if (grid)
                grid.forceLayout();
        }
    }
    Loader {
        id: virtualLoader
        active: shortcuts.layout.shortcuts.length > 12
        width: parent.width
        height: active ? 240 : 0
        sourceComponent: GridView {
            id: virtualCards
            width: virtualLoader.width
            height: virtualLoader.height
            // One virtual cell per entry keeps identity and visibility tracking
            // continuous. Odd entries slide from the second column into the rail.
            cellWidth: width
            cellHeight: 27 + 21 * shortcuts.railProgress
            bottomMargin: (count % 2) * 27 * (1 - shortcuts.railProgress)
            model: shortcuts.layout.shortcuts.length + 1
            reuseItems: true
            EchoViewPoolGuard {
                targetView: virtualCards
            }
            cacheBuffer: 108
            clip: true
            ScrollBar.vertical: EchoPageScrollBar {
                view: virtualCards
                viewActive: shortcuts.resourceActive
            }
            function restoreFocus(key: string): void {
                const index = shortcuts.layout.shortcuts.findIndex(row => row.key === key);
                if (index < 0)
                    return;
                currentIndex = index;
                positionViewAtIndex(index, GridView.Contain);
                forceLayout();
                Qt.callLater(() => {
                    const cell = itemAtIndex(index);
                    if (cell && cell.cardItem && cell.cardItem.activation.enabled)
                        cell.cardItem.activation.forceActiveFocus(Qt.TabFocusReason);
                });
            }
            delegate: Item {
                id: cell
                required property int index
                property bool pooled: false
                GridView.onPooled: pooled = true
                GridView.onReused: pooled = false
                width: 93 - 49 * shortcuts.collapseProgress
                height: 46 - 6 * shortcuts.collapseProgress
                transform: Translate {
                    x: (cell.index % 2) * 101 * (1 - shortcuts.collapseProgress) - cell.x
                    // GridView polishes its cells after the clock tick. Project
                    // from the actual native slot, including its pixel rounding.
                    y: Math.floor(cell.index / 2) * 54 * (1 - shortcuts.railProgress) + cell.index * 48 * shortcuts.railProgress - cell.y
                }
                readonly property var row: shortcuts.layout.shortcuts[index] || ({})
                readonly property alias cardItem: cardLoader.item
                Loader {
                    id: cardLoader
                    anchors.fill: parent
                    active: cell.index < shortcuts.layout.shortcuts.length
                    sourceComponent: SidebarShortcutCard {
                        layout: shortcuts.layout
                        router: shortcuts.router
                        dragLayer: virtualCards
                        entryIndex: cell.index
                        entryKey: cell.row.key
                        routeName: cell.row.route
                        title: cell.row.title
                        glyph: cell.row.glyph
                        cover: cell.row.cover || ""
                        resourceId: cell.row.resourceId || ""
                        collapsed: shortcuts.collapsed
                        collapseProgress: shortcuts.collapseProgress
                        resourceActive: shortcuts.resourceActive && !cell.pooled
                        onReorderRequested: (key, target, after) => {
                            shortcuts.layout.moveShortcutBefore(key, target, after);
                            virtualCards.restoreFocus(key);
                        }
                        onRemoved: (previous, keyboard) => {
                            if (keyboard)
                                Qt.callLater(() => {
                                    const entry = shortcuts.layout.shortcuts[Math.min(previous, shortcuts.layout.shortcuts.length - 1)];
                                    if (entry)
                                        virtualCards.restoreFocus(entry.key);
                                });
                        }
                    }
                }
                EchoIconButton {
                    anchors.fill: parent
                    visible: cell.index === shortcuts.layout.shortcuts.length
                    objectName: "sidebarShortcutAddVirtual"
                    glyph: "plus"
                    text: qsTr("添加快捷卡片")
                    onClicked: shortcuts.addRequested()
                }
            }
        }
    }
    Flow {
        id: grid
        visible: !virtualLoader.active
        // The native Flow owns the expanded slots even during collapse.
        // Card presentation interpolates from those slots to a single rail;
        // no breakpoint can suddenly wrap a card on a resize frame.
        width: 194
        spacing: 8
        onPositioningComplete: shortcuts.modelRepositionPending = false
        move: Transition {
            enabled: shortcuts.repositionEnabled && shortcuts.modelRepositionPending
            NumberAnimation {
                properties: "x,y"
                duration: Theme.store.motionEnabled ? 160 : 0
                easing.type: Easing.BezierSpline
                easing.bezierCurve: [0.25, 0.1, 0.25, 1, 1, 1]
            }
        }
        Repeater {
            id: cards
            // The built-in set is bounded; never project the user's unbounded
            // playlist catalog through this positioner.
            model: virtualLoader.active ? null : shortcuts.layout.shortcutModel
            delegate: Item {
                id: slot
                required property int index
                required property string shortcutKey
                required property string shortcutTitle
                required property string shortcutRoute
                required property string shortcutGlyph
                required property string shortcutCover
                required property string shortcutResourceId
                width: 93
                height: 46
                readonly property alias entryKey: shortcutCard.entryKey
                readonly property alias activation: shortcutCard.activation
                SidebarShortcutCard {
                    id: shortcutCard
                    parent: shortcuts
                    layout: shortcuts.layout
                    router: shortcuts.router
                    entryKey: slot.shortcutKey
                    routeName: slot.shortcutRoute
                    title: slot.shortcutTitle
                    glyph: slot.shortcutGlyph
                    cover: slot.shortcutCover
                    resourceId: slot.shortcutResourceId
                    entryIndex: slot.index
                    collapsed: shortcuts.collapsed
                    collapseProgress: shortcuts.collapseProgress
                    resourceActive: shortcuts.resourceActive
                    dragLayer: shortcuts
                    x: slot.x * (1 - shortcuts.collapseProgress)
                    y: slot.y * (1 - shortcuts.railProgress) + (slot.y / 54 * 2 + slot.x / 101) * 48 * shortcuts.railProgress
                    width: 93 - 49 * shortcuts.collapseProgress
                    height: 46 - 6 * shortcuts.collapseProgress
                    onReorderRequested: (key, target, after) => {
                        shortcuts.layout.moveShortcutBefore(key, target, after);
                        restoreKeyboardFocus(key);
                    }
                    function restoreKeyboardFocus(key) {
                        Qt.callLater(() => {
                            for (let i = 0; i < cards.count; ++i) {
                                const candidate = cards.itemAt(i);
                                if (candidate && candidate.entryKey === key && candidate.activation.enabled)
                                    candidate.activation.forceActiveFocus(Qt.TabFocusReason);
                            }
                        });
                    }
                    onRemoved: (previous, keyboard) => {
                        if (!keyboard)
                            return;
                        Qt.callLater(() => {
                            const next = cards.itemAt(Math.min(previous, cards.count - 1));
                            if (next && next.activation.enabled)
                                next.activation.forceActiveFocus(Qt.TabFocusReason);
                            else
                                add.forceActiveFocus(Qt.TabFocusReason);
                        });
                    }
                }
            }
        }
        Item {
            id: addSlot
            width: cards.count % 2 === 0 ? grid.width : 93
            height: cards.count % 2 === 0 ? 28 : 46
        }
    }
    EchoIconButton {
        id: add
        visible: !virtualLoader.active
        parent: shortcuts
        x: addSlot.x * (1 - shortcuts.collapseProgress)
        y: addSlot.y * (1 - shortcuts.railProgress) + (addSlot.y / 54 * 2 + addSlot.x / 101) * 48 * shortcuts.railProgress
        objectName: "sidebarShortcutAdd"
        glyph: "plus"
        focusPolicy: Qt.StrongFocus
        text: qsTr("添加快捷卡片")
        width: addSlot.width * (1 - shortcuts.collapseProgress) + 44 * shortcuts.collapseProgress
        height: addSlot.height * (1 - shortcuts.collapseProgress) + 40 * shortcuts.collapseProgress
        padding: !shortcuts.collapsed && cards.count % 2 === 0 ? 0 : 8
        foreground: Theme.secondaryText
        contentItem: Item {
            EchoIcon {
                anchors.centerIn: parent
                width: 25
                height: 25
                name: "plus"
                color: add.foreground
            }
        }
        onClicked: shortcuts.addRequested()
        background: EchoDashedFrame {
            objectName: "sidebarShortcutAddFrame"
            resourceActive: shortcuts.resourceActive
            strokeColor: Theme.colors.strongBorder
            fillColor: add.hovered ? Theme.hover : "transparent"
            radius: Theme.cardRadius
        }
    }
}
