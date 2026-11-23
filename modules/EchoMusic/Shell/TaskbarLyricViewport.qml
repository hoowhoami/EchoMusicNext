pragma ComponentBehavior: Bound
import QtQuick
import EchoMusic.Theme

// Bounded presentation snapshots only; the caller owns lyrics and playback time.
Item {
    id: root
    property var source: null
    property real timeMs: 0
    property real fontSize: 14
    property bool anchorRight: false
    property color foreground: Theme.text
    property color secondaryColor: Theme.secondaryText
    property color accent: Theme.accent
    property bool animate: true
    readonly property bool motionAllowed: animate && visible && Theme.store.motionEnabled
    property bool rolling: false
    readonly property real naturalWidth: Math.max(primaryRow ? primaryRow.contentWidth : 0, secondaryShown && secondaryRow ? secondaryRow.contentWidth * 0.82 : 0)
    readonly property bool secondaryShown: !!(presented && presented.secondary) && height >= 26
    readonly property real primaryY: (height - primaryMetrics.height - (secondaryShown ? primaryMetrics.height * 0.82 + 1 : 0)) / 2
    readonly property real secondaryY: primaryY + primaryMetrics.height + 1
    property var presented: null
    property real progress: 1
    property LyricRow primaryRow: rowA
    property LyricRow secondaryRow: rowB
    property bool queued: false
    property bool resetPending: false
    clip: true

    TextMetrics {
        id: primaryMetrics
        text: "Ag国"
        font.family: Theme.store.taskbarLyricBaseFont.family
        font.pixelSize: root.fontSize
    }

    component LyricRow: TaskbarLyricLine {
        property var payload: null
        property bool primary: false
        property bool frozen: false
        property real frozenTime: 0
        property real fromY: 0
        property real toY: 0
        property real fromSize: 1
        property real toSize: 1
        property real fromInk: 1
        property real toInk: 1
        property real fromOpacity: 1
        property real toOpacity: 1
        readonly property real phase: root.progress
        readonly property real ink: fromInk + (toInk - fromInk) * phase
        // Keep one shaped font size; the scene graph scales the same glyphs
        // continuously instead of stepping through integer pixel sizes.
        scale: fromSize + (toSize - fromSize) * phase
        transformOrigin: root.anchorRight ? Item.TopRight : Item.TopLeft
        width: root.width / scale
        x: root.anchorRight ? root.width - width : 0
        height: implicitHeight
        y: fromY + (toY - fromY) * phase
        opacity: fromOpacity + (toOpacity - fromOpacity) * phase
        text: payload ? (primary ? payload.primary : payload.secondary) : ""
        units: primary && payload ? payload.units : []
        wordByWord: primary && payload ? payload.wordByWord : false
        // A preview keeps its text head in view until it becomes the active line.
        timeMs: !primary && payload && payload.preview ? startMs : frozen ? frozenTime : root.timeMs
        startMs: payload ? payload.startMs : 0
        endTimeMs: payload ? payload.endTimeMs : 0
        timeOffset: payload ? payload.offsetMs : 0
        anchorRight: root.anchorRight
        fontSize: root.fontSize
        baseColor: Qt.rgba(root.secondaryColor.r + (root.foreground.r - root.secondaryColor.r) * ink, root.secondaryColor.g + (root.foreground.g - root.secondaryColor.g) * ink, root.secondaryColor.b + (root.foreground.b - root.secondaryColor.b) * ink, root.secondaryColor.a + (root.foreground.a - root.secondaryColor.a) * ink)
        accentColor: primary ? root.accent : root.secondaryColor
    }
    LyricRow {
        id: rowA
    }
    LyricRow {
        id: rowB
    }
    LyricRow {
        id: rowC
        visible: false
    }
    LyricRow {
        id: rowD
        visible: false
    }

    function rows(): var {
        return [rowA, rowB, rowC, rowD];
    }
    function pose(row, fromY, toY, fromSize, toSize, fromInk, toInk, fromOpacity, toOpacity): void {
        row.fromY = fromY;
        row.toY = toY;
        row.fromSize = fromSize;
        row.toSize = toSize;
        row.fromInk = fromInk;
        row.toInk = toInk;
        row.fromOpacity = fromOpacity;
        row.toOpacity = toOpacity;
    }
    function settle(): void {
        if (!primaryRow || !secondaryRow)
            return;
        motion.stop();
        rolling = false;
        primaryRow.primary = true;
        secondaryRow.primary = false;
        progress = 1;
        for (const row of rows()) {
            row.visible = row === primaryRow || (row === secondaryRow && secondaryShown);
            row.frozen = false;
            if (row !== primaryRow && row !== secondaryRow)
                row.payload = null;
        }
        pose(primaryRow, primaryY, primaryY, 1, 1, 1, 1, 1, 1);
        pose(secondaryRow, secondaryY, secondaryY, 0.82, 0.82, 0, 0, 1, 1);
    }
    function resetTimeline(): void {
        resetPending = true;
        requestUpdate();
    }
    function requestUpdate(): void {
        if (queued)
            return;
        queued = true;
        Qt.callLater(updateLines);
    }
    // Coalesce text, times and subtitle bindings into one settled snapshot.
    function updateLines(): void {
        queued = false;
        if (!source)
            return;
        const old = presented;
        const fresh = source;
        const sameLine = old && old.trackId === fresh.trackId && old.index === fresh.index && old.startMs === fresh.startMs && old.primary === fresh.primary && old.secondary === fresh.secondary && old.preview === fresh.preview;
        // Metadata and timing refreshes must not snap an in-flight promotion.
        if (sameLine && !resetPending && motionAllowed) {
            presented = fresh;
            primaryRow.payload = fresh;
            secondaryRow.payload = fresh;
            return;
        }
        settle();
        const oldPrimaryY = primaryY;
        const oldSecondaryY = secondaryY;
        const oldSecondaryShown = secondaryShown;
        const advance = old && old.hasLine && fresh.hasLine && old.preview && oldSecondaryShown && old.trackId === fresh.trackId && old.nextIndex === fresh.index && old.nextStartMs === fresh.startMs && old.secondary === fresh.primary;
        const oldPrimary = primaryRow;
        const oldSecondary = secondaryRow;
        const free = rows().filter(row => row !== oldPrimary && row !== oldSecondary);
        const reset = resetPending;
        resetPending = false;
        rolling = !!old && !sameLine && !reset && motionAllowed;
        presented = fresh;
        if (!old || sameLine || reset || !motionAllowed) {
            primaryRow.payload = fresh;
            secondaryRow.payload = fresh;
            settle();
            return;
        }
        primaryRow = advance ? oldSecondary : free[0];
        secondaryRow = advance ? free[0] : free[1];
        primaryRow.primary = true;
        primaryRow.payload = fresh;
        secondaryRow.primary = false;
        secondaryRow.payload = fresh;
        const distance = advance ? oldSecondaryY - primaryY : height;
        oldPrimary.frozenTime = timeMs;
        oldPrimary.frozen = true;
        oldPrimary.visible = true;
        pose(oldPrimary, oldPrimaryY, oldPrimaryY - distance, 1, 1, 1, 1, 1, 0);
        if (!advance) {
            oldSecondary.frozenTime = timeMs;
            oldSecondary.frozen = true;
            oldSecondary.visible = oldSecondaryShown;
            pose(oldSecondary, oldSecondaryY, oldSecondaryY - distance, 0.82, 0.82, 0, 0, 1, 0);
        }
        primaryRow.visible = true;
        secondaryRow.visible = secondaryShown;
        pose(primaryRow, advance ? oldSecondaryY : primaryY + distance, primaryY, advance ? 0.82 : 1, 1, advance ? 0 : 1, 1, advance ? 1 : 0, 1);
        pose(secondaryRow, secondaryY + distance, secondaryY, 0.82, 0.82, 0, 0, 0, 1);
        progress = 0;
        motion.start();
    }
    onSourceChanged: requestUpdate()
    onMotionAllowedChanged: {
        if (!motionAllowed)
            settle();
    }
    onPrimaryYChanged: {
        if (!rolling)
            settle();
    }
    onSecondaryYChanged: {
        if (!rolling)
            settle();
    }
    onHeightChanged: settle()
    onFontSizeChanged: settle()
    Component.onCompleted: requestUpdate()
    NumberAnimation {
        id: motion
        target: root
        property: "progress"
        from: 0
        to: 1
        duration: Theme.taskbarLyricRollDuration
        easing.type: Easing.BezierSpline
        easing.bezierCurve: Theme.easeStandard
        onFinished: root.settle()
    }
}
