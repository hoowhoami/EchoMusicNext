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
        Rectangle {
            id: line
            anchors.left: parent.left
            anchors.right: parent.right
            // Fractional DPRs (Windows 125%) turn a 1-DIP line into 1.25 device
            // pixels whose phase differs per separator. Render exactly one
            // device pixel on the window's grid instead: height 1/dpr, shifted
            // so the top edge lands on a whole device row. The ancestor walk
            // reads each y, keeping the snap reactive to layout and scrolling;
            // offsets above the list are common to all lines, so they stay
            // mutually consistent.
            readonly property real dpr: Screen.devicePixelRatio > 0 ? Screen.devicePixelRatio : 1
            height: 1 / dpr
            y: {
                let absolute = 0;
                for (let node = lineHost, hop = 0; node && hop < 16; ++hop) {
                    absolute += node.y;
                    node = node.parent;
                }
                const device = absolute * dpr;
                return (Math.round(device) - device) / dpr;
            }
            color: Theme.floating.subtleBorder
        }
    }
}
