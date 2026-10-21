pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Shell
import EchoMusic.Theme
import EchoMusic.Controls

RoutePage {
    id: page
    objectName: "themeCenterPage"
    readonly property EchoColorPickerDialog skinColorDialog: skinPickerOwner.item
    readonly property ThemeAppearanceDrawer appearanceDrawer: adjustmentsOwner.item
    property WindowController windowService: null
    property int tab: 0
    Component.onCompleted: {
        tab = Theme.store.extensions.theme ? 0 : Theme.store.themeKey === "custom" ? 2 : Theme.store.themeKey === "solid" ? 1 : 0;
    }
    Component.onDestruction: {
        Theme.store.endBackgroundPreview();
        Theme.store.extensions.cancelPreview();
    }
    onTabChanged: {
        if (tab === 2)
            Theme.store.previewBackground();
        else
            Theme.store.endBackgroundPreview();
    }
    readonly property var solids: ["#7da3ce", "#6eafb7", "#87a793", "#9293bf", "#bd97bb", "#ce929c", "#bcaa81", "#ac9b8e", "#6b7180", "#5485ad", "#487b76", "#67765e", "#786889", "#a56d7f", "#ac7862", "#998650", "#586f82", "#536d62", "#66617b", "#866e76", "#806f62", "#73715c", "#4d5766", "#4b5350"]
    onPageActiveChanged: {
        if (!pageActive) {
            Theme.store.endBackgroundPreview();
            Theme.store.extensions.cancelPreview();
            extensionSettingsOwner.close();
            adjustmentsOwner.close();
            skinPickerOwner.close();
        }
    }
    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: 32
        anchors.rightMargin: 32
        anchors.topMargin: 26
        anchors.bottomMargin: 20
        spacing: 0
        RowLayout {
            Layout.fillHeight: false
            objectName: "themeHeading"
            Layout.fillWidth: true
            Layout.bottomMargin: 20
            spacing: 16
            ColumnLayout {
                Layout.fillHeight: false
                Layout.fillWidth: false
                spacing: 8
                Label {
                    text: qsTr("个性主题")
                    color: Theme.text
                    font: Theme.spacedFont(28, Theme.strongWeight, -0.5)
                }
                RowLayout {
                    Layout.fillHeight: false
                    spacing: 6
                    Rectangle {
                        implicitWidth: 6
                        implicitHeight: 6
                        radius: 3
                        color: Theme.accent
                    }
                    Label {
                        text: ((Theme.store.backgroundPreview || Theme.store.extensions.previewing) ? qsTr("预览中 · ") : qsTr("正在使用 · ")) + (Theme.store.extensions.theme ? Theme.store.extensions.theme.title : Theme.store.themeKey === "echo" ? "Echo" : Theme.store.themeKey === "solid" ? qsTr("纯色") : qsTr("自定义"))
                        color: Theme.secondaryText
                        font: Theme.font(12, Theme.regularWeight)
                    }
                }
            }
            Item {
                Layout.fillWidth: true
            }
            EchoButton {
                accent: false
                implicitHeight: 36
                objectName: "appearanceAction"
                text: qsTr("外观调整")
                glyph: "palette"
                glyphSpacing: 7
                font: Theme.font(13, Theme.semiboldWeight)
                leftPadding: 12
                rightPadding: 12
                onClicked: adjustmentsOwner.open()
            }
        }
        EchoTabBar {
            objectName: "themeBrowseTabs"
            Layout.preferredWidth: 288
            Layout.maximumWidth: page.width - 64
            Layout.bottomMargin: 26
            labels: [qsTr("主题"), qsTr("纯色"), qsTr("自定义")]
            currentIndex: page.tab
            accessibleName: qsTr("主题浏览")
            onPicked: index => page.tab = index
        }
        EchoPageScrollContainer {
            id: viewport
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: page.tab !== 2
            Layout.leftMargin: -32
            Layout.rightMargin: -32
            pageActive: page.pageActive
            scrollView: page.tab === 0 ? catalog : paletteScroll
            resetViews: [catalog, paletteScroll]
            Flickable {
                id: catalog
                objectName: "themeCatalogScroll"
                x: 32
                y: 2
                width: Math.max(0, parent.width - 64)
                height: Math.max(0, parent.height - 2)
                visible: page.tab === 0
                interactive: visible && page.pageActive
                clip: true
                contentWidth: width
                contentHeight: groups.height
                boundsBehavior: Flickable.StopAtBounds
                Column {
                    id: groups
                    width: catalog.width
                    spacing: 24
                    Repeater {
                        model: ["default", "solid", "dynamic"]
                        delegate: Column {
                            id: group
                            required property string modelData
                            readonly property AppearanceListModel candidates: Theme.store.extensions.themeGroup(modelData)
                            width: groups.width
                            visible: candidates.count > 0
                            spacing: 14
                            RowLayout {
                                width: parent.width
                                spacing: 16
                                Label {
                                    text: group.modelData === "default" ? qsTr("静态") : group.modelData === "solid" ? qsTr("纯色") : qsTr("动态")
                                    color: Theme.text
                                    font: Theme.font(14, Theme.semiboldWeight)
                                }
                                Rectangle {
                                    Layout.fillWidth: true
                                    implicitHeight: 1
                                    color: Theme.subtleBorder
                                }
                            }
                            EchoVirtualGridSection {
                                id: section
                                width: group.width + 18
                                columns: Math.max(1, Math.floor(width / 218))
                                cellHeight: (cellWidth - 38) / 1.6 + 20 + 10 + 20 + 4 + 38.4 + 24
                                scrollView: catalog
                                contentTop: group.y + y
                                resourceActive: catalog.visible && page.pageActive
                                model: group.candidates
                                itemCount: group.candidates.count
                                onViewChanged: if (view)
                                    view.objectName = group.modelData === "default" ? "themeCatalog" : "themeCatalog-" + group.modelData
                                delegate: ThemeCatalogCard {
                                    required property string entryKey
                                    required property string title
                                    required property var model
                                    required property url preview
                                    required property AppearanceEntry entry
                                    width: section.cellWidth - 18
                                    description: model.description
                                    themeName: title
                                    previewSource: preview
                                    variants: model.variants || ({})
                                    extension: entry !== null
                                    selected: entryKey === "echo" ? Theme.store.themeKey === "echo" && Theme.store.extensions.selectedTheme.length === 0 : Theme.store.extensions.selectedTheme === entryKey
                                    onChosen: entryKey === "echo" ? Theme.store.selectEcho() : Theme.store.extensions.chooseTheme(entryKey)
                                    onPreviewRequested: Theme.store.extensions.previewTheme(entryKey)
                                    onSettingsRequested: extensionSettingsOwner.open({
                                        entry: entry
                                    })
                                }
                            }
                        }
                    }
                }
                ScrollBar.vertical: EchoPageScrollBar {
                    view: catalog
                    container: viewport
                    viewActive: catalog.visible
                }
            }
            Flickable {
                id: paletteScroll
                x: 32
                y: 2
                width: Math.max(0, parent.width - 64)
                height: Math.max(0, parent.height - 2)
                visible: page.tab === 1
                interactive: visible && page.pageActive
                contentWidth: width
                contentHeight: paletteGallery.implicitHeight + 40
                flickableDirection: Flickable.VerticalFlick
                boundsBehavior: Flickable.StopAtBounds
                ThemePaletteGallery {
                    id: paletteGallery
                    width: paletteScroll.width
                    colors: page.solids
                    onCustomize: skinPickerOwner.open()
                }
                ScrollBar.vertical: EchoPageScrollBar {
                    view: paletteScroll
                    container: viewport
                    viewActive: paletteScroll.visible
                }
            }
        }
        ThemeImageEditor {
            objectName: "themeImageEditor"
            pageActive: page.pageActive
            visible: page.tab === 2
            Layout.fillWidth: true
            Layout.fillHeight: true
            uiScale: page.settings.uiScale
            viewport: Qt.size(page.Window.window ? page.Window.window.width : page.width, page.Window.window ? page.Window.window.height : page.height)
        }
    }
    EchoPopupLoader {
        id: adjustmentsOwner
        objectName: "themeAppearanceDrawerOwner"
        hostItem: page
        sourceComponent: Component {
            ThemeAppearanceDrawer {
                id: adjustments
                windowService: page.windowService
                uiScale: page.settings.uiScale
            }
        }
    }
    EchoPopupLoader {
        id: skinPickerOwner
        objectName: "skinColorPickerOwner"
        hostItem: page
        sourceComponent: Component {
            EchoColorPickerDialog {
                id: skinPicker
                objectName: "skinColorPicker"
                title: qsTr("皮肤颜色")
                uiScale: page.settings.uiScale
                value: Theme.store.seedColor.toString()
                presets: ["#7da3ce", "#6eafb7", "#bd97bb", "#ce929c", "#bcaa81", "#87a793", "#9293bf", "#6b7180", "#0071e3", "#ef4444", "#ec4899", "#8b5cf6", "#6366f1", "#0ea5e9", "#14b8a6", "#22c55e", "#f59e0b", "#f97316"]
                onConfirmed: color => Theme.store.selectSolid(color)
            }
        }
    }
    EchoPopupLoader {
        id: extensionSettingsOwner
        hostItem: page
        sourceComponent: Component {
            EchoAppearanceSettingsDialog {
                uiScale: page.settings.uiScale
            }
        }
    }
}
