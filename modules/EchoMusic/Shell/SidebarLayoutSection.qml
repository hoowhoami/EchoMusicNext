pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import EchoMusic.Controls
import EchoMusic.Theme
import EchoMusic.Shell

Rectangle {
    id: section
    required property var sectionData
    required property SidebarLayoutModel layout
    property bool resourceActive: true
    objectName: "sidebarEditorSection-" + sectionData.key
    height: 16 + header.height + (entries.count ? 6 + entries.implicitHeight : 0)
    radius: Theme.cardRadius
    color: Theme.withAlpha(Theme.main, sectionData.hidden ? 0.38 : 0.64)
    border.color: Theme.withAlpha(Theme.floatingText, sectionData.hidden ? 0.1 : 0.07)
    Rectangle {
        visible: section.sectionData.hidden
        x: 0
        y: section.radius
        width: 3
        height: parent.height - section.radius * 2
        color: Theme.withAlpha(Theme.floatingText, 0.2)
    }
    Item {
        id: header
        objectName: "sidebarEditorHeading-" + section.sectionData.key
        x: 8
        y: 8
        width: section.width - 16
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
                visible: !section.sectionData.cover
                name: section.sectionData.glyph
                color: Theme.iconMain
            }
            EchoCover {
                anchors.centerIn: parent
                width: 22
                height: 22
                visible: !!section.sectionData.cover
                source: section.sectionData.cover
                resourceActive: section.resourceActive
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
                text: section.sectionData.title
                font: Theme.font(13, Theme.strongWeight)
                color: Theme.floatingText
                lineHeightMode: Text.FixedHeight
                lineHeight: 19.5
                elide: Text.ElideRight
            }
            Text {
                width: parent.width
                text: section.sectionData.key === "playlists" ? qsTr("自建与收藏歌单") : section.sectionData.visibleCount + " / " + section.sectionData.itemCount + qsTr(" 项")
                font: Theme.font(11, Theme.regularWeight)
                color: Theme.floatingSecondaryText
                lineHeightMode: Text.FixedHeight
                lineHeight: 16.5
            }
        }
        EchoIconButton {
            objectName: "sidebarSectionVisibility-" + section.sectionData.key
            x: parent.width - width
            anchors.verticalCenter: parent.verticalCenter
            width: 28
            height: 28
            padding: 6.5
            glyph: section.sectionData.hidden ? "eye-off" : "eye"
            text: section.sectionData.key === "playlists" ? section.sectionData.hidden ? qsTr("显示整个歌单区域") : qsTr("隐藏整个歌单区域") : section.sectionData.hidden ? qsTr("显示分组") : qsTr("隐藏分组")
            colors: Theme.floating
            onClicked: section.layout.toggleGroup(section.sectionData.key)
        }
    }
    Item {
        id: items
        x: 8
        y: header.y + header.height + 6
        width: section.width - 16
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
            x: 33
            width: parent.width - 33
            maximumViewportHeight: 240
            clip: true
            reuseItems: true
            cacheBuffer: 32
            spacing: 3
            model: section.sectionData.items
            ScrollBar.vertical: EchoPageScrollBar {
                view: entries
                viewActive: section.resourceActive
            }
            delegate: Rectangle {
                id: entry
                required property var rowData
                readonly property var modelData: rowData
                objectName: "sidebarEditorItem-" + modelData.key
                width: entries.width
                height: 32
                radius: Theme.itemRadius
                color: modelData.hidden ? Theme.withAlpha(Theme.floatingText, 0.03) : hover.hovered ? Theme.withAlpha(Theme.floatingText, 0.05) : "transparent"
                HoverHandler {
                    id: hover
                }
                DropArea {
                    anchors.fill: parent
                    enabled: !entry.modelData.lockedOrder
                    keys: ["sidebar-menu-" + section.sectionData.key]
                    onDropped: event => {
                        const key = event.source.objectName;
                        const target = entry.modelData.key;
                        const after = event.y > height / 2;
                        const layout = section.layout;
                        Qt.callLater(() => layout.moveBefore(key, target, after));
                        event.acceptProposedAction();
                    }
                }
                Rectangle {
                    x: 3
                    anchors.verticalCenter: parent.verticalCenter
                    width: 26
                    height: 26
                    radius: Theme.itemRadius
                    color: Theme.withAlpha(Theme.floatingText, 0.05)
                    ShellActionDragHandle {
                        anchors.centerIn: parent
                        width: 16
                        height: 16
                        visible: !entry.modelData.cover
                        actionKey: entry.modelData.key
                        glyph: entry.modelData.glyph
                        dragGroup: "sidebar-menu-" + section.sectionData.key
                        resourceActive: section.resourceActive && !entry.modelData.lockedOrder
                        foreground: Theme.iconMain
                    }
                    EchoCover {
                        anchors.centerIn: parent
                        width: 22
                        height: 22
                        visible: !!entry.modelData.cover
                        source: entry.modelData.cover || ""
                        resourceActive: section.resourceActive
                    }
                }
                EchoTextTagRow {
                    x: 36
                    width: parent.width - x - 35
                    anchors.verticalCenter: parent.verticalCenter
                    text: entry.modelData.title
                    color: entry.modelData.hidden ? Theme.floatingSecondaryText : Theme.floatingText
                    font: Theme.font(12.5, Theme.regularWeight)
                    lineHeight: 18.75
                    fillTextWidth: true
                }
                EchoIconButton {
                    objectName: "sidebarVisibility-" + entry.modelData.key
                    x: parent.width - width
                    anchors.verticalCenter: parent.verticalCenter
                    width: 28
                    height: 28
                    padding: 7
                    glyph: entry.modelData.hidden ? "eye-off" : "eye"
                    text: entry.modelData.hidden ? qsTr("显示入口") : qsTr("隐藏入口")
                    colors: Theme.floating
                    onClicked: section.layout.toggleHidden(entry.modelData.key)
                }
            }
        }
    }
}
