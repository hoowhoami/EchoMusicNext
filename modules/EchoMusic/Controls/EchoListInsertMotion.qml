pragma ComponentBehavior: Bound
import QtQuick
import EchoMusic.Theme
import EchoMusic.Controls

// History's 280ms scroll-in is shared by all song views. Model insertions and
// promotions trigger one content animation, never delegate creation/reuse.
QtObject {
    id: motion
    objectName: "listInsertMotion"
    required property ListView view
    property bool active: true
    property real rowHeight: 60
    property real occlusionHeight: 0
    property int observedCount: 0
    property bool wasAtStart: false
    readonly property bool running: slide.running
    function inserted(first: int, last: int): void {
        const presentation = motion.view.EchoPageState.presentation;
        if (presentation && presentation.suppressEntry)
            return;
        if (!slide.allowed)
            return;
        // Fixed song rows let us reject offscreen pagination without forcing
        // layout or instantiating delegates. originY belongs to ListView.
        const stride = rowHeight + view.spacing;
        const top = view.originY + first * stride;
        const bottom = view.originY + last * stride + rowHeight;
        if (bottom > view.contentY + Math.max(0, occlusionHeight) && top < view.contentY + view.height)
            slide.play();
    }
    function promote(): void {
        inserted(0, 0);
    }
    function keepBeginning(): void {
        view.positionViewAtBeginning();
        // ListView's positioning API aligns the first row, excluding the
        // positive margin reserved for a page's expanded sticky header.
        // Restore the logical page start within the same model notification.
        view.contentY = view.originY - view.topMargin;
    }
    property EchoSlideMotion slide: EchoSlideMotion {
        target: motion.view.contentItem
        active: motion.active
        fromY: -52
        fromOpacity: 0.92
        duration: Theme.songInsertDuration
        curve: Theme.easeSongInsert
    }
    property Connections modelEvents: Connections {
        target: Qt.isQtObject(motion.view.model) ? motion.view.model : null
        ignoreUnknownSignals: true
        function onRowsAboutToBeInserted(parent, first, last): void {
            motion.wasAtStart = motion.view.contentY <= motion.view.originY - motion.view.topMargin + 0.5;
        }
        function onRowsInserted(parent, first, last): void {
            // Native ListView preserves the old first delegate when prepending.
            // At logical top, keep the beginning visible instead. Scrolled
            // readers retain their native anchor and never jump to the new row.
            if (first === 0 && motion.wasAtStart)
                motion.keepBeginning();
            motion.inserted(first, last);
        }
        function onRowsAboutToBeMoved(parent, first, last, destination, row): void {
            motion.wasAtStart = motion.view.contentY <= motion.view.originY - motion.view.topMargin + 0.5;
        }
        function onRowsMoved(parent, first, last, destination, row): void {
            if (row === 0 && motion.wasAtStart)
                motion.keepBeginning();
            if (row < first)
                motion.inserted(row, row + last - first);
        }
        function onModelAboutToBeReset(): void {
            motion.slide.finish();
        }
    }
    property Connections viewEvents: Connections {
        target: motion.view
        function onCountChanged(): void {
            const count = motion.view.count;
            if (typeof motion.view.model === "number" && count > motion.observedCount)
                motion.inserted(motion.observedCount, count - 1);
            motion.observedCount = count;
        }
    }
}
