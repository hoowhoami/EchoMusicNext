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
    property bool resourceActive: true
    property bool modelRepositionPending: false
    readonly property bool repositionEnabled: resourceActive && Theme.store.motionEnabled
    signal addRequested
    // Natural width establishes the inline header height before the parent
    // assigns its final expanded or collapsed width.
    implicitWidth: 194
    implicitHeight: virtualLoader.active ? virtualLoader.height : grid.implicitHeight
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
        height: active ? Math.min(240, Math.ceil((shortcuts.layout.shortcuts.length + 1) / (shortcuts.collapsed ? 1 : 2)) * (shortcuts.collapsed ? 48 : 54)) : 0
        sourceComponent: GridView {
            id: virtualCards
            width: virtualLoader.width
            height: virtualLoader.height
            cellWidth: shortcuts.collapsed ? width : (width + 8) / 2
            cellHeight: shortcuts.collapsed ? 48 : 54
            model: shortcuts.layout.shortcuts.length + 1
            reuseItems: true
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
                width: virtualCards.cellWidth - (shortcuts.collapsed ? 0 : 8)
                height: virtualCards.cellHeight - 8
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
                        resourceActive: shortcuts.resourceActive
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
        width: parent.width
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
            delegate: SidebarShortcutCard {
                required property int index
                required property string shortcutKey
                required property string shortcutTitle
                required property string shortcutRoute
                required property string shortcutGlyph
                required property string shortcutCover
                required property string shortcutResourceId
                layout: shortcuts.layout
                router: shortcuts.router
                entryKey: shortcutKey
                routeName: shortcutRoute
                title: shortcutTitle
                glyph: shortcutGlyph
                cover: shortcutCover
                resourceId: shortcutResourceId
                entryIndex: index
                collapsed: shortcuts.collapsed
                resourceActive: shortcuts.resourceActive
                dragLayer: shortcuts
                width: shortcuts.collapsed ? grid.width : Math.max(0, (grid.width - grid.spacing) / 2)
                height: shortcuts.collapsed ? 40 : 46
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
        EchoIconButton {
            id: add
            objectName: "sidebarShortcutAdd"
            glyph: "plus"
            focusPolicy: Qt.StrongFocus
            text: qsTr("添加快捷卡片")
            width: shortcuts.collapsed || cards.count % 2 === 0 ? grid.width : Math.max(0, (grid.width - grid.spacing) / 2)
            height: shortcuts.collapsed ? 40 : cards.count % 2 === 0 ? 28 : 46
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
            background: Canvas {
                readonly property color outlineColor: Theme.colors.strongBorder
                onOutlineColorChanged: requestPaint()
                onWidthChanged: requestPaint()
                onHeightChanged: requestPaint()
                onPaint: {
                    const ctx = getContext("2d");
                    ctx.reset();
                    ctx.fillStyle = add.hovered ? Theme.hover : "transparent";
                    ctx.strokeStyle = outlineColor;
                    ctx.lineWidth = 1;
                    ctx.setLineDash([3, 3]);
                    ctx.beginPath();
                    ctx.roundedRect(0.5, 0.5, width - 1, height - 1, Theme.cardRadius, Theme.cardRadius);
                    ctx.fill();
                    ctx.stroke();
                }
                Connections {
                    target: add
                    function onHoveredChanged() {
                        add.background.requestPaint();
                    }
                }
            }
        }
    }
}
