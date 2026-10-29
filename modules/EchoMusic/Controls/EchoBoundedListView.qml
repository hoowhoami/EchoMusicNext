pragma ComponentBehavior: Bound
import QtQuick
import EchoMusic.Controls
import EchoMusic.Theme

// Auto-height for a vertical, sectionless list with a bounded viewport. A virtual
// ListView's contentHeight estimates unloaded rows, so it cannot size its own
// viewport. Start with the available budget; shrink only when every row is
// actually instantiated and native geometry supplies the complete extent.
// Large lists retain the budget and normal ListView virtualization. No hidden
// measurement delegates, text-layout copies, timers or scans beyond the mounted item budget are used.
ListView {
    id: view
    EchoViewPoolGuard {
        targetView: view
    }
    EchoWheelScroll.enabled: true
    EchoWheelScroll.motionEnabled: Theme.store.motionEnabled
    property real maximumViewportHeight: 320
    property real minimumViewportHeight: 0
    property real emptyViewportHeight: 0
    property real measuredExtent: -1
    // Inline native header/footer items share the same bounded viewport.
    readonly property real headerFooterExtent: (headerItem ? headerItem.height : 0) + (footerItem ? footerItem.height : 0)
    implicitHeight: Math.min(Math.max(0, maximumViewportHeight), Math.max(minimumViewportHeight, count === 0 ? emptyViewportHeight + headerFooterExtent : measuredExtent < 0 ? maximumViewportHeight : measuredExtent + headerFooterExtent))
    function invalidateExtent(): void {
        measuredExtent = -1;
        Qt.callLater(measureExtent);
    }
    function measureExtent(): void {
        if (!visible || width <= 0 || height <= 0 || count === 0)
            return;
        forceLayout();
        // currentItem can remain instantiated outside the contiguous viewport.
        // Seeing both endpoints alone therefore does not prove an exact extent.
        // Bound the scan by mounted children before checking native row presence.
        if (count > contentItem.children.length) {
            measuredExtent = -1;
            return;
        }
        for (let index = 0; index < count; ++index) {
            if (!itemAtIndex(index)) {
                measuredExtent = -1;
                return;
            }
        }
        const first = itemAtIndex(0), last = itemAtIndex(count - 1);
        measuredExtent = Math.max(0, last.y + last.height - first.y) + topMargin + bottomMargin;
    }
    onWidthChanged: invalidateExtent()
    onCountChanged: invalidateExtent()
    onModelChanged: invalidateExtent()
    onVisibleChanged: if (visible)
        invalidateExtent()
    // The estimated contentHeight need not change when the last incubated
    // delegate becomes ready. Observe native mounting and viewport readiness
    // as well, keeping the scan bounded by the actually mounted children.
    onHeightChanged: Qt.callLater(measureExtent)
    property Connections mountedRows: Connections {
        target: view.contentItem
        function onChildrenChanged() {
            Qt.callLater(view.measureExtent);
        }
    }
    onContentHeightChanged: Qt.callLater(measureExtent)
    Component.onCompleted: invalidateExtent()
}
