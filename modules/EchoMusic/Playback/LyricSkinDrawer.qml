pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Theme
import EchoMusic.Controls
import EchoMusic.Playback

EchoDrawer {
    id: drawer
    objectName: "lyricSkinDrawer"
    required property PlaybackService playback
    readonly property bool portraitSelected: Theme.store.extensions.selectedSkin === "portrait"
    property bool showSettings: false
    signal globalSettingsRequested
    preferredWidth: 360
    playerBottomInset: 88
    onAboutToHide: {
        if (builtinSettings.item) {
            if (portraitSelected)
                (builtinSettings.item as PortraitSkinSettings).closePopups();
            else
                (builtinSettings.item as LyricSkinSettings).closePopups();
        }
    }
    function openSkins(): void {
        showSettings = false;
        open();
    }
    contentItem: ColumnLayout {
        spacing: 0
        EchoDrawerHeader {
            Layout.fillWidth: true
            heading: drawer.showSettings ? qsTr("皮肤设置") : qsTr("换肤")
            description: drawer.showSettings ? (Theme.store.extensions.skin ? Theme.store.extensions.skin.title : drawer.portraitSelected ? qsTr("写真") : qsTr("封面歌词")) : ""
            headingFont: Theme.font(16, 800)
            closeLabel: qsTr("关闭皮肤设置")
            onCloseRequested: drawer.close()
            actions: Component {
                RowLayout {
                    spacing: 8
                    EchoIconButton {
                        visible: drawer.showSettings
                        Layout.fillHeight: false
                        implicitWidth: 32
                        implicitHeight: 32
                        colors: Theme.floating
                        glyph: "chevron-left"
                        text: qsTr("返回换肤")
                        onClicked: drawer.showSettings = false
                    }
                    EchoIconButton {
                        Layout.fillHeight: false
                        implicitWidth: 32
                        implicitHeight: 32
                        colors: Theme.floating
                        glyph: "settings"
                        text: qsTr("全局设置")
                        onClicked: {
                            drawer.close();
                            drawer.globalSettingsRequested();
                        }
                    }
                }
            }
        }
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 1
            color: Theme.floatingBorder
        }
        GridView {
            id: catalog
            objectName: "lyricSkinCatalog"
            visible: !drawer.showSettings
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 16
            clip: true
            reuseItems: true
            cacheBuffer: height
            cellWidth: width / 2
            cellHeight: (cellWidth - 30) * 9 / 16 + 58
            model: Theme.store.extensions.skins
            delegate: LyricSkinCard {
                required property var model
                width: catalog.cellWidth - 8
                entryKey: model.entryKey
                title: model.title
                preview: model.preview
                selected: Theme.store.extensions.selectedSkin === entryKey
                onChosen: {
                    if (cardSelected())
                        drawer.showSettings = true;
                    else
                        Theme.store.extensions.chooseSkin(entryKey);
                }
                function cardSelected(): bool {
                    return Theme.store.extensions.selectedSkin === entryKey;
                }
            }
            ScrollBar.vertical: EchoPopupScrollBar {
                surface: drawer
                view: catalog
                resourceActive: drawer.visible && catalog.visible
            }
        }
        ScrollView {
            id: scroll
            visible: drawer.showSettings
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 0
            contentWidth: availableWidth
            clip: true
            ScrollBar.vertical: EchoPopupScrollBar {
                surface: drawer
                view: scroll
                resourceActive: drawer.visible && scroll.visible
            }
            ColumnLayout {
                width: scroll.availableWidth
                spacing: 12
                Loader {
                    id: builtinSettings
                    active: drawer.showSettings && Theme.store.extensions.skin === null
                    Layout.fillWidth: true
                    Layout.preferredHeight: item ? (item as Item).implicitHeight : 0
                    Layout.margins: active ? 16 : 0
                    Layout.bottomMargin: active ? 24 : 0
                    sourceComponent: drawer.portraitSelected ? portraitRecipe : coverRecipe
                }
                Component {
                    id: coverRecipe
                    LyricSkinSettings {
                        appearance: drawer.playback.lyricAppearance
                        coverColor: Theme.store.lyricAccent(drawer.playback.coverColor)
                        uiScale: drawer.uiScale
                    }
                }
                Component {
                    id: portraitRecipe
                    PortraitSkinSettings {
                        appearance: drawer.playback.lyricAppearance
                        coverColor: Theme.store.lyricAccent(drawer.playback.coverColor)
                        uiScale: drawer.uiScale
                    }
                }
                AppearanceHost {
                    id: skinSettingsHost
                    objectName: "pluginSkinSettingsHost"
                    visible: drawer.showSettings && Theme.store.extensions.skin !== null
                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.max(1, implicitHeight)
                    Layout.margins: 16
                    entry: Theme.store.extensions.skin
                    placement: "settings"
                    resourceActive: drawer.visible && drawer.showSettings
                }
                Label {
                    Layout.fillWidth: true
                    Layout.margins: 16
                    visible: drawer.showSettings && Theme.store.extensions.skin !== null && !skinSettingsHost.available
                    text: qsTr("此皮肤未提供专属设置")
                    color: Theme.floatingSecondaryText
                    font: Theme.font(13, Theme.regularWeight)
                }
                Label {
                    Layout.fillWidth: true
                    Layout.margins: 16
                    text: Theme.store.extensions.skin ? Theme.store.extensions.skin.configuration.errorMessage : ""
                    visible: drawer.showSettings && text.length > 0
                    color: Theme.floating.dangerText
                    font: Theme.font(12, Theme.regularWeight)
                    wrapMode: Text.Wrap
                }
                EchoSoftButton {
                    visible: drawer.showSettings && Theme.store.extensions.skin !== null
                    Layout.margins: 16
                    text: qsTr("恢复默认")
                    onClicked: Theme.store.extensions.skin.configuration.reset()
                }
            }
        }
    }
}
