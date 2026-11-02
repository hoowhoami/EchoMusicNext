pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Window
import EchoMusic.Shell

Item {
    id: host
    property PluginSurfaceModel model: null
    required property string target
    property int revision: 0
    readonly property var hostWindow: Window.window
    readonly property bool live: visible && width > 0 && hostWindow && hostWindow.visible && hostWindow.visibility !== Window.Minimized
    readonly property var contributions: model ? model.entries.filter(function (entry) {
        return entry.target === host.target;
    }) : []
    implicitHeight: {
        const changed = revision;
        let maximum = 0;
        for (let i = 0; i < surfaces.count; ++i) {
            const item = surfaces.itemAt(i);
            if (item)
                maximum = Math.max(maximum, item.implicitHeight);
        }
        return maximum;
    }
    Repeater {
        id: surfaces
        model: host.live ? host.contributions : []
        onItemAdded: ++host.revision
        onItemRemoved: ++host.revision
        delegate: Item {
            id: surface
            required property var modelData
            anchors.fill: parent
            implicitHeight: loader.item instanceof Item ? (loader.item as Item).implicitHeight : 0
            Loader {
                id: loader
                anchors.fill: parent
                asynchronous: true
                // Async delegates may finish after Repeater.itemAdded; notify
                // natural-height consumers once the plugin item exists.
                onLoaded: ++host.revision
                Component.onCompleted: setSource(surface.modelData.component, {
                    plugin: surface.modelData.scope,
                    placement: host.target
                })
            }
        }
    }
}
