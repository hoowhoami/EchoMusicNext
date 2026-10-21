pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQml.Models

// Cold anchors retain only the recipe. Timers, window/overlay attached objects,
// cancellation subscriptions and the native Popup belong to one hover session.
QtObject {
    id: tip
    required property Item parent
    property string text: ""
    property string description: ""
    property bool requestedVisible: false
    property int delay: 500
    property real anchorPosition: 0.5
    property bool dismissedUntilHoverEnds: false
    readonly property HoverSession session: requests.count > 0 ? requests.objectAt(0) as HoverSession : null
    readonly property var hostWindow: session ? session.hostWindow : null
    readonly property Item overlayItem: session ? session.overlayItem : null
    readonly property bool eligible: session !== null && session.eligible
    readonly property bool triggered: eligible && !dismissedUntilHoverEnds
    property bool delayElapsed: false
    readonly property EchoToolTipPopup popup: session ? session.popup : null
    readonly property bool visible: popup !== null && popup.visible
    onRequestedVisibleChanged: {
        if (!requestedVisible)
            dismissedUntilHoverEnds = false;
    }
    onTriggeredChanged: {
        if (!triggered)
            delayElapsed = false;
    }
    onDelayChanged: delayElapsed = false
    // Stop the creation chain before window/overlay teardown changes bindings.
    Component.onDestruction: requests.active = false
    property Instantiator requests: Instantiator {
        model: 1
        active: tip.requestedVisible && tip.parent !== null && tip.parent.visible && tip.parent.enabled && (tip.description.length > 0 || tip.text.length > 0)
        delegate: HoverSession {
            controller: tip
        }
    }
    component HoverSession: QtObject {
        id: hover
        required property EchoToolTip controller
        readonly property var hostWindow: controller.parent ? controller.parent.Window.window : null
        readonly property Item overlayItem: controller.parent ? controller.parent.Overlay.overlay : null
        readonly property bool eligible: hostWindow !== null && hostWindow.visible && hostWindow.visibility !== Window.Minimized
        readonly property EchoToolTipPopup popup: instance.count > 0 ? instance.objectAt(0) as EchoToolTipPopup : null
        property Timer requestDelay: Timer {
            interval: Math.max(1, hover.controller.delay)
            running: hover.controller.triggered && hover.controller.delay > 0 && !hover.controller.delayElapsed
            onTriggered: hover.controller.delayElapsed = true
        }
        property Instantiator instance: Instantiator {
            model: 1
            active: hover.controller.triggered && (hover.controller.delay <= 0 || hover.controller.delayElapsed)
            delegate: EchoToolTipPopup {
                controller: hover.controller
                parent: controller.parent
                text: controller.text
                description: controller.description
                anchorPosition: controller.anchorPosition
            }
        }
        property Connections popupEvents: Connections {
            target: hover.controller.eligible ? EchoPopupCoordinator : null
            function onNativeMenuOpening() {
                hover.controller.dismissedUntilHoverEnds = true;
            }
            function onHintOpening(surface, overlay) {
                const hint = surface as EchoToolTipPopup;
                if ((!hint || hint.controller !== hover.controller) && overlay === hover.overlayItem)
                    hover.controller.dismissedUntilHoverEnds = true;
            }
            function onPopupOpening(surface, overlay) {
                if (overlay && overlay === hover.overlayItem)
                    hover.controller.dismissedUntilHoverEnds = true;
            }
        }
    }
}
