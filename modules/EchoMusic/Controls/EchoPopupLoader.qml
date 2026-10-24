pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Templates as T

// Owns a component recipe, never an invisible Popup. Keep the native Qt surface
// through its exit transition; release its entire QObject tree after closed.
QtObject {
    id: owner
    required property Item hostItem
    required property Component sourceComponent
    property Item popupParent: hostItem ? hostItem.Overlay.overlay : null
    property bool requestedVisible: false
    // A minimize is not a dismissal. Owners that opt in keep the surface (and
    // its open intent) so it is still there when the window comes back; window
    // hide still closes, because the host is really gone.
    property bool keepOnMinimize: false
    readonly property bool hostMinimized: hostWindow !== null && hostWindow.visible && hostWindow.visibility === Window.Minimized
    property T.Popup item: null
    readonly property bool visible: item !== null && item.visible
    readonly property bool opened: item !== null && item.opened
    readonly property var hostWindow: hostItem ? hostItem.Window.window : null
    readonly property bool available: hostItem !== null && hostItem.visible && hostItem.enabled && hostWindow !== null && hostWindow.visible && hostWindow.visibility !== Window.Minimized
    property int revision: 0

    function create(properties: var): T.Popup {
        if (!available || !sourceComponent)
            return null;
        ++revision;
        if (!item) {
            const initial = Object.assign({
                parent: popupParent
            }, properties || {});
            item = sourceComponent.createObject(owner, initial) as T.Popup;
        } else if (properties) {
            for (const key of Object.keys(properties))
                item[key] = properties[key];
        }
        return item;
    }
    function open(properties: var): void {
        const surface = create(properties);
        if (surface)
            surface.open();
    }
    // Menu.popup keeps Qt's item-relative coordinates and Popup.Window behavior.
    function popup(anchor: Item, x: real, y: real): void {
        const menu = create({
            parent: anchor
        }) as T.Menu;
        if (menu)
            menu.popup(anchor, x, y);
    }
    function close(): void {
        if (item) {
            item.close();
            if (!item.visible)
                releaseAfterClose();
        }
    }
    function releaseAfterClose(): void {
        const surface = item;
        const token = revision;
        Qt.callLater(function () {
            // The host can be destroyed before this queued close completes.
            if (owner && surface && owner.item === surface && token === owner.revision && !surface.visible) {
                owner.item = null;
                surface.destroy();
            }
        });
    }
    onRequestedVisibleChanged: requestedVisible ? open() : close()
    onAvailableChanged: {
        if (!available) {
            if (!(keepOnMinimize && hostMinimized))
                close();
        } else if (requestedVisible) {
            open();
        }
    }
    Component.onCompleted: if (requestedVisible)
        open()
    Component.onDestruction: if (item)
        item.close()
    property Connections surfaceEvents: Connections {
        target: owner.item
        function onClosed(): void {
            owner.releaseAfterClose();
        }
    }
}
