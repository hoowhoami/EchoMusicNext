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
        height: rowData.heading === true ? navigation.collapsed ? 0 : 32 : navigation.collapsed ? 44 : 39
        Button {
            id: entry
            objectName: "sidebarMenu-" + row.rowData.key
            anchors.horizontalCenter: parent.horizontalCenter
            width: navigation.collapsed ? 38 : parent.width
            height: row.rowData.heading === true ? 32 : navigation.collapsed ? 38 : 37
            visible: row.height > 0
            readonly property bool heading: row.rowData.heading === true
            readonly property bool selected: !heading && navigation.router.current && navigation.router.current.name === row.rowData.route
            enabled: heading ? row.rowData.collapsible === true : row.rowData.plugin === true ? navigation.layout.entryEnabled(row.rowData.key) : navigation.router.available(row.rowData.route)
            leftPadding: navigation.collapsed ? 0 : 14
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
                spacing: 14
                opacity: entry.enabled || entry.heading ? 1 : 0.35
                glyph: entry.heading ? row.rowData.collapsed ? "chevron-right" : "chevron-down" : row.rowData.glyph
                text: navigation.collapsed ? "" : row.rowData.title
                glyphSize: entry.heading ? 12 : navigation.collapsed ? 19 : 18
                horizontalAlignment: navigation.collapsed ? Qt.AlignHCenter : Qt.AlignLeft
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
