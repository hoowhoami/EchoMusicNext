pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Controls
import EchoMusic.Theme
import EchoMusic.Shell

EchoDialog {
    id: popup
    component ShortcutOption: Item {
        id: option
        required property var modelData
        property bool artist: false
        readonly property bool available: modelData.plugin ? popup.layout.entryEnabled(modelData.key) : popup.router.available(modelData.route)
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        implicitHeight: artist ? 142 : modelData.resourceId ? Math.max(36, label.implicitHeight) + 20 : Math.max(23, label.implicitHeight) + 30
        Button {
            id: activate
            objectName: "shortcutCandidate-" + option.modelData.key
            anchors.fill: parent
            enabled: option.available
            hoverEnabled: true
            Accessible.name: option.modelData.title
            onClicked: if (popup.layout.openShortcut(popup.router, option.modelData.key))
                popup.close()
            background: Rectangle {
                radius: Theme.controlRadius
                color: activate.hovered && activate.enabled ? Theme.floating.hover : Theme.floating.mutedSurface
                border.width: activate.visualFocus ? 2 : 0
                border.color: Theme.floating.accent
            }
            contentItem: Item {}
        }
        RowLayout {
            anchors.fill: parent
            anchors.margins: option.modelData.resourceId ? 10 : 15
            spacing: 12
            visible: !option.artist
            Loader {
                Layout.preferredWidth: option.modelData.resourceId ? 36 : 23
                Layout.preferredHeight: width
                sourceComponent: option.modelData.resourceId ? artwork : icon
                Component {
                    id: icon
                    EchoIcon {
                        name: option.modelData.glyph
                        color: option.modelData.pinned || !option.available ? Theme.floatingSecondaryText : Theme.floatingText
                    }
                }
                Component {
                    id: artwork
                    EchoCover {
                        source: option.modelData.cover || ""
                        coverAlt: option.modelData.title
                        cornerRadius: 5
                        resourceActive: popup.visible
                        colors: Theme.floating
                    }
                }
            }
            EchoTextTagRow {
                id: label
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                text: option.modelData.title
                wrap: true
                fillTextWidth: true
                color: option.modelData.pinned || !option.available ? Theme.floatingSecondaryText : Theme.floatingText
                font: Theme.font(14, Theme.regularWeight)
            }
            Item {
                Layout.preferredWidth: 24
                Layout.preferredHeight: 24
            }
        }
        Loader {
            active: option.artist
            anchors.fill: parent
            sourceComponent: ColumnLayout {
                spacing: 10
                Item {
                    Layout.preferredHeight: 20
                }
                EchoCover {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredWidth: 64
                    Layout.preferredHeight: 64
                    source: option.modelData.cover || ""
                    coverAlt: option.modelData.title
                    circular: true
                    placeholderGlyph: "user"
                    resourceActive: popup.visible
                    colors: Theme.floating
                }
                Label {
                    Layout.fillWidth: true
                    Layout.leftMargin: 8
                    Layout.rightMargin: 8
                    text: option.modelData.title
                    elide: Text.ElideRight
                    horizontalAlignment: Text.AlignHCenter
                    color: Theme.floatingText
                    font: Theme.font(14, Theme.regularWeight)
                }
                Item {
                    Layout.fillHeight: true
                }
            }
        }
        EchoIconButton {
            objectName: "shortcutCandidateToggle-" + option.modelData.key
            anchors.right: parent.right
            anchors.rightMargin: option.artist ? 6 : 12
            anchors.top: option.artist ? parent.top : undefined
            anchors.topMargin: 6
            anchors.verticalCenter: option.artist ? undefined : parent.verticalCenter
            width: 30
            height: 30
            padding: 6
            enabled: option.modelData.pinned ? popup.layout.shortcuts.length > 1 : option.available
            glyph: option.modelData.pinned ? "minus" : "plus"
            text: (option.modelData.pinned ? qsTr("移除") : qsTr("添加")) + option.modelData.title + qsTr("卡片")
            colors: Theme.floating
            onClicked: popup.layout.toggleShortcut(option.modelData.key)
        }
    }
    objectName: "sidebarShortcutPopup"
    required property SidebarLayoutModel layout
    required property RouteController router
    required property Item anchorItem
    readonly property rect anchorRect: geometry.rect
    readonly property real sidebarEdge: anchorRect.x + anchorRect.width
    readonly property real edge: 12 * uiScale
    heading: qsTr("添加卡片")
    preferredWidth: 520
    maximumHeight: parent ? parent.height / uiScale - 24 : 620
    windowHeightMargin: 24
    modal: false
    // Anchor against the whole sidebar rather than the add button.
    x: parent && parent.width - sidebarEdge - 20 * uiScale >= 340 * uiScale ? sidebarEdge + 8 * uiScale : edge
    width: parent ? Math.max(0, Math.min(520, (parent.width - x) / uiScale - 12)) : 520
    height: maximumHeight
    y: edge
    EchoPopupAnchor {
        id: geometry
        item: popup.anchorItem
        coordinateItem: popup.parent
    }
    onOpened: layout.browseResources(true)
    onClosed: layout.browseResources(false)
    Component.onDestruction: layout.browseResources(false)
    flushBody: true
    body: Component {
        Flickable {
            id: shortcutView
            implicitHeight: popup.bodyHeightLimit
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            contentWidth: width
            contentHeight: body.implicitHeight
            ScrollBar.vertical: EchoPopupScrollBar {
                surface: popup
                view: shortcutView
                resourceActive: popup.visible
            }
            ColumnLayout {
                id: body
                width: shortcutView.width - 24
                spacing: 0
                Label {
                    Layout.fillWidth: true
                    Layout.bottomMargin: 16
                    text: qsTr("拖动卡片调整顺序，第一张就是首页；至少保留一张卡片。")
                    color: Theme.floatingSecondaryText
                    font: Theme.font(12, Theme.regularWeight)
                    lineHeightMode: Text.FixedHeight
                    lineHeight: 19.2
                    wrapMode: Text.Wrap
                }
                RowLayout {
                    Layout.fillHeight: false
                    spacing: 5
                    Layout.fillWidth: true
                    Layout.bottomMargin: 16
                    Label {
                        Layout.fillWidth: true
                        text: qsTr("常用功能")
                        color: Theme.floatingText
                        font: Theme.font(17, Theme.semiboldWeight)
                    }
                    EchoSoftButton {
                        text: qsTr("恢复默认")
                        colors: Theme.floating
                        implicitHeight: 28
                        font: Theme.font(12, Theme.semiboldWeight)
                        onClicked: popup.layout.restoreShortcuts()
                    }
                }
                EchoBoundedListView {
                    id: options
                    Layout.fillWidth: true
                    maximumViewportHeight: 320
                    implicitWidth: 420
                    clip: true
                    cacheBuffer: 60
                    reuseItems: true
                    property var catalog: popup.layout.shortcutCatalog
                    model: Math.ceil(catalog.length / 2)
                    spacing: 10
                    ScrollBar.vertical: EchoPageScrollBar {
                        view: options
                        viewActive: popup.visible
                    }
                    delegate: RowLayout {
                        id: optionRow
                        required property int index
                        width: options.width
                        spacing: 10
                        uniformCellSizes: true
                        ShortcutOption {
                            modelData: options.catalog[optionRow.index * 2]
                        }
                        Loader {
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            active: optionRow.index * 2 + 1 < options.catalog.length
                            sourceComponent: ShortcutOption {
                                modelData: options.catalog[optionRow.index * 2 + 1]
                                width: parent.width
                            }
                        }
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: 30
                    spacing: 16
                    Label {
                        objectName: "shortcutArtistsHeading"
                        text: qsTr("常听艺人")
                        color: Theme.floatingText
                        font: Theme.font(17, Theme.semiboldWeight)
                    }
                    GridLayout {
                        Layout.fillWidth: true
                        visible: popup.layout.frequentArtists.length > 0
                        columns: 3
                        columnSpacing: 12
                        rowSpacing: 12
                        Repeater {
                            model: popup.layout.frequentArtists
                            delegate: ShortcutOption {
                                artist: true
                            }
                        }
                    }
                    Label {
                        objectName: "shortcutArtistsEmpty"
                        visible: popup.layout.frequentArtists.length === 0
                        Layout.fillWidth: true
                        Layout.topMargin: 18
                        Layout.bottomMargin: 18
                        text: popup.layout.historyLoading ? qsTr("正在加载播放记录…") : popup.layout.historyError.length ? qsTr("暂时无法加载播放记录，请稍后重试") : qsTr("播放歌曲后，常听艺人会出现在这里")
                        wrapMode: Text.Wrap
                        color: Theme.floatingSecondaryText
                        font: Theme.font(13, Theme.regularWeight)
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: 30
                    spacing: 16
                    Label {
                        objectName: "shortcutPlaylistsHeading"
                        text: qsTr("最近常听")
                        color: Theme.floatingText
                        font: Theme.font(17, Theme.semiboldWeight)
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        visible: popup.layout.recentPlaylists.length > 0
                        spacing: 4
                        Repeater {
                            model: popup.layout.recentPlaylists
                            delegate: ShortcutOption {}
                        }
                    }
                    Label {
                        objectName: "shortcutPlaylistsEmpty"
                        visible: popup.layout.recentPlaylists.length === 0
                        Layout.fillWidth: true
                        Layout.topMargin: 18
                        Layout.bottomMargin: 18
                        text: qsTr("播放过的歌单会出现在这里")
                        wrapMode: Text.Wrap
                        color: Theme.floatingSecondaryText
                        font: Theme.font(13, Theme.regularWeight)
                    }
                }
            }
        }
    }
}
