pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import EchoMusic.Theme

// Preserve line boxes and gaps while centering rendered glyphs.
// Font ascent boxes differ across fallback fonts; avoid page-specific y offsets.
Item {
    id: row
    default property alias tags: tagRow.data
    property string text: ""
    property font font: Theme.font(14, Theme.regularWeight)
    property color color: Theme.text
    property real spacing: 8
    property real textTagSpacing: spacing
    property real lineHeight: font.pixelSize * 1.5
    property bool wrap: false
    // Keep tab label and badge on one line even when layout rounding
    // reduces preferred width during asynchronous count changes.
    property bool wrapTags: true
    // Flex labels can reserve the remaining line width and pin tags to its end.
    property bool fillTextWidth: false
    readonly property bool truncated: !wrap && ink.displayText !== text
    property int horizontalAlignment: Text.AlignLeft
    property alias textItem: label
    readonly property alias textBox: textSlot
    readonly property real firstLineInkCenter: flow.y + textSlot.y + label.y + label.baselineOffset + ink.firstLineInkCenterFromBaseline
    implicitWidth: ink.naturalWidth + (tagRow.implicitWidth > 0 ? textTagSpacing + tagRow.implicitWidth : 0)
    implicitHeight: flow.implicitHeight
    data: [
        EchoTextInkMetrics {
            id: ink
            font: row.font
            text: row.text
            layoutWidth: textSlot.width
            lineHeight: row.lineHeight
            wrap: row.wrap
            elide: !row.wrap
        },
        Flow {
            id: flow
            width: row.wrapTags ? row.width : Math.max(row.width, Math.ceil(textSlot.width + (tagRow.implicitWidth > 0 ? row.textTagSpacing + tagRow.implicitWidth : 0)))
            // Controls may stretch contentItem beyond its natural line height.
            // Center shaped glyphs within the available extent, retaining fractional
            // child heights even when Flow rounds its implicitHeight.
            y: (row.height - childrenRect.height) / 2 - childrenRect.y
            spacing: row.textTagSpacing

            Item {
                id: textSlot

                // Use the available width for a tag-free wrapped label too;
                // metric natural width can be narrower than rendered glyphs.
                // Tagged flows keep their intrinsic text/tag wrapping behavior.
                width: Math.max(0, Math.min(row.fillTextWidth && (!row.wrap || tagRow.implicitWidth === 0) ? row.width : ink.naturalWidth, row.wrap ? row.width : row.width - (tagRow.implicitWidth > 0 ? row.textTagSpacing + tagRow.implicitWidth : 0)))
                height: Math.max(row.lineHeight * Math.max(1, label.lineCount), tagRow.implicitHeight)
                Text {
                    id: label
                    width: textSlot.width
                    height: implicitHeight
                    // baselineOffset is local to this Text. Center the shaped
                    // multiline ink without changing inter-line spacing.
                    y: ink.hasInk ? textSlot.height / 2 - baselineOffset - ink.inkCenterFromBaseline : (textSlot.height - height) / 2
                    text: ink.displayText
                    Accessible.role: Accessible.StaticText
                    Accessible.name: row.text
                    textFormat: Text.PlainText
                    font: row.font
                    color: row.color
                    lineHeightMode: Text.FixedHeight
                    lineHeight: row.lineHeight
                    verticalAlignment: Text.AlignTop
                    horizontalAlignment: row.horizontalAlignment
                    wrapMode: row.wrap ? Text.Wrap : Text.NoWrap
                }
            }
            Item {
                id: tagSlot
                // Empty layouts naturally have zero width. Hiding the layout
                // by its own implicitWidth creates a visibility/size cycle when
                // an async badge changes from hidden to visible.
                readonly property bool inline: !row.wrapTags || textSlot.width + row.textTagSpacing + tagRow.implicitWidth <= row.width
                width: tagRow.implicitWidth
                height: inline ? textSlot.height : Math.max(row.lineHeight, tagRow.implicitHeight)
                // Keep the native layout at its natural height, then center the
                // whole group. Stretching it to a 21px text line rounds an 18px
                // badge's 1.5px offset to 2px and shifts it off the ink center.
                RowLayout {
                    id: tagRow
                    // The non-layout parent positions the whole group, outside
                    // Flow's direct children and rounded layout child geometry.
                    y: (tagSlot.height - height) / 2
                    width: implicitWidth
                    height: implicitHeight
                    spacing: row.spacing
                }
            }
        }
    ]
}
