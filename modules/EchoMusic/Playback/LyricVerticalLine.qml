pragma ComponentBehavior: Bound
import QtQuick
import EchoMusic.Theme
import EchoMusic.Playback

Item {
    id: root
    property string effectSurface: ""
    property real followOverride: -1
    signal followRequested(real position, real maximum)
    property var units: []
    property font font: Theme.bodyFont
    property real timeMs: -1
    property int alignment: Qt.AlignTop
    property bool current: false
    property color playedColor: "white"
    property color unplayedColor: "gray"
    property size arrangedSize: Qt.size(0, 0)
    property VerticalUnit activeUnit: null
    readonly property real defaultScrollY: activeUnit && activeUnit.height > height ? Math.max(0, Math.min(Math.max(0, arrangedSize.height - height), activeUnit.y + activeUnit.sweepY - height * .6)) : 0
    readonly property real scrollY: followOverride >= 0 ? followOverride : defaultScrollY
    onDefaultScrollYChanged: if (effectSurface === "desktop")
        followRequested(defaultScrollY, Math.max(0, arrangedSize.height - height))
    property real scrollShift: 0
    property bool animateScroll: false
    property int focusedIndex: -1
    onUnitsChanged: {
        activeUnit = null;
        animateScroll = false;
        focusedIndex = -1;
        scrollShift = 0;
    }
    clip: true
    function revealCell(cell: Item, index: int): void {
        const base = width - arrangedSize.width;
        const left = base + cell.x;
        let shift = scrollShift;
        if (left + shift < 0)
            shift = -left;
        else if (left + cell.width + shift > width)
            shift = width - left - cell.width;
        animateScroll = focusedIndex >= 0 && Math.abs(index - focusedIndex) <= 1 && Theme.store.motionEnabled;
        focusedIndex = index;
        scrollShift = Math.max(0, Math.min(Math.max(0, arrangedSize.width - width), shift));
    }
    Behavior on scrollShift {
        enabled: root.animateScroll
        NumberAnimation {
            duration: 180
            easing.type: Easing.OutCubic
        }
    }
    function resetFollow(): void {
        animateScroll = false;
        focusedIndex = -1;
        syncFocus();
    }
    function syncFocus(): void {
        if (!current)
            return;
        let selected = null;
        let selectedIndex = -1;
        for (let i = 0; i < cells.count; ++i) {
            const cell = cells.itemAt(i) as VerticalUnit;
            if (cell && cell.modelData.start_ms + cell.modelData.from * cell.modelData.duration_ms <= timeMs) {
                selected = cell;
                selectedIndex = i;
            }
        }
        activeUnit = selected;
        if (selected)
            revealCell(selected, selectedIndex);
    }
    implicitWidth: flow.implicitWidth
    Flow {
        id: flow
        x: root.width - root.arrangedSize.width + root.scrollShift
        y: root.arrangedSize.height > root.height ? -root.scrollY : root.alignment === Qt.AlignVCenter ? Math.max(0, (root.height - root.arrangedSize.height) / 2) : root.alignment === Qt.AlignBottom ? Math.max(0, root.height - root.arrangedSize.height) : 0
        height: root.height
        flow: Flow.TopToBottom
        layoutDirection: Qt.RightToLeft
        spacing: 2
        onPositioningComplete: {
            root.arrangedSize = Qt.size(childrenRect.width, childrenRect.height);
            root.syncFocus();
        }
        Repeater {
            id: cells
            model: root.units
            delegate: VerticalUnit {}
        }
    }
    component VerticalUnit: Item {
        id: unit
        required property var modelData
        required property int index
        readonly property bool sweepActive: root.current && root.timeMs >= modelData.start_ms + modelData.from * modelData.duration_ms && root.timeMs < modelData.start_ms + modelData.to * modelData.duration_ms
        readonly property real sweepY: modelData.sideways ? ink.readingPosition(ink.timeMs) - root.font.pixelSize : 0
        onSweepActiveChanged: if (sweepActive) {
            root.activeUnit = unit;
            root.revealCell(unit, index);
        }
        Component.onCompleted: if (sweepActive)
            root.activeUnit = unit
        TextMetrics {
            id: metrics
            font: root.font
            text: unit.modelData.text
        }
        readonly property real extent: Math.max(root.font.pixelSize, metrics.advanceWidth)
        width: root.font.pixelSize * 1.25
        height: unit.modelData.sideways ? extent : root.font.pixelSize * 1.2
        LyricLine {
            id: ink
            EchoLyricInkPart {
                runKey: String(unit.modelData.start_ms) + ":" + String(unit.index)
                renderer: ink
                surface: root.effectSurface
                active: root.effectSurface !== ""
            }
            // Each piece is shaped by Qt; a Latin run rotates together, while CJK
            // remains upright. Sweep fractions are presentation coordinates, not new lyric times.
            x: unit.modelData.sideways ? (unit.width - unit.extent) / 2 - root.font.pixelSize : -root.font.pixelSize
            y: unit.modelData.sideways ? (unit.height - textHeight) / 2 - root.font.pixelSize : -root.font.pixelSize
            width: (unit.modelData.sideways ? unit.extent : unit.width) + 2 * root.font.pixelSize
            height: implicitHeight
            rotation: unit.modelData.sideways ? 90 : 0
            transformOrigin: Item.Center
            words: unit.modelData.wordTimed ? [
                {
                    text: unit.modelData.text,
                    start_ms: unit.modelData.start_ms,
                    duration_ms: unit.modelData.duration_ms
                }
            ] : []
            text: unit.modelData.text
            font: root.font
            alignment: Qt.AlignHCenter
            current: root.current
            startMs: unit.modelData.start_ms
            durationMs: unit.modelData.duration_ms
            timeMs: unit.modelData.start_ms + (root.timeMs - unit.modelData.start_ms - unit.modelData.from * unit.modelData.duration_ms) / Math.max(.0001, unit.modelData.to - unit.modelData.from)
            playedColor: root.playedColor
            unplayedColor: root.unplayedColor
            animateWords: false
        }
    }
}
