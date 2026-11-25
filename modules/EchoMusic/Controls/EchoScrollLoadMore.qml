pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Window

// One deferred viewport check; the domain still owns requests, admission and
// errors. Failed pages wait for an explicit retry instead of looping at EOF.
QtObject {
    id: observer
    required property Flickable view
    property bool active: true
    property bool hasMore: false
    property bool loading: false
    property bool failed: false
    property int itemCount: 0
    property real preloadMargin: 240
    readonly property bool presented: view.visible && view.Window.window !== null && view.Window.window.visible && view.Window.window.visibility !== Window.Minimized
    signal loadMoreRequested
    function schedule(): void {
        if (active && !task.running)
            task.start();
    }
    function check(): void {
        if (!active || !presented || view.height <= 0 || itemCount <= 0 || !hasMore || loading || failed)
            return;
        const remaining = view.contentHeight - (view.contentY - view.originY + view.height);
        if (remaining <= preloadMargin)
            loadMoreRequested();
    }
    onActiveChanged: {
        if (active)
            schedule();
        else
            task.stop();
    }
    onHasMoreChanged: schedule()
    onLoadingChanged: schedule()
    onFailedChanged: schedule()
    onItemCountChanged: schedule()
    onPresentedChanged: schedule()
    onPreloadMarginChanged: schedule()
    Component.onCompleted: schedule()
    property Timer task: Timer {
        interval: 0
        onTriggered: observer.check()
    }
    property Connections events: Connections {
        target: observer.view
        function onContentYChanged(): void {
            observer.schedule();
        }
        function onContentHeightChanged(): void {
            observer.schedule();
        }
        function onHeightChanged(): void {
            observer.schedule();
        }
        function onVisibleChanged(): void {
            observer.schedule();
        }
    }
}
