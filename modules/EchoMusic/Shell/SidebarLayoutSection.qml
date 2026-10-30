pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import EchoMusic.Controls
import EchoMusic.Theme
import EchoMusic.Shell

Rectangle {
    id: pane
    required property var sectionData
    required property SidebarLayoutModel layout
    property bool resourceActive: true
    objectName: "sidebarEditorSection-" + sectionData.key
    height: 16 + header.height + (entries.count ? 6 + entries.implicitHeight : 0)
    radius: Theme.cardRadius
    color: Theme.withAlpha(Theme.main, sectionData.hidden ? 0.38 : 0.64)
    border.color: Theme.withAlpha(Theme.floatingText, sectionData.hidden ? 0.1 : 0.07)
    Rectangle {
        visible: pane.sectionData.hidden
        x: 0
        y: pane.radius
        width: 3
        height: parent.height - pane.radius * 2
        color: Theme.withAlpha(Theme.floatingText, 0.2)
    }
    Item {
        id: header
        objectName: "sidebarEditorHeading-" + pane.sectionData.key
        x: 8
        y: 8
        width: pane.width - 16
        height: Math.max(34, copy.height)
        Rectangle {
            width: 26
            height: 26
            anchors.verticalCenter: parent.verticalCenter
            radius: Theme.itemRadius
            color: Theme.withAlpha(Theme.floatingText, 0.05)
            EchoIcon {
                anchors.centerIn: parent
                width: 16
                height: 16
                visible: !pane.sectionData.cover
                name: pane.sectionData.glyph
                color: Theme.iconMain
            }
            EchoCover {
                anchors.centerIn: parent
                width: 22
                height: 22
                visible: !!pane.sectionData.cover
                source: pane.sectionData.cover
                resourceActive: pane.resourceActive
            }
        }
        Column {
            id: copy
            x: 33
            width: header.width - x - 35
            anchors.verticalCenter: parent.verticalCenter
            spacing: 1
            Text {
                width: parent.width
                text: pane.sectionData.title
                font: Theme.font(13, Theme.strongWeight)
                color: Theme.floatingText
                lineHeightMode: Text.FixedHeight
                lineHeight: 19.5
                elide: Text.ElideRight
            }
            Text {
                width: parent.width
                text: pane.sectionData.key === "playlists" ? qsTr("自建与收藏歌单") : pane.sectionData.visibleCount + " / " + pane.sectionData.itemCount + qsTr(" 项")
                font: Theme.font(11, Theme.regularWeight)
                color: Theme.floatingSecondaryText
                lineHeightMode: Text.FixedHeight
                lineHeight: 16.5
            }
        }
        EchoIconButton {
            objectName: "sidebarSectionVisibility-" + pane.sectionData.key
            x: parent.width - width
            anchors.verticalCenter: parent.verticalCenter
            width: 28
            height: 28
            padding: 6.5
            glyph: pane.sectionData.hidden ? "eye-off" : "eye"
            text: pane.sectionData.key === "playlists" ? pane.sectionData.hidden ? qsTr("显示整个歌单区域") : qsTr("隐藏整个歌单区域") : pane.sectionData.hidden ? qsTr("显示分组") : qsTr("隐藏分组")
            colors: Theme.floating
            onClicked: pane.layout.toggleGroup(pane.sectionData.key)
        }
    }
    Item {
        id: items
        x: 8
        y: header.y + header.height + 6
        width: pane.width - 16
        height: entries.implicitHeight
        visible: entries.count > 0
        Rectangle {
            x: 13
            y: 4
            width: 1
            height: Math.max(0, parent.height - 8)
            radius: Theme.microRadius
            color: Theme.withAlpha(Theme.floatingText, 0.08)
        }
        EchoBoundedListView {
            id: entries
            objectName: "sidebarEditorItems-" + pane.sectionData.key
            x: 33
            width: parent.width - 33
            maximumViewportHeight: 240
            clip: true
            reuseItems: true
            cacheBuffer: 32
            spacing: 3
            model: pane.sectionData.items
            ScrollBar.vertical: EchoPageScrollBar {
                view: entries
                viewActive: pane.resourceActive
            }
            interactive: !menuReorder.busy
            property EchoListReorder reorderController: EchoListReorder {
                id: menuReorder
                objectName: "sidebarMenuReorder-" + pane.sectionData.key
                parent: entries
                view: entries
                sourceModel: pane.sectionData.items
                rowHeight: 32
                handleRight: 28
                blocked: pane.sectionData.lockedOrder === true
                resourceActive: pane.resourceActive
                snapshotForRow: row => row.modelData
                onMoveRequested: (from, to) => {
                    const target = entries.itemAtIndex(to);
                    if (target)
                        pane.layout.moveBefore(snapshot.key, snapshotForRow(target).key, from < to);
                }
                dragContent: Component {
                    SidebarLayoutEntry {
                        objectName: "sidebarMenuDragCard"
                        width: parent.width
                        entryData: menuReorder.snapshot || ({})
                        interactive: false
                        lifted: true
                        resourceActive: pane.resourceActive
                    }
                }
            }
            delegate: EchoReorderRow {
                id: entry
                required property var rowData
                readonly property var modelData: rowData
                objectName: "sidebarEditorItem-" + modelData.key
                reorder: menuReorder
                SidebarLayoutEntry {
                    width: entry.width
                    entryData: entry.modelData
                    resourceActive: pane.resourceActive && entry.contentActive
                    interactive: !menuReorder.busy
                    onVisibilityRequested: pane.layout.toggleHidden(entry.modelData.key)
                }
            }
        }
    }
}
