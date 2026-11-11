pragma ComponentBehavior: Bound
import QtQuick
import EchoMusic.Controls
import EchoMusic.Theme
import EchoMusic.Shell
import EchoMusic.Playback

SettingsSection {
    id: section

    property bool editingSecondary: false
    required property PlaybackService playback
    readonly property MenuBarLyricPreferences preferences: windowService.menuBarLyrics.settings
    property real uiScale: 1
    required property WindowController windowService

    glyph: "layout-navbar"
    heading: qsTr("菜单栏歌词")
    objectName: "menuBarLyricSettings"

    SettingsRow {
        description: qsTr("在 macOS 菜单栏显示歌词，鼠标移入时显示播放控制")
        heading: qsTr("启用菜单栏歌词")

        EchoSwitch {
            Accessible.name: qsTr("启用菜单栏歌词")
            checked: section.preferences.enabled
            colors: Theme.floating
            objectName: "menuBarLyricEnabled"

            onToggled: section.preferences.enabled = checked
        }
    }
    SettingsDivider {}
    SettingsRow {
        description: qsTr("没有所选副行时显示原文；双行字号根据菜单栏高度自动调整，副行小于原文")
        heading: qsTr("显示模式")

        EchoSelect {
            colors: Theme.floating
            currentIndex: section.preferences.displayMode
            implicitWidth: 180
            model: [qsTr("原文"), section.preferences.secondaryContent === 0 ? qsTr("译文优先") : section.preferences.secondaryContent === 1 ? qsTr("音译优先") : qsTr("副行优先"), qsTr("单行双语"), qsTr("双行双语")]
            objectName: "menuBarLyricDisplayMode"

            onActivated: section.preferences.displayMode = currentIndex
        }
    }
    SettingsDivider {}
    SettingsRow {
        heading: qsTr("副行内容")
        description: qsTr("普通模式可显示音译 / 译文；显示注音时只保留原文与上方注音，隐藏译文")
        EchoSelect {
            objectName: "menuBarLyricSecondaryContent"
            Accessible.name: qsTr("菜单栏歌词副行内容")
            colors: Theme.floating
            implicitWidth: 180
            model: [qsTr("译文"), qsTr("音译"), qsTr("音译 / 译文")]
            currentIndex: section.preferences.secondaryContent
            onActivated: section.preferences.secondaryContent = currentIndex
        }
    }
    SettingsDivider {}
    SettingsRow {
        description: qsTr("固定歌词区域宽度，长句在区域内省略或滚动；菜单栏拥挤时可调小（128 - 800 点）")
        heading: qsTr("最大宽度")

        EchoSpinBox {
            colors: Theme.floating
            from: 128
            implicitWidth: 130
            stepSize: 20
            to: 800
            value: section.preferences.maxWidth

            onValueModified: section.preferences.maxWidth = Math.round(value)
        }
    }
    SettingsDivider {}
    FontFamilySettingsRow {
        description: qsTr("跟随全局或单独指定菜单栏歌词的字体")
        family: Theme.store.menuBarLyricFont
        heading: qsTr("菜单栏歌词字体")
        selectorName: "settingsMenuBarLyricFont"

        onFamilySelected: selectedFamily => Theme.store.menuBarLyricFont = selectedFamily
    }
    SettingsDivider {}
    SettingsRow {
        heading: qsTr("歌词字号")

        EchoSpinBox {
            Accessible.name: qsTr("菜单栏歌词字号")
            colors: Theme.floating
            from: 10
            implicitWidth: 130
            objectName: "menuBarLyricFontSize"
            to: 22
            value: section.preferences.fontSize

            onValueModified: section.preferences.fontSize = Math.round(value)
        }
    }
    SettingsDivider {}
    SettingsRow {
        heading: qsTr("歌词字重")

        EchoSelect {
            colors: Theme.floating
            currentIndex: Math.round((section.preferences.fontWeight - 400) / 100)
            implicitWidth: 180
            model: [qsTr("常规"), qsTr("中等"), qsTr("半粗"), qsTr("粗体")]
            objectName: "menuBarLyricFontWeight"

            onActivated: section.preferences.fontWeight = 400 + currentIndex * 100
        }
    }
    SettingsDivider {}
    SettingsRow {
        description: qsTr("跟随系统可自动适应菜单栏的深浅外观")
        heading: qsTr("原文颜色")

        EchoSelect {
            colors: Theme.floating
            currentIndex: section.preferences.colorMode
            implicitWidth: 180
            model: [qsTr("跟随系统"), qsTr("跟随封面"), qsTr("自定义")]

            onActivated: section.preferences.colorMode = currentIndex
        }
    }
    SettingsRow {
        heading: qsTr("副行颜色")
        description: qsTr("译文、音译共用副行颜色；跟随系统时使用菜单栏正文色的80%不透明度")
        EchoSelect {
            colors: Theme.floating
            implicitWidth: 180
            model: [qsTr("跟随系统"), qsTr("跟随封面"), qsTr("自定义")]
            currentIndex: section.preferences.secondaryColorMode
            onActivated: section.preferences.secondaryColorMode = currentIndex
        }
    }
    SettingsRow {
        heading: qsTr("自定义原文颜色")
        visible: section.preferences.colorMode === 2
        EchoSoftButton {
            colors: Theme.floating
            text: section.preferences.primaryColor.toString()
            onClicked: {
                section.editingSecondary = false;
                colorPicker.open();
            }
        }
    }
    SettingsRow {
        heading: qsTr("自定义副行颜色")
        visible: section.preferences.secondaryColorMode === 2
        EchoSoftButton {
            colors: Theme.floating
            text: section.preferences.secondaryColor.toString()
            onClicked: {
                section.editingSecondary = true;
                colorPicker.open();
            }
        }
    }
    SettingsDivider {}
    SettingsRow {
        heading: qsTr("不透明度")

        EchoSpinBox {
            colors: Theme.floating
            from: 20
            implicitWidth: 130
            stepSize: 5
            to: 100
            value: section.preferences.opacity

            onValueModified: section.preferences.opacity = Math.round(value)
        }
    }
    SettingsDivider {}
    SettingsRow {
        description: qsTr("逐字歌词按真实时间着色，普通歌词整行显示")
        heading: qsTr("逐字进度")

        EchoSwitch {
            Accessible.name: qsTr("菜单栏歌词逐字进度")
            checked: section.preferences.wordByWord
            colors: Theme.floating

            onToggled: section.preferences.wordByWord = checked
        }
    }
    SettingsDivider {}
    SettingsRow {
        description: qsTr("关闭时省略超出部分；暂停或减少动态效果时停止滚动")
        heading: qsTr("长句滚动")

        EchoSwitch {
            Accessible.name: qsTr("菜单栏歌词长句滚动")
            checked: section.preferences.scrolling
            colors: Theme.floating

            onToggled: section.preferences.scrolling = checked
        }
    }
    SettingsDivider {}
    SettingsRow {
        description: qsTr("歌词过滤、文字转换和时间偏移沿用通用歌词设置")
        heading: qsTr("无歌词或间奏时")

        EchoSelect {
            colors: Theme.floating
            currentIndex: section.preferences.idleBehavior
            implicitWidth: 180
            model: [qsTr("显示歌名"), qsTr("保留上一句"), qsTr("隐藏")]

            onActivated: section.preferences.idleBehavior = currentIndex
        }
    }
    SettingsDivider {}
    SettingsRow {
        heading: qsTr("暂停时")

        EchoSelect {
            colors: Theme.floating
            currentIndex: section.preferences.pauseBehavior
            implicitWidth: 180
            model: [qsTr("保持显示"), qsTr("降低不透明度"), qsTr("隐藏")]

            onActivated: section.preferences.pauseBehavior = currentIndex
        }
    }
    EchoPopupLoader {
        id: colorPicker

        hostItem: section

        sourceComponent: EchoColorPickerDialog {
            title: section.editingSecondary ? qsTr("副行颜色") : qsTr("原文颜色")
            uiScale: section.uiScale
            value: (section.editingSecondary ? section.preferences.secondaryColor : section.preferences.primaryColor).toString()

            onConfirmed: value => {
                if (section.editingSecondary)
                    section.preferences.secondaryColor = value;
                else
                    section.preferences.primaryColor = value;
            }
        }
    }
}
