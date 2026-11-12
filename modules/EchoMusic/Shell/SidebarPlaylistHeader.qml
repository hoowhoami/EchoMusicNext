pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Controls
import EchoMusic.Theme
import EchoMusic.Library

Item {
    id: header
    required property LibraryController service
    required property bool collapsed
    property real collapseProgress: collapsed ? 1 : 0
    property real uiScale: 1
    signal orderRequested
    signal createRequested
    signal importRequested
    objectName: "sidebarPlaylistHeader"
    implicitHeight: 24 + 6 * collapseProgress
    // Keep the three toolbar glyphs on the same 12px source grid. A 13px
    // refresh box requires fractional resampling at common display scales.
    component PlaylistAction: EchoIconButton {
        implicitWidth: 24
        implicitHeight: 24
        padding: 6
    }
    RowLayout {
        width: 188
        height: 24
        anchors.verticalCenter: parent.verticalCenter
        visible: header.collapseProgress < 1
        enabled: !header.collapsed
        opacity: Math.max(0, 1 - 2 * header.collapseProgress)
        spacing: 6
        RowLayout {
            Layout.fillHeight: false
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            spacing: 4
            EchoActionButton {
                id: created
                objectName: "sidebarCreatedTab"
                padding: 0
                text: qsTr("自建歌单")
                onClicked: header.service.tab = 0
                background: null
                contentItem: Text {
                    text: created.text
                    color: header.service.tab === 0 ? Theme.accentText : created.hovered ? Theme.text : Theme.secondaryText
                    font: Theme.spacedFont(11, Theme.semiboldWeight, 0.1)
                    verticalAlignment: Text.AlignVCenter
                }
            }
            Rectangle {
                Layout.preferredWidth: 1
                Layout.preferredHeight: 12
                color: Theme.withAlpha(Theme.text, Theme.dark ? 0.36 : 0.22)
            }
            EchoActionButton {
                id: collected
                objectName: "sidebarCollectedTab"
                padding: 0
                text: qsTr("收藏歌单")
                onClicked: header.service.tab = 1
                background: null
                contentItem: Text {
                    text: collected.text
                    color: header.service.tab === 1 ? Theme.accentText : collected.hovered ? Theme.text : Theme.secondaryText
                    font: Theme.spacedFont(11, Theme.semiboldWeight, 0.1)
                    verticalAlignment: Text.AlignVCenter
                }
            }
            Item {
                Layout.fillWidth: true
            }
        }
        RowLayout {
            Layout.fillWidth: false
            Layout.fillHeight: false
            spacing: 2
            Layout.leftMargin: 2
            PlaylistAction {
                glyph: "arrows-sort"
                text: qsTr("调整歌单顺序")
                tooltipText: text
                objectName: "sidebarPlaylistOrder"
                enabled: header.service.signedIn && !header.service.busy
                onClicked: header.orderRequested()
            }
            PlaylistAction {
                objectName: "sidebarPlaylistRefresh"
                glyph: "refresh"
                text: qsTr("刷新歌单")
                enabled: header.service.signedIn && !header.service.busy
                onClicked: header.service.reload()
            }
            Item {
                Layout.preferredWidth: 24
                Layout.preferredHeight: 24
                PlaylistAction {
                    anchors.fill: parent
                    visible: header.service.tab === 0
                    objectName: "sidebarPlaylistAdd"
                    glyph: "plus"
                    text: qsTr("添加歌单")
                    enabled: header.service.signedIn && !header.service.busy
                    onClicked: addMenuOwner.open()
                }
            }
        }
    }
    EchoPopupLoader {
        id: addMenuOwner
        objectName: "sidebarPlaylistAddMenuOwner"
        hostItem: header
        sourceComponent: Component {
            EchoAnchoredPopup {
                id: addMenu
                objectName: "sidebarPlaylistAddMenu"
                anchorItem: header
                uiScale: header.uiScale
                preferredWidth: 248
                sideOffset: 6
                padding: 8
                contentItem: ColumnLayout {
                    spacing: 2
                    Label {
                        Layout.fillWidth: true
                        Layout.leftMargin: 10
                        Layout.topMargin: 4
                        Layout.bottomMargin: 6
                        text: qsTr("添加歌单")
                        font: Theme.spacedFont(11, Theme.semiboldWeight, 0.55)
                        color: Theme.floatingSecondaryText
                    }
                    Repeater {
                        model: [qsTr("新建空歌单"), qsTr("导入外部歌单")]
                        delegate: ItemDelegate {
                            id: action
                            EchoActionCursor {}
                            required property int index
                            required property string modelData
                            objectName: index === 0 ? "sidebarPlaylistNew" : "sidebarPlaylistImport"
                            Layout.fillWidth: true
                            implicitHeight: 48
                            padding: 8
                            Accessible.name: modelData
                            background: Rectangle {
                                radius: Theme.cardRadius
                                color: action.hovered ? Theme.withAlpha(Theme.floating.accent, 0.1) : "transparent"
                            }
                            contentItem: RowLayout {
                                spacing: 12
                                Rectangle {
                                    objectName: "sidebarPlaylistActionIcon"
                                    Layout.minimumWidth: 32
                                    Layout.preferredWidth: 32
                                    Layout.maximumWidth: 32
                                    Layout.preferredHeight: 32
                                    radius: Theme.cardRadius
                                    color: Theme.withAlpha(Theme.floating.accent, action.hovered ? 0.2 : 0.12)
                                    EchoIcon {
                                        anchors.centerIn: parent
                                        name: action.index === 0 ? "playlist-add" : "external-link"
                                        width: 16
                                        height: 16
                                        color: Theme.floating.accentText
                                    }
                                }
                                // Preserve fractional leading-tight line boxes without
                                // rounding both lines separately in a nested layout.
                                Column {
                                    id: textColumn
                                    Layout.fillWidth: true
                                    Layout.preferredWidth: 0
                                    Layout.minimumWidth: 0
                                    spacing: 2
                                    EchoTextTagRow {
                                        objectName: "sidebarPlaylistActionTitle"
                                        width: textColumn.width
                                        fillTextWidth: true
                                        text: action.modelData
                                        font: Theme.font(13, Theme.mediumWeight)
                                        lineHeight: 16.25
                                        color: Theme.floatingText
                                    }
                                    EchoTextTagRow {
                                        objectName: "sidebarPlaylistActionDescription"
                                        width: textColumn.width
                                        fillTextWidth: true
                                        text: action.index === 0 ? qsTr("自定义名称从零开始") : qsTr("链接 / 截图")
                                        font: Theme.font(11, Theme.regularWeight)
                                        lineHeight: 13.75
                                        color: Theme.floatingSecondaryText
                                    }
                                }
                            }
                            onClicked: {
                                addMenu.close();
                                if (index === 0)
                                    header.createRequested();
                                else
                                    header.importRequested();
                            }
                        }
                    }
                }
            }
        }
    }
    Rectangle {
        anchors.fill: parent
        visible: header.collapseProgress > 0
        enabled: header.collapsed
        opacity: Math.max(0, 2 * header.collapseProgress - 1)
        radius: Theme.itemRadius
        color: Theme.withAlpha(Theme.text, 0.06)
        border.color: Theme.withAlpha(Theme.text, 0.04)
        Rectangle {
            x: 3 + header.service.tab * (width + 2)
            y: 3
            width: (parent.width - 8) / 2
            height: parent.height - 6
            radius: Theme.itemRadius
            color: Theme.sidebar
            border.color: Theme.withAlpha(Theme.accent, 0.1)
            Behavior on x {
                NumberAnimation {
                    duration: 220
                    easing.type: Easing.OutCubic
                }
            }
        }
        RowLayout {
            anchors.fill: parent
            anchors.margins: 3
            spacing: 2
            Repeater {
                model: [qsTr("自建歌单"), qsTr("收藏歌单")]
                delegate: EchoIconButton {
                    id: railTab
                    required property int index
                    required property string modelData
                    objectName: "sidebarRailPlaylistTab-" + index
                    Layout.fillWidth: true
                    Layout.preferredHeight: 24
                    glyph: index === 0 ? "playlist-add" : "heart"
                    text: modelData
                    tooltipText: text
                    padding: 4.5
                    contentItem: Item {
                        EchoIcon {
                            anchors.centerIn: parent
                            width: 15
                            height: 15
                            name: railTab.glyph
                            color: railTab.foreground
                            retainWhileLoading: true
                        }
                    }
                    foreground: header.service.tab === index ? Theme.accentText : hovered ? Theme.text : Theme.iconMain
                    background: null
                    onClicked: header.service.tab = index
                }
            }
        }
    }
}
