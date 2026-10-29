pragma ComponentBehavior: Bound
import QtQuick

// Native file dialogs have accepted/rejected rather than Popup.closed. Delay
// disposal until the original result handler has consumed selectedFiles.
QtObject {
    id: owner
    required property Item hostItem
    required property Component sourceComponent
    property EchoFileDialog item: null
    readonly property var hostWindow: hostItem ? hostItem.Window.window : null
    readonly property bool hostAlive: hostItem !== null && hostItem.visible && hostWindow !== null && hostWindow.visible && hostWindow.visibility !== Window.Minimized
    readonly property bool available: hostAlive && hostItem.enabled
    property int revision: 0
    function open(): void {
        if (!available || !sourceComponent)
            return;
        ++revision;
        if (!item)
            item = sourceComponent.createObject(owner, {
                parentWindow: hostItem.Window.window
            }) as EchoFileDialog;
        if (item)
            item.open();
    }
    function close(): void {
        if (item) {
            item.close();
            releaseAfterClose();
        }
    }
    function releaseAfterClose(): void {
        const dialog = item;
        const token = revision;
        Qt.callLater(function () {
            if (dialog && owner.item === dialog && token === owner.revision && !dialog.visible) {
                owner.item = null;
                dialog.destroy();
            }
        });
    }
    Component.onDestruction: if (item)
        item.close()
    property Connections resultEvents: Connections {
        target: owner.item
        function onVisibleChanged(): void {
            if (owner.item && !owner.item.visible)
                owner.releaseAfterClose();
        }
    }
    onHostAliveChanged: if (!hostAlive)
        close()
    onAvailableChanged: if (!available && !(hostAlive && EchoPopupCoordinator.interactionSuspended(hostWindow)))
        close()
}
