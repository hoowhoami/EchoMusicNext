pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Controls
import EchoMusic.Theme
import EchoMusic.Shell
import EchoMusic.Playback

SettingsSection {
    id: section
    required property WindowController windowService
    property real uiScale: 1
    required property PlaybackService playback
    readonly property FloatingPlayerController windows: windowService.floating
    readonly property FloatingLyricPreferences style: windows.desktop
    heading: qsTr("桌面歌词")
    glyph: "device-desktop"

    SettingsRow {
        heading: qsTr("置顶显示")
        description: qsTr("关闭后歌词窗口不会始终覆盖其他窗口和全屏应用")
        EchoSwitch {
            colors: Theme.floating
            checked: section.style.alwaysOnTop
            enabled: true
            Accessible.name: qsTr("置顶显示")
            onToggled: section.style.alwaysOnTop = checked
        }
    }
    SettingsDivider {}
    SettingsRow {
        heading: qsTr("锁定时显示解锁按钮")
        description: section.windows.wayland ? qsTr("当前桌面不支持浮动解锁按钮，请从托盘或设置解锁") : qsTr("关闭后可从托盘菜单或此处解锁")
        EchoSwitch {
            colors: Theme.floating
            checked: !section.windows.wayland && section.style.showUnlockButton
            enabled: !section.windows.wayland
            Accessible.name: qsTr("锁定时显示解锁按钮")
            onToggled: section.style.showUnlockButton = checked
        }
    }
    SettingsDivider {}
    SettingsRow {
        visible: section.windows.locked
        heading: qsTr("桌面歌词已锁定")
        description: qsTr("解锁后可拖动歌词窗口并使用控制按钮")
        EchoSoftButton {
            colors: Theme.floating
            size: EchoControlSize.Compact
            text: qsTr("解锁")
            onClicked: section.windows.locked = false
        }
    }
    SettingsDivider {
        visible: section.windows.locked
    }
    SettingsRow {
        heading: qsTr("显示布局")
        description: qsTr("选择桌面歌词的排列方式")
        EchoSelect {
            colors: Theme.floating
            implicitWidth: 180
            model: [qsTr("横排"), qsTr("竖排")]
            currentIndex: ["horizontal", "vertical"].indexOf(section.style.layout)
            onActivated: section.style.layout = ["horizontal", "vertical"][currentIndex]
        }
    }
    SettingsDivider {}
    SettingsRow {
        heading: qsTr("下一行预览")
        description: qsTr("未显示翻译和音译时预览下一行歌词")
        EchoSwitch {
            colors: Theme.floating
            checked: section.style.showNextLinePreview
            enabled: true
            Accessible.name: qsTr("下一行预览")
            onToggled: section.style.showNextLinePreview = checked
        }
    }
    SettingsDivider {}
    FontFamilySettingsRow {
        heading: qsTr("桌面歌词字体")
        description: qsTr("桌面歌词窗口使用的字体，跟随全局或单独指定")
        selectorName: "settingsDesktopLyricFont"
        family: Theme.store.desktopLyricFont
        onFamilySelected: selectedFamily => Theme.store.desktopLyricFont = selectedFamily
    }
    SettingsDivider {}

    SettingsRow {
        heading: qsTr("文字对齐")
        description: qsTr("选择桌面歌词的对齐方式")
        EchoSelect {
            colors: Theme.floating
            implicitWidth: 180
            model: [qsTr("左对齐"), qsTr("居中"), qsTr("右对齐"), qsTr("交替")]
            currentIndex: ["left", "center", "right", "both"].indexOf(section.style.alignment)
            onActivated: section.style.alignment = ["left", "center", "right", "both"][currentIndex]
        }
    }
    SettingsDivider {}
    SettingsRow {
        heading: qsTr("文字阴影")
        description: qsTr("提升歌词在不同桌面背景上的可读性")
        EchoSelect {
            colors: Theme.floating
            implicitWidth: 180
            model: [qsTr("关闭"), qsTr("柔和"), qsTr("标准"), qsTr("清晰")]
            currentIndex: ["none", "soft", "normal", "strong"].indexOf(section.style.shadowStrength)
            onActivated: section.style.shadowStrength = ["none", "soft", "normal", "strong"][currentIndex]
        }
    }
    SettingsDivider {}
    SettingsRow {
        heading: qsTr("文字粗体")
        description: qsTr("使用加粗字重显示歌词")
        EchoSwitch {
            colors: Theme.floating
            checked: section.style.bold
            enabled: true
            Accessible.name: qsTr("文字粗体")
            onToggled: section.style.bold = checked
        }
    }
    SettingsDivider {}
    LyricColorSettingsRow {
        uiScale: section.uiScale
        heading: qsTr("文字颜色")
        description: qsTr("分别设置已播与未播字色，也可跟随封面取色")
        customized: section.style.playedColor !== "#31cfa1" || section.style.unplayedColor !== "#7a7a7a" || followsCover || unplayedFollowsCover
        coverColor: section.playback.coverColor
        playedColor: section.style.playedColor || Theme.accentText
        unplayedColor: section.style.unplayedColor || Theme.floatingSecondaryText
        followsCover: section.style.followCoverColor
        unplayedFollowsCover: section.style.unplayedFollowCoverColor
        onColorChosen: (unplayed, value, follow) => {
            if (unplayed) {
                section.style.unplayedFollowCoverColor = follow;
                if (!follow)
                    section.style.unplayedColor = value;
            } else {
                section.style.followCoverColor = follow;
                if (!follow)
                    section.style.playedColor = value;
            }
        }
        onResetRequested: section.style.resetColors()
    }
    SettingsNotice {
        visible: section.windows.wayland
        text: qsTr("Wayland 下窗口位置、置顶和鼠标穿透由桌面合成器决定；可通过托盘或设置解锁。")
    }
}
