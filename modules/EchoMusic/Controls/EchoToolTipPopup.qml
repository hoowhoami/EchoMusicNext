pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import EchoMusic.Controls
import EchoMusic.Theme

ToolTip {
    id: tip
    required property EchoToolTip controller
    property string description: ""
    // Public parent remains the trigger for ownership and lifecycle. Popup.Item
    // paints in the overlay, so both its scale and placement need scene geometry.
    property real anchorPosition: 0.5
    readonly property Item overlayItem: parent ? parent.Overlay.overlay : null
    readonly property var hostWindow: parent ? parent.Window.window : null
    // The overlay itself changes visibility while Popup opens. Cache availability
    // so an unchanged geometry notification cannot re-enter visible's binding.
    readonly property bool anchorAvailable: anchorGeometry.available
    // UI zoom is window geometry, not the trigger's hover/press animation scale.
    readonly property real uiScale: hostWindow && typeof hostWindow.uiScale === "number" ? hostWindow.uiScale : 1
    readonly property real edge: 8 * uiScale
    readonly property real gap: 6 * uiScale
    readonly property real aboveSpace: Math.max(0, anchorGeometry.rect.y - edge - gap)
    readonly property real belowSpace: Math.max(0, (overlayItem ? overlayItem.height : 0) - anchorGeometry.rect.y - anchorGeometry.rect.height - edge - gap)
    readonly property real placementSpace: Math.max(aboveSpace, belowSpace)
    readonly property bool placedAbove: aboveSpace >= implicitHeight * uiScale || aboveSpace >= belowSpace
    readonly property real windowX: Math.max(edge, Math.min(anchorGeometry.rect.x + anchorGeometry.rect.width * anchorPosition - width * uiScale / 2, (overlayItem ? overlayItem.width : 0) - edge - width * uiScale))
    readonly property real windowY: placedAbove ? anchorGeometry.rect.y - gap - height * uiScale : anchorGeometry.rect.y + anchorGeometry.rect.height + gap
    readonly property point anchorOffset: parent && overlayItem ? parent.mapFromItem(overlayItem, windowX, windowY) : Qt.point(0, 0)
    EchoPopupAnchor {
        id: anchorGeometry
        item: tip.parent
        coordinateItem: item ? tip.overlayItem : null
    }
    // HoverSession cancels on passive pointer observation. Overlay auto-close
    // must not consume one half of the underlying control's press/release pair.
    closePolicy: Popup.NoAutoClose
    popupType: Popup.Item
    scale: uiScale
    transformOrigin: Popup.TopLeft
    x: anchorOffset.x
    y: anchorOffset.y
    margins: edge
    height: Math.min(implicitHeight, placementSpace / Math.max(uiScale, 0.01))
    visible: anchorAvailable && parent.enabled && (description.length > 0 || text.length > 0) && uiScale > 0 && placementSpace >= (18 + topPadding + bottomPadding) * uiScale
    onAboutToShow: EchoPopupCoordinator.hintOpening(tip, overlayItem)
    onClosed: {
        if (controller && controller.requestedVisible)
            controller.dismissedUntilHoverEnds = true;
    }
    // The trigger owns the cancellable wait; a swept pointer must not create
    // and destroy native Popups for hints that never reach their show delay.
    delay: 0
    padding: 6
    leftPadding: 9
    rightPadding: 9
    implicitWidth: Math.max(1, Math.min(300, (overlayItem ? overlayItem.width : 300) / Math.max(uiScale, 0.01) - 16, contentItem.implicitWidth + leftPadding + rightPadding))
    contentItem: Text {
        textFormat: Text.PlainText
        text: tip.description.length ? tip.description : tip.text
        color: Theme.floatingText
        font: Theme.font(12, Theme.regularWeight)
        wrapMode: Text.WrapAnywhere
        maximumLineCount: Math.max(1, Math.floor((tip.placementSpace / Math.max(tip.uiScale, 0.01) - tip.topPadding - tip.bottomPadding) / 18))
        elide: Text.ElideRight
        lineHeightMode: Text.FixedHeight
        lineHeight: 18
    }
    background: Loader {
        active: tip.visible
        sourceComponent: EchoFloatingSurface {
            id: tooltipSurface
            shadowEnabled: false
            frostedEnabled: false
            color: Theme.elevated
            radius: 6
            border.width: 1
            border.color: Theme.floating.surfaceOutline
            EchoSurfaceShadow {
                anchors.fill: parent
                surface: tooltipSurface
                z: -1
                radius: 6
                blur: 8
                offset: Qt.vector2d(0, 2)
                color: Qt.rgba(0, 0, 0, Theme.dark ? 0.28 : 0.12)
            }
        }
    }
}
