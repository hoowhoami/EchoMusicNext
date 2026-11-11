pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import EchoMusic.Controls
import EchoMusic.Theme
import EchoMusic.Shell

EchoBoundedListView {
    id: navigation
    required property SidebarLayoutModel layout
    required property RouteController router
    required property bool collapsed
    property real collapseProgress: collapsed ? 1 : 0
    maximumViewportHeight: 240
    implicitWidth: 194
    model: layout.navigationModel
    clip: true
    reuseItems: true
    cacheBuffer: 80
    boundsBehavior: Flickable.StopAtBounds
    ScrollBar.vertical: EchoPageScrollBar {
        view: navigation
        viewActive: navigation.visible
    }
    delegate: Item {
        id: row
        required property var rowData
        width: navigation.width
        height: rowData.heading === true ? 32 * (1 - navigation.collapseProgress) : 39 + 5 * navigation.collapseProgress
        EchoActionButton {
            id: entry
            objectName: "sidebarMenu-" + row.rowData.key
            anchors.horizontalCenter: parent.horizontalCenter
            width: parent.width
            height: row.rowData.heading === true ? 32 : 37 + navigation.collapseProgress
            visible: row.height > 0
            opacity: heading ? Math.max(0, 1 - 2 * navigation.collapseProgress) : 1
            readonly property bool heading: row.rowData.heading === true
            readonly property bool selected: !heading && navigation.router.current && navigation.router.current.name === row.rowData.route
            enabled: heading ? row.rowData.collapsible === true : row.rowData.plugin === true ? navigation.layout.entryEnabled(row.rowData.key) : navigation.router.available(row.rowData.route)
            leftPadding: 14 - 4.5 * navigation.collapseProgress
            rightPadding: leftPadding
            hoverEnabled: true
            Accessible.name: row.rowData.title
            onClicked: {
                if (heading)
                    navigation.layout.toggleCollapsed(row.rowData.section);
                else if (!navigation.layout.activate(row.rowData.key))
                    navigation.router.navigate(row.rowData.route);
            }
            EchoToolTip {
                parent: entry
                text: row.rowData.title
                requestedVisible: entry.enabled && entry.hovered && navigation.collapsed
            }
            contentItem: EchoButtonContent {
                spacing: 14 * (1 - navigation.collapseProgress)
                textOpacity: Math.max(0, 1 - 2 * navigation.collapseProgress)
                textExtent: Math.max(0, 1 - navigation.collapseProgress)
                retainGlyphWhileLoading: true
                opacity: entry.enabled || entry.heading ? 1 : 0.35
                glyph: entry.heading ? row.rowData.collapsed ? "chevron-right" : "chevron-down" : row.rowData.glyph
                text: row.rowData.title
                glyphSize: entry.heading ? 12 : 18 + navigation.collapseProgress
                horizontalAlignment: Qt.AlignLeft
                color: entry.selected ? Theme.accentText : entry.heading ? Theme.secondaryText : Theme.text
                iconColor: entry.selected ? Theme.accentText : Theme.iconMain
                font: Theme.font(entry.heading ? 12 : 14, entry.selected ? Theme.semiboldWeight : Theme.regularWeight)
            }
            background: Rectangle {
                radius: Theme.itemRadius
                color: entry.selected ? Theme.selected : entry.hovered && entry.enabled ? Theme.rowHover : "transparent"
            }
        }
    }
}
