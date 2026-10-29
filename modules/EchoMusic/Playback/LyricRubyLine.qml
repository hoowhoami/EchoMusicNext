pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import EchoMusic.Theme
import EchoMusic.Playback

// Native word-group shaping. Flow wraps page/Mini lines; the desktop's Row
// retains legibility and follows the native word cursor without changing times.
Item {
    id: root
    property string effectSurface: ""
    property real followOverride: -1
    signal followRequested(real position, real maximum)
    property var units: []
    property font font: Theme.bodyFont
    property real timeMs: -1
    property int alignment: Qt.AlignLeft
    property bool current: false
    property real highlightAmount: 1
    property bool wrap: true
    property color playedColor: "white"
    property color unplayedColor: "gray"
    property bool animateWords: true
    property real arrangedWidth: 0
    property RubyUnit activeUnit: null
    onUnitsChanged: activeUnit = null
    function syncFocus(): void {
        if (wrap || !current)
            return;
        let selected = null;
        for (let i = 0; i < rowCells.count; ++i) {
            const cell = rowCells.itemAt(i) as RubyUnit;
            if (cell && cell.modelData.start_ms <= timeMs)
                selected = cell;
        }
        activeUnit = selected;
    }
    implicitHeight: wrap ? flow.implicitHeight : row.implicitHeight
    clip: !wrap
    readonly property real maximumScroll: Math.max(0, row.implicitWidth - width)
    readonly property real defaultScrollOffset: {
        if (!current || !units.length || timeMs < units[0].start_ms)
            return 0;
        const last = units[units.length - 1];
        if (timeMs >= last.start_ms + last.duration_ms)
            return maximumScroll;
        return activeUnit ? Math.max(0, Math.min(maximumScroll, activeUnit.x + activeUnit.sweepX - width * .6)) : 0;
    }
    readonly property real scrollOffset: followOverride >= 0 ? followOverride : defaultScrollOffset
    onDefaultScrollOffsetChanged: if (effectSurface === "desktop")
        followRequested(defaultScrollOffset, maximumScroll)
    Flow {
        id: flow
        visible: root.wrap
        x: root.alignment === Qt.AlignHCenter ? (root.width - root.arrangedWidth) / 2 : root.alignment === Qt.AlignRight ? root.width - root.arrangedWidth : 0
        width: root.width
        spacing: 0
        onPositioningComplete: if (Math.abs(root.arrangedWidth - childrenRect.width) > .01)
            root.arrangedWidth = childrenRect.width
        Repeater {
            model: root.wrap ? root.units : []
            delegate: RubyUnit {}
        }
    }
    Row {
        id: row
        visible: !root.wrap
        x: implicitWidth > root.width ? -root.scrollOffset : root.alignment === Qt.AlignHCenter ? (root.width - implicitWidth) / 2 : root.alignment === Qt.AlignRight ? root.width - implicitWidth : 0
        spacing: 0
        onPositioningComplete: root.syncFocus()
        Repeater {
            id: rowCells
            model: root.wrap ? [] : root.units
            delegate: RubyUnit {}
        }
    }
    component RubyUnit: Item {
        id: unit
        required property var modelData
        required property int index
        readonly property real rubyHeight: root.font.pixelSize * .8
        readonly property real naturalWidth: Math.max(baseMetrics.advanceWidth, rubyMetrics.advanceWidth) + 2
        readonly property real sweepX: base.readingPosition(root.timeMs) - root.font.pixelSize
        readonly property bool sweepActive: root.current && root.timeMs >= modelData.start_ms && root.timeMs < modelData.start_ms + modelData.duration_ms
        onSweepActiveChanged: if (sweepActive)
            root.activeUnit = unit
        Component.onCompleted: if (sweepActive)
            root.activeUnit = unit
        width: root.wrap ? Math.min(root.width, naturalWidth) : naturalWidth
        height: base.textHeight + rubyHeight
        TextMetrics {
            id: baseMetrics
            font: root.font
            text: unit.modelData.text
        }
        TextMetrics {
            id: rubyMetrics
            font: annotation.font
            text: annotation.text
        }
        Label {
            id: annotation
            width: parent.width
            height: unit.rubyHeight
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            text: unit.modelData.ruby
            textFormat: Text.PlainText
            font: Theme.store.styledFont(root.font, root.font.pixelSize * .6, root.font.weight, 0, false, false)
            color: root.current ? root.playedColor : root.unplayedColor
            opacity: .7
        }
        LyricLine {
            id: base
            EchoLyricInkPart {
                runKey: String(unit.modelData.start_ms) + ":" + String(unit.index)
                renderer: base
                surface: root.effectSurface
                active: root.effectSurface !== ""
            }
            x: -root.font.pixelSize
            y: unit.rubyHeight - root.font.pixelSize
            width: parent.width + root.font.pixelSize * 2
            height: implicitHeight
            text: unit.modelData.text
            words: unit.modelData.words
            startMs: unit.modelData.start_ms
            durationMs: unit.modelData.duration_ms
            font: root.font
            wrap: root.wrap
            alignment: Qt.AlignHCenter
            current: root.current
            highlightAmount: root.highlightAmount
            timeMs: root.timeMs
            playedColor: root.playedColor
            unplayedColor: root.unplayedColor
            animateWords: root.animateWords
        }
    }
}
