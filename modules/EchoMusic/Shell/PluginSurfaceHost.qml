pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Window
import EchoMusic.Shell
import EchoMusic.Controls

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
    EchoUiPart {
        target: host
        role: "extension.surface"
        key: host.target
        state: ({
                placement: host.target
            })
        properties: []
    }
    implicitHeight: {
        const changed = revision;
        let maximum = 0;
        for (const child of children)
            maximum = Math.max(maximum, child.implicitHeight);
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
            EchoUiScope.owner: modelData.scope.id
            EchoUiScope.surface: host.target.startsWith("mini.") ? "mini" : host.target.startsWith("lyrics.") ? "playback" : "main"
            EchoUiScope.key: "surface:" + modelData.key
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
