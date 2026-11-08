pragma ComponentBehavior: Bound
import QtQuick
import EchoMusic.Theme

QtObject {
    id: geometry
    property QtObject uiColumnAdapter: null
    readonly property list<string> uiColumnKeys: ["index", "title", "album", "duration"]
    property int rowCount: 0
    readonly property real albumWidth: 142
    readonly property real lyricWidth: 260
    readonly property real durationWidth: 68
    readonly property font numberFont: digitMetrics.font
    readonly property FontMetrics digitMetrics: FontMetrics {
        font: Theme.font(12, Theme.regularWeight)
    }
    // Measure ten glyphs once per font change, never all model rows or each
    // recycled delegate. The widest digit covers proportional numeral fonts.
    // The argument makes font notifications an explicit binding dependency;
    // calling FontMetrics' imperative methods alone does not capture it.
    readonly property real widestDigit: measureWidestDigit(numberFont)
    function measureWidestDigit(measuredFont: font): real {
        let width = 0;
        for (let digit = 0; digit < 10; ++digit)
            width = Math.max(width, digitMetrics.advanceWidth(String(digit)));
        return width;
    }
    // EchoMusic's normal column is 40px, with the number/play center at x=16.
    // Only widen when the maximum displayed ordinal cannot fit with an 8px
    // cover gap. All rows, the header and skeleton share this geometry.
    readonly property real indexWidth: Math.max(40, Math.ceil(widestDigit * String(Math.max(1, rowCount)).length) + 8)
}
