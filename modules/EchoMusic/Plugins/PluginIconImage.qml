pragma ComponentBehavior: Bound
import QtQuick
import EchoMusic.Controls
import EchoMusic.Plugins

// Artwork follows the marketplace accelerator and retries its original URL once.
// EchoCover owns visible-only I/O, decode budgets and release when the delegate exits.
EchoCover {
    id: icon
    required property PluginManager manager
    property url iconSource
    property url primarySource
    property url originSource
    property bool originFallback: false
    property bool initialized: false
    property bool originFallbackQueued: false
    source: originFallback ? originSource : primarySource
    function refresh() {
        if (!initialized)
            return;
        // Retire a failed request even when only the applied proxy policy changed.
        primarySource = "";
        originSource = "";
        originFallback = false;
        originFallbackQueued = false;
        if (manager && iconSource.toString().length > 0) {
            originSource = manager.iconRequestUrl(iconSource, true);
            primarySource = manager.iconRequestUrl(iconSource, false);
        }
    }
    onIconSourceChanged: refresh()
    function applyOriginFallback() {
        if (!originFallbackQueued)
            return;
        originFallbackQueued = false;
        originFallback = true;
    }
    onStatusChanged: {
        if (status === Image.Error && !originFallback && primarySource.toString() !== originSource.toString()) {
            // Changing Image.source during status notification re-enters its
            // read-only status binding. Queue one owned, coalesced hand-off.
            originFallbackQueued = true;
            Qt.callLater(applyOriginFallback);
        }
    }
    Connections {
        target: icon.manager
        function onIconNetworkChanged() {
            icon.refresh();
        }
    }
    Component.onCompleted: {
        initialized = true;
        refresh();
    }
}
