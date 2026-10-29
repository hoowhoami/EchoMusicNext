pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Controls
import EchoMusic.Theme
import EchoMusic.Shell

SettingsSection {
    id: section
    required property ShellSettings settings
    required property WindowController windowService
    heading: qsTr("界面与外观")
    glyph: "eye"
    SettingsRow {
        heading: qsTr("深浅色模式")
        description: qsTr("选择主题的深浅配色，自定义背景保留所选文字颜色")
        DisplayModeControl {
            objectName: "appearanceMode"
            colors: Theme.floating
            Layout.preferredWidth: 144
        }
    }
    SettingsDivider {}
    FontFamilySettingsRow {
        heading: qsTr("全局字体")
        description: qsTr("应用到软件内所有区域的字体")
        selectorName: "settingsGlobalFont"
        followGlobal: false
        family: Theme.store.globalFont
        onFamilySelected: selectedFamily => Theme.store.globalFont = selectedFamily
    }
    SettingsDivider {}
    SettingsRow {
        heading: qsTr("全局字号")
        description: qsTr("缩放软件内所有文字（80% - 150%）")
        EchoSelect {
            objectName: "settingsGlobalFontSizeScale"
            colors: Theme.floating
            implicitWidth: 180
            model: ["80%", "90%", "100%", "110%", "125%", "150%"]
            currentIndex: [80, 90, 100, 110, 125, 150].indexOf(Theme.store.globalFontSizeScale)
            Accessible.name: qsTr("全局字号")
            onActivated: index => Theme.store.globalFontSizeScale = [80, 90, 100, 110, 125, 150][index]
        }
    }
    SettingsDivider {}
    SettingsRow {
        heading: qsTr("全局字重")
        description: qsTr("整体调整软件内文字的粗细")
        EchoSelect {
            objectName: "settingsGlobalFontWeightOffset"
            colors: Theme.floating
            implicitWidth: 180
            model: [qsTr("标准"), qsTr("偏粗"), qsTr("加粗"), qsTr("特粗")]
            currentIndex: [0, 100, 200, 300].indexOf(Theme.store.globalFontWeightOffset)
            Accessible.name: qsTr("全局字重")
            onActivated: index => Theme.store.globalFontWeightOffset = [0, 100, 200, 300][index]
        }
    }
    SettingsDivider {}
    SettingsRow {
        heading: qsTr("界面缩放")
        description: qsTr("立即调整整个界面并自动保存。") + (Qt.platform.os === "osx" ? "⌘" : "Ctrl") + qsTr(" + 加号 / 减号缩放，0 重置")
        EchoSoftButton {
            objectName: "settingsZoomOut"
            text: "−"
            colors: Theme.floating
            implicitWidth: EchoControlSize.heightFor(EchoControlSize.Standard)
            Accessible.name: qsTr("缩小界面")
            enabled: section.settings.uiScale > Math.pow(1.2, -8) + 0.001
            onClicked: section.settings.zoomOut()
        }
        Label {
            font: Theme.font(13, Theme.regularWeight)
            text: Math.round(section.settings.uiScale * 100) + "%"
            color: Theme.floatingText
            Layout.preferredWidth: 48
            horizontalAlignment: Text.AlignHCenter
        }
        EchoSoftButton {
            objectName: "settingsZoomIn"
            text: "+"
            colors: Theme.floating
            implicitWidth: EchoControlSize.heightFor(EchoControlSize.Standard)
            Accessible.name: qsTr("放大界面")
            enabled: section.settings.uiScale < Math.pow(1.2, 8) - 0.001
            onClicked: section.settings.zoomIn()
        }
        EchoSoftButton {
            text: qsTr("重置")
            colors: Theme.floating
            onClicked: section.settings.resetZoom()
        }
    }
    SettingsDivider {}
    SettingsRow {
        visible: !section.windowService.macOS
        heading: qsTr("全屏按钮")
        description: qsTr("在标题栏显示全屏按钮")
        EchoSwitch {
            objectName: "settingsFullscreenButton"
            checked: section.settings.showFullscreenButton
            Accessible.name: qsTr("全屏按钮")
            onToggled: section.settings.showFullscreenButton = checked
        }
    }
    SettingsDivider {
        visible: !section.windowService.macOS
    }
    SettingsRow {
        heading: qsTr("搜索框默认推荐词")
        description: qsTr("在搜索框显示默认推荐词，可能有广告")
        EchoSwitch {
            objectName: "settingsDefaultSearch"
            checked: section.settings.searchDefaultEnabled
            Accessible.name: qsTr("搜索框默认推荐词")
            onToggled: section.settings.searchDefaultEnabled = checked
        }
    }
}
