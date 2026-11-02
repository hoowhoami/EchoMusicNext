pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import EchoMusic.Theme

// Shared context-menu surface with bounded viewport width and height.
// Long menus scroll; Action entries can carry a dynamic danger property.
Menu {
    id: menuRoot
    EchoPopupLifecycle {
        surface: menuRoot
    }
    // Rounded so the content ListView and every stacked row land on whole DIPs;
    // fractional offsets would smear EchoMenuSeparator's 1px line across pixels.
    padding: Math.round(6 * uiScale)
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
    property var hostWindow: menuRoot.parent && menuRoot.parent.Window !== undefined ? menuRoot.parent.Window.window : null
    property real measuredWidth: 180
    // Compute size before the first frame. ListView contentHeight settles only
    // after delegate creation, but aboutToShow already needs the placement bounds.
    property real measuredHeight: 40
    function refreshMeasuredSize() {
        let widest = 168 * menuRoot.uiScale;
        let height = 0;
        // Popup-derived Menu exposes contentData, not Item.data.
        const children = menuRoot.contentData;
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
            // are ignored so they cannot inflate the menuRoot.
            const isAction = typeof item.text === "string" && item.implicitHeight === undefined;
            if (!isAction && item.implicitHeight === undefined)
                continue;
            height += isAction ? 36 * menuRoot.uiScale : item.implicitHeight;
            if (typeof item.text !== "string" || item.text.length === 0)
                continue;
            measure.text = item.text;
            widest = Math.max(widest, measure.advanceWidth + (isAction ? (20 + (item.checkable === true ? 22 : 0)) * menuRoot.uiScale : item.leftPadding + item.rightPadding));
        }
        const cap = menuRoot.hostWindow ? menuRoot.hostWindow.width - 16 * menuRoot.uiScale : 100000;
        menuRoot.measuredWidth = Math.min(Math.max(180 * menuRoot.uiScale, widest + menuRoot.leftPadding + menuRoot.rightPadding), Math.max(96 * menuRoot.uiScale, cap));
        const heightCap = menuRoot.hostWindow ? Math.max(96 * menuRoot.uiScale, menuRoot.hostWindow.height - 104 * menuRoot.uiScale) : 100000;
        menuRoot.measuredHeight = Math.min(Math.max(40 * menuRoot.uiScale, height + menuRoot.topPadding + menuRoot.bottomPadding), heightCap);
    }
    implicitWidth: menuRoot.measuredWidth
    implicitHeight: menuRoot.measuredHeight
    onCountChanged: menuRoot.refreshMeasuredSize()
    onUiScaleChanged: menuRoot.refreshMeasuredSize()
    onAboutToShow: {
        menuRoot.refreshMeasuredSize();
        if (!menuRoot.clampToWindow || !menuRoot.hostWindow)
            return;
        const maxX = Math.max(8, menuRoot.hostWindow.width - menuRoot.implicitWidth - 8);
        const maxY = Math.max(8, menuRoot.hostWindow.height - menuRoot.implicitHeight - 96);
        menuRoot.x = Math.round(Math.min(Math.max(menuRoot.x, 8), maxX));
        menuRoot.y = Math.round(Math.min(Math.max(menuRoot.y, 8), maxY));
    }
    Component.onCompleted: menuRoot.refreshMeasuredSize()
    contentItem: ListView {
        id: entries
        implicitHeight: contentHeight
        model: menuRoot.contentModel
        currentIndex: menuRoot.currentIndex
        onContentHeightChanged: Qt.callLater(menuRoot.refreshMeasuredSize)
        clip: true
        interactive: contentHeight > height
        ScrollBar.vertical: EchoPopupScrollBar {
            surface: menuRoot
            view: entries
            resourceActive: menuRoot.visible
        }
    }
    background: EchoFloatingSurface {
        color: Theme.elevated
        radius: Theme.popoverRadius * menuRoot.uiScale
        border.width: 0.5 * menuRoot.uiScale
        border.color: Theme.floating.subtleBorder
    }
    delegate: EchoMenuEntry {
        uiScale: menuRoot.uiScale
    }
    TextMetrics {
        id: measure
        objectName: "echoMenuMeasure"
        font: Theme.font(13 * menuRoot.uiScale, Theme.semiboldWeight)
    }
}
