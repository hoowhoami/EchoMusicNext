pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import EchoMusic.Theme

// Shared context-menu surface with bounded viewport width and height.
// Long menus scroll; Action entries can carry a dynamic danger property.
Menu {
    id: menu
    EchoPopupLifecycle {
        surface: menu
    }
    padding: 6 * uiScale
    margins: 12 * uiScale
    property real uiScale: 1
    // Native popup windows size themselves from width/height, not Popup.scale.
    // Scale the recipe's dimensions so its native bounds include every row.
    // Basic's default modeless dimmer is rectangular, including the native
    // popup's transparent corners. Menus need no separate dimming layer.
    Overlay.modal: Item {}
    Overlay.modeless: Item {}
    /// SongContextMenu clamps its open point using an 8px viewport inset
    /// and 96px bottom reserve. Floating hosts can use anchored placement instead.
    property bool clampToWindow: false
    property var hostWindow: menu.parent && menu.parent.Window !== undefined ? menu.parent.Window.window : null
    property real measuredWidth: 180
    // Compute size before the first frame. ListView contentHeight settles only
    // after delegate creation, but aboutToShow already needs the placement bounds.
    property real measuredHeight: 40
    function refreshMeasuredSize() {
        let widest = 168 * menu.uiScale;
        let height = 0;
        // Popup-derived Menu exposes contentData, not Item.data.
        const children = menu.contentData;
        for (let i = 0; i < children.length; ++i) {
            const item = children[i];
            // Skip the measuring TextMetrics and non-entry owners.
            if (!item || item.objectName === "echoMenuMeasure")
                continue;
            if (item.visible === false)
                continue;
            // Direct Item children (EchoMenuEntry / EchoMenuSeparator) carry
            // their own measured height; Action children resolve through the
            // delegate's 36px minimum row. Non-entry objects (stray QtObjects)
            // are ignored so they cannot inflate the menu.
            const isAction = typeof item.text === "string" && item.implicitHeight === undefined;
            if (!isAction && item.implicitHeight === undefined)
                continue;
            height += isAction ? 36 * menu.uiScale : item.implicitHeight;
            if (typeof item.text !== "string" || item.text.length === 0)
                continue;
            measure.text = item.text;
            widest = Math.max(widest, measure.advanceWidth + (isAction ? (20 + (item.checkable === true ? 22 : 0)) * menu.uiScale : item.leftPadding + item.rightPadding));
        }
        const cap = menu.hostWindow ? menu.hostWindow.width - 16 * menu.uiScale : 100000;
        menu.measuredWidth = Math.min(Math.max(180 * menu.uiScale, widest + menu.leftPadding + menu.rightPadding), Math.max(96 * menu.uiScale, cap));
        const heightCap = menu.hostWindow ? Math.max(96 * menu.uiScale, menu.hostWindow.height - 104 * menu.uiScale) : 100000;
        menu.measuredHeight = Math.min(Math.max(40 * menu.uiScale, height + menu.topPadding + menu.bottomPadding), heightCap);
    }
    implicitWidth: menu.measuredWidth
    implicitHeight: menu.measuredHeight
    onCountChanged: menu.refreshMeasuredSize()
    onUiScaleChanged: menu.refreshMeasuredSize()
    onAboutToShow: {
        menu.refreshMeasuredSize();
        if (!menu.clampToWindow || !menu.hostWindow)
            return;
        const maxX = Math.max(8, menu.hostWindow.width - menu.implicitWidth - 8);
        const maxY = Math.max(8, menu.hostWindow.height - menu.implicitHeight - 96);
        menu.x = Math.round(Math.min(Math.max(menu.x, 8), maxX));
        menu.y = Math.round(Math.min(Math.max(menu.y, 8), maxY));
    }
    Component.onCompleted: menu.refreshMeasuredSize()
    contentItem: ListView {
        id: entries
        implicitHeight: contentHeight
        model: menu.contentModel
        currentIndex: menu.currentIndex
        onContentHeightChanged: Qt.callLater(menu.refreshMeasuredSize)
        clip: true
        interactive: contentHeight > height
        ScrollBar.vertical: EchoPopupScrollBar {
            surface: menu
            view: entries
            resourceActive: menu.visible
        }
    }
    background: EchoFloatingSurface {
        color: Theme.elevated
        radius: Theme.popoverRadius * menu.uiScale
        border.width: 0.5 * menu.uiScale
        border.color: Theme.floating.subtleBorder
    }
    delegate: EchoMenuEntry {
        uiScale: menu.uiScale
    }
    TextMetrics {
        id: measure
        objectName: "echoMenuMeasure"
        font: Theme.font(13 * menu.uiScale, Theme.semiboldWeight)
    }
}
