pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import EchoMusic.Theme

// A 1px subtle divider with 4px vertical and 6px horizontal margins. Paddings
// round to whole DIPs (bind uiScale from the owning menu) and the line renders
// exactly one device pixel, so it never straddles a pixel boundary.
MenuSeparator {
    property real uiScale: 1
    // Invisible separators remain in Qt's contentModel. Collapse their geometry
    // as well, so the measured menu and the ListView agree on the scroll range.
    implicitHeight: visible ? topPadding + contentItem.implicitHeight + bottomPadding : 0
    horizontalPadding: Math.round(6 * uiScale)
    verticalPadding: Math.round(4 * uiScale)
    contentItem: Item {
        id: lineHost
        implicitHeight: 1
        EchoDivider {
            anchors.fill: parent
            lineColor: Theme.floating.subtleBorder
        }
    }
}
