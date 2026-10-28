pragma ComponentBehavior: Bound
pragma Singleton
import QtQuick
import QtQuick.Templates as T

// Qt owns the actual popup stack, modal blocking, Escape and focus restoration.
// This window-scoped notification only coordinates product rules such as
// dismissing search suggestions before another surface opens; it stores no stack.
QtObject {
    id: coordinator
    // Only the verification presentation owns this window-scoped suspension.
    // Store window identities, never popup instances or business draft state.
    property var suspendedWindows: []
    property var focusRestorations: []
    function interactionSuspended(window: var): bool {
        return window !== null && suspendedWindows.indexOf(window) >= 0;
    }
    function preservingFocus(window: var): bool {
        return focusRestorations.some(entry => entry.window === window);
    }
    function setInteractionSuspended(window: var, suspended: bool): void {
        if (!window || interactionSuspended(window) === suspended)
            return;
        const previous = focusRestorations.find(entry => entry.window === window);
        if (suspended) {
            const entry = {
                window: window,
                focusItem: previous ? previous.focusItem : window.activeFocusItem
            };
            focusRestorations = focusRestorations.filter(record => record.window !== window).concat([entry]);
            suspendedWindows = suspendedWindows.concat([window]);
            return;
        }
        suspendedWindows = suspendedWindows.filter(w => w !== window);
        // Restore input after the covered tree is enabled and Qt has closed the
        // verification popup. Focus-loss observers must not dismiss old surfaces
        // during this handover. A new verification replaces this record.
        Qt.callLater(function () {
            if (focusRestorations.find(entry => entry.window === window) !== previous || interactionSuspended(window))
                return;
            const focusItem = previous ? previous.focusItem : null;
            if (window && focusItem && focusItem.Window.window === window && focusItem.visible && focusItem.enabled)
                focusItem.forceActiveFocus(Qt.OtherFocusReason);
            focusRestorations = focusRestorations.filter(record => record !== previous);
        });
    }

    // Native Qt dialogs have no Templates.Popup instance. Their notification
    // passes a null surface with the owning window's overlay instead.
    signal popupOpening(T.Popup surface, Item overlay)
    // App-level system/tray menus can be invoked while another app window is
    // presenting Mini or desktop lyrics. Cancel hints/search across those hosts.
    signal nativeMenuOpening
    // Hints do not join the business popup lifecycle; only one can explain the
    // current pointer target in a window, including nested hover targets.
    signal hintOpening(T.Popup surface, Item overlay)
}
