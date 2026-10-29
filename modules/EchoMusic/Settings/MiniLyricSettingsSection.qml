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
    readonly property FloatingLyricPreferences style: windows.mini
    readonly property color defaultForeground: (Theme.store.extensionAppearance.mini || {}).text || Theme.floatingText
    heading: qsTr("Mini 歌词")
    glyph: "picture-in-picture"
    LyricColorSettingsRow {
        objectName: "miniLyricColors"
        uiScale: section.uiScale
        heading: qsTr("文字颜色")
        description: qsTr("分别设置已播与未播字色，也可跟随封面取色")
        customized: section.style.playedColor.length > 0 || section.style.unplayedColor.length > 0 || followsCover || unplayedFollowsCover
        coverColor: section.playback.coverColor
        playedColor: LyricColors.miniPlayed(section.style, Theme.store.lyricAccent(section.playback.coverColor), section.defaultForeground)
        unplayedColor: LyricColors.miniUnplayed(section.style, Theme.store.lyricAccent(section.playback.coverColor), section.defaultForeground)
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
    SettingsDivider {}
    SettingsRow {
        heading: qsTr("主歌词字号")
        description: qsTr("Mini 模式主歌词文字大小")
        EchoSpinBox {
            implicitWidth: 130
            from: 12
            to: 22
            stepSize: 1
            value: section.style.fontSize
            onValueModified: section.style.fontSize = Math.round(value)
        }
    }
    SettingsDivider {}
    SettingsRow {
        heading: qsTr("副歌词字号")
        description: qsTr("翻译和音译的文字大小")
        EchoSpinBox {
            implicitWidth: 130
            from: 10
            to: 18
            stepSize: 1
            value: section.style.secondaryFontSize
            onValueModified: section.style.secondaryFontSize = Math.round(value)
        }
    }
    SettingsDivider {}
    SettingsRow {
        heading: qsTr("行间距")
        description: qsTr("主歌词与副歌词之间的间距")
        EchoSpinBox {
            implicitWidth: 130
            from: 0
            to: 12
            stepSize: 1
            value: section.style.lineGap
            onValueModified: section.style.lineGap = Math.round(value)
        }
    }
    SettingsDivider {}
    SettingsRow {
        heading: qsTr("文字字重")
        description: qsTr("Mini 模式歌词的文字粗细")
        EchoSelect {
            colors: Theme.floating
            implicitWidth: 180
            model: [qsTr("常规"), qsTr("半粗"), qsTr("加粗")]
            currentIndex: [400, 600, 780].indexOf(section.style.fontWeight)
            onActivated: section.style.fontWeight = [400, 600, 780][currentIndex]
        }
    }
    SettingsDivider {}
    SettingsRow {
        heading: qsTr("文字对齐")
        description: qsTr("Mini 模式歌词的对齐方式")
        EchoSelect {
            colors: Theme.floating
            implicitWidth: 180
            model: [qsTr("左对齐"), qsTr("居中"), qsTr("右对齐")]
            currentIndex: ["left", "center", "right"].indexOf(section.style.alignment)
            onActivated: section.style.alignment = ["left", "center", "right"][currentIndex]
        }
    }
    SettingsDivider {}
    SettingsRow {
        heading: qsTr("封面模糊背景")
        description: qsTr("使用当前封面的柔和背景")
        EchoSwitch {
            colors: Theme.floating
            checked: section.style.backgroundBlur
            enabled: true
            Accessible.name: qsTr("封面模糊背景")
            onToggled: section.style.backgroundBlur = checked
        }
    }
    SettingsDivider {}
    SettingsRow {
        heading: qsTr("排版与背景")
        description: qsTr("恢复默认字号、字重、行距、对齐和背景")
        EchoSoftButton {
            colors: Theme.floating
            text: qsTr("重置")
            onClicked: section.style.resetLayout()
        }
    }
}
