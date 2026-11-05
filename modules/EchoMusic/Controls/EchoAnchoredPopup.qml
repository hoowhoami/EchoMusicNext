pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import EchoMusic.Theme
import EchoMusic.Controls

Popup {
    id: popup
    EchoPopupLifecycle {
        surface: popup
    }
    required property Item anchorItem
    // Avoid Popup.Right (TransformOrigin): an inherited enum name resolves
    // differently in callers and in this component.
    enum Side {
        BelowOrAbove,
        Beside
    }
    property int side: EchoAnchoredPopup.BelowOrAbove
    property real uiScale: 1
    property real preferredWidth: 280
    property real maximumHeight: 420
    property real sideOffset: 8
    property bool preferAbove: false
    property bool alignEnd: true
    property string contentAlign: alignEnd ? "end" : "center"
    property bool showArrow: false
    // Native Popup.Window surfaces may extend beyond a small host (Mini),
    // while ordinary application popovers remain bounded by their overlay.
    property bool screenBounded: false
    readonly property rect placementRect: screenBounded ? geometry.screenRect : Qt.rect(0, 0, parent ? parent.width : preferredWidth, parent ? parent.height : 0)
    readonly property real edge: 12 * uiScale
    readonly property rect anchorRect: geometry.rect
    readonly property real aboveSpace: Math.max(0, anchorRect.y - placementRect.y - edge - sideOffset * uiScale)
    readonly property real belowSpace: Math.max(0, placementRect.y + placementRect.height - anchorRect.y - anchorRect.height - edge - sideOffset * uiScale)
    readonly property bool placedAbove: preferAbove ? (aboveSpace >= height * uiScale || aboveSpace >= belowSpace) : !(belowSpace >= height * uiScale || belowSpace >= aboveSpace)
    readonly property real placementHeight: side === EchoAnchoredPopup.Beside ? Math.max(0, placementRect.height - edge * 2) : Math.max(aboveSpace, belowSpace)
    // Geometry is in overlay pixels; public sizes and padding are logical DIP.
    // Scale available space before applying the caller's logical height limit.
    readonly property real contentHeightLimit: Math.max(0, Math.min(maximumHeight, placementHeight / uiScale) - topPadding - bottomPadding)
    parent: Overlay.overlay
    scale: uiScale
    transformOrigin: Popup.TopLeft
    width: Math.max(0, Math.min(preferredWidth, placementRect.width / uiScale - 24))
    height: Math.min(implicitHeight, contentHeightLimit + topPadding + bottomPadding)
    x: Math.max(placementRect.x + edge, Math.min(side === EchoAnchoredPopup.Beside ? anchorRect.x + anchorRect.width + sideOffset * uiScale : contentAlign === "end" ? anchorRect.x + anchorRect.width - width * uiScale : contentAlign === "start" ? anchorRect.x : anchorRect.x + (anchorRect.width - width * uiScale) / 2, placementRect.x + placementRect.width - edge - width * uiScale))
    y: side === EchoAnchoredPopup.Beside ? Math.max(placementRect.y + edge, Math.min(alignEnd ? anchorRect.y + anchorRect.height - height * uiScale : anchorRect.y, placementRect.y + placementRect.height - edge - height * uiScale)) : placedAbove ? anchorRect.y - sideOffset * uiScale - height * uiScale : anchorRect.y + anchorRect.height + sideOffset * uiScale
    padding: 8
    focus: true
    modal: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    Overlay.modal: Item {}
    background: EchoFloatingSurface {
        // Anchored popovers are borderless; dialogs and search panels can opt into a ring.
        border.width: 0
        Canvas {
            id: arrow
            objectName: "anchoredPopupArrow"
            readonly property color fillColor: Theme.elevated
            readonly property bool above: popup.placedAbove
            visible: popup.showArrow
            width: 14
            height: 8
            x: Math.max(Theme.popoverRadius, Math.min((popup.anchorRect.x + popup.anchorRect.width / 2 - popup.x) / popup.uiScale - width / 2, popup.width - Theme.popoverRadius - width))
            y: above ? popup.height - 1 : -height + 1
            onFillColorChanged: requestPaint()
            onAboveChanged: requestPaint()
            onPaint: {
                const ctx = getContext("2d");
                ctx.reset();
                ctx.fillStyle = fillColor;
                ctx.beginPath();
                ctx.moveTo(0, above ? 0 : height);
                ctx.lineTo(width / 2, above ? height : 0);
                ctx.lineTo(width, above ? 0 : height);
                ctx.closePath();
                ctx.fill();
            }
        }
    }
    EchoPopupMotion {
        id: appearance
        surface: popup
        fromX: popup.side === EchoAnchoredPopup.Beside ? -4 : 0
        fromY: popup.side === EchoAnchoredPopup.Beside ? 0 : popup.placedAbove ? 4 : -4
    }
    enter: appearance.enter
    exit: appearance.exit
    EchoPopupAnchor {
        id: geometry
        item: popup.anchorItem
        coordinateItem: popup.parent
        onGeometryChanged: if (!geometry.available && popup.visible)
            popup.close()
    }
}
