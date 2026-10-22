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
    property int horizontalAlignment: Text.AlignLeft
    property alias textItem: label
    readonly property alias textBox: line
    // These projections are needed by only some consumers. Evaluate inside
    // their binding instead of installing two more bindings on every label.
    function isTruncated(): bool {
        return !wrap && ink.displayText !== text;
    }
    function firstLineInkCenter(): real {
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
        elide: !line.wrap
    }
    Text {
        id: label
        width: Math.max(0, line.fillTextWidth ? line.width : Math.min(line.width, ink.naturalWidth))
        height: implicitHeight
        y: ink.hasInk ? line.height / 2 - baselineOffset - ink.inkCenterFromBaseline : (line.height - height) / 2
        text: ink.displayText
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
