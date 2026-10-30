pragma ComponentBehavior: Bound
import QtQuick
import EchoMusic.Controls
import EchoMusic.Theme

Rectangle {
    id: entry
    required property var entryData
    property bool resourceActive: true
    property bool interactive: true
    property bool lifted: false
    signal visibilityRequested
    height: 32
    radius: Theme.itemRadius
    color: lifted ? "transparent" : entryData.hidden ? Theme.withAlpha(Theme.floatingText, 0.03) : hover.hovered ? Theme.withAlpha(Theme.floatingText, 0.05) : "transparent"
    HoverHandler {
        id: hover
        enabled: entry.interactive
    }
    Rectangle {
        x: 3
        anchors.verticalCenter: parent.verticalCenter
        width: 26
        height: 26
        radius: Theme.itemRadius
        color: Theme.withAlpha(Theme.floatingText, 0.05)
        EchoIcon {
            anchors.centerIn: parent
            width: 16
            height: 16
            visible: !entry.entryData.cover
            name: entry.entryData.glyph || ""
            color: Theme.iconMain
        }
        EchoCover {
            anchors.centerIn: parent
            width: 22
            height: 22
            visible: !!entry.entryData.cover
            source: entry.entryData.cover || ""
            resourceActive: entry.resourceActive
        }
    }
    EchoTextTagRow {
        x: 36
        width: parent.width - x - 35
        anchors.verticalCenter: parent.verticalCenter
        text: entry.entryData.title || ""
        color: entry.entryData.hidden ? Theme.floatingSecondaryText : Theme.floatingText
        font: Theme.font(12.5, Theme.regularWeight)
        lineHeight: 18.75
        fillTextWidth: true
    }
    EchoIconButton {
        objectName: "sidebarVisibility-" + entry.entryData.key
        x: parent.width - width
        anchors.verticalCenter: parent.verticalCenter
        width: 28
        height: 28
        padding: 7
        glyph: entry.entryData.hidden ? "eye-off" : "eye"
        text: entry.entryData.hidden ? qsTranslate("SidebarLayoutSection", "显示入口") : qsTranslate("SidebarLayoutSection", "隐藏入口")
        colors: Theme.floating
        enabled: entry.interactive
        onClicked: entry.visibilityRequested()
    }
}
