pragma ComponentBehavior: Bound
import QtQuick
import EchoMusic.Theme

// The same shaped-ink contract as EchoTextTagRow, without tag slots or a Flow.
// Plain labels keep native elision/wrapping and fractional baseline placement.
Item {
    id: line
    property string text: ""
    property font font: Theme.font(14, Theme.regularWeight)
    property color color: Theme.text
    property real lineHeight: font.pixelSize * 1.5
    property bool wrap: false
    property bool fillTextWidth: false
    // Width-only changes must not recenter a name when elision removes tall
    // glyphs or descenders. Opt in where actions change the available width.
    property bool stableElisionBaseline: false
    property int horizontalAlignment: Text.AlignLeft
    property alias textItem: label
    readonly property alias textBox: line
    // These projections are needed by only some consumers. Evaluate inside
    // their binding instead of installing two more bindings on every label.
    function isTruncated(): bool {
        return !wrap && (stableElisionBaseline ? label.truncated : ink.displayText !== text);
    }
    function firstLineInkCenter(): real {
        // Stable elision exposes the full name's alignment reference, so the
        // remaining glyphs keep their baseline when the visible suffix changes.
        return label.y + label.baselineOffset + ink.firstLineInkCenterFromBaseline;
    }
    implicitWidth: ink.naturalWidth
    // Preserve the fractional line box, including Flow's empty-width case.
    implicitHeight: label.width > 0 ? lineHeight * Math.max(1, label.lineCount) : 0
    EchoTextInkMetrics {
        id: ink
        font: line.font
        text: line.text
        layoutWidth: label.width
        lineHeight: line.lineHeight
        wrap: line.wrap
        elide: !line.wrap && !line.stableElisionBaseline
    }
    Text {
        id: label
        width: Math.max(0, line.fillTextWidth ? line.width : Math.min(line.width, ink.naturalWidth))
        height: implicitHeight
        y: ink.hasInk ? line.height / 2 - baselineOffset - ink.inkCenterFromBaseline : (line.height - height) / 2
        text: line.stableElisionBaseline && !line.wrap ? line.text : ink.displayText
        elide: line.stableElisionBaseline && !line.wrap ? Text.ElideRight : Text.ElideNone
        Accessible.role: Accessible.StaticText
        Accessible.name: line.text
        textFormat: Text.PlainText
        font: line.font
        color: line.color
        lineHeightMode: Text.FixedHeight
        lineHeight: line.lineHeight
        verticalAlignment: Text.AlignTop
        horizontalAlignment: line.horizontalAlignment
        wrapMode: line.wrap ? Text.Wrap : Text.NoWrap
    }
}
