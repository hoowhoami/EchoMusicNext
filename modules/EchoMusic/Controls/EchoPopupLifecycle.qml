pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Templates as T

QtObject {
    id: lifecycle
    required property T.Popup surface
    property bool suspensionExempt: false
    readonly property bool interactionSuspended: !suspensionExempt && EchoPopupCoordinator.interactionSuspended(surface.parent ? surface.parent.Window.window : null)
    // Preserve the original bindings and exact popup instance. Verification is
    // a temporary input cover, never a dismissal or a business cancellation.
    property Binding suspendedInput: Binding {
        target: lifecycle.surface
        property: "enabled"
        when: lifecycle.interactionSuspended
        value: false
        restoreMode: Binding.RestoreBindingOrValue
    }
    property Binding suspendedDismissal: Binding {
        target: lifecycle.surface
        property: "closePolicy"
        when: lifecycle.interactionSuspended
        value: T.Popup.NoAutoClose
        restoreMode: Binding.RestoreBindingOrValue
    }
    property EchoPopupContentClip contentClip: EchoPopupContentClip {
        surface: lifecycle.surface
    }
    // The public Template base covers Popup, Dialog, Drawer and Menu styles.
    // Connections preserves derived onAboutToShow handlers (draft preparation).
    property Connections connection: Connections {
        target: lifecycle.surface
        function onAboutToShow() {
            EchoPopupCoordinator.popupOpening(lifecycle.surface, lifecycle.surface.parent ? lifecycle.surface.parent.Overlay.overlay : null);
        }
    }
}
