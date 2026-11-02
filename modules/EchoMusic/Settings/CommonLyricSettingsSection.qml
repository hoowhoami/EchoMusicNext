pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import EchoMusic.Controls
import EchoMusic.Theme
import EchoMusic.Playback

SettingsSection {
    id: section
    required property PlaybackService playback
    heading: qsTr("通用")
    glyph: "music-share"
    SettingsRow {
        heading: qsTr("显示翻译")
        description: qsTr("有翻译时显示")
        EchoSwitch {
            colors: Theme.floating
            checked: section.playback.lyricText.wantTranslation
            Accessible.name: qsTr("显示翻译")
            onToggled: section.playback.lyricText.wantTranslation = checked
        }
    }
    SettingsDivider {}
    SettingsRow {
        heading: qsTr("显示音译")
        description: qsTr("有音译时显示")
        EchoSwitch {
            colors: Theme.floating
            checked: section.playback.lyricText.wantRomanization
            Accessible.name: qsTr("显示音译")
            onToggled: section.playback.lyricText.wantRomanization = checked
        }
    }
    SettingsDivider {}
    SettingsRow {
        heading: qsTr("音译样式")
        description: qsTr("各歌词视图显示音译的方式")
        EchoSelect {
            colors: Theme.floating
            implicitWidth: 180
            model: [qsTr("独立一行"), qsTr("注音")]
            currentIndex: section.playback.lyricText.romanizationAsRuby ? 1 : 0
            onActivated: section.playback.lyricText.romanizationAsRuby = currentIndex === 1
        }
    }
    SettingsDivider {}
    SettingsRow {
        heading: qsTr("歌词文字转换")
        description: qsTr("对歌词文本进行简繁转换")
        EchoSelect {
            readonly property var conversionModes: ["none", "simplified", "traditional", "traditional-variants", "traditional-tw", "traditional-hk"]
            colors: Theme.floating
            objectName: "lyricTextConversion"
            implicitWidth: 180
            model: [qsTr("原文"), qsTr("转简体"), qsTr("转繁体"), qsTr("转异体繁体"), qsTr("转台湾正体"), qsTr("转香港繁体")]
            currentIndex: conversionModes.indexOf(section.playback.lyricText.conversion)
            onActivated: section.playback.lyricText.conversion = conversionModes[currentIndex]
        }
    }
    SettingsDivider {}
    SettingsRow {
        heading: qsTr("全局歌词时间偏移")
        description: qsTr("正数让歌词提前，负数让歌词延后；与单曲微调叠加")
        EchoSoftButton {
            text: qsTr("重置")
            enabled: section.playback.lyricText.globalOffsetMs !== 0
            onClicked: section.playback.lyricText.globalOffsetMs = 0
        }
        EchoSpinBox {
            objectName: "globalLyricOffset"
            implicitWidth: 130
            from: -20
            to: 20
            decimals: 1
            stepSize: .1
            value: section.playback.lyricText.globalOffsetMs / 1000
            onValueModified: section.playback.lyricText.globalOffsetMs = Math.round(value * 1000)
        }
        Label {
            text: qsTr("秒")
            font: Theme.font(12, Theme.regularWeight)
            color: Theme.floatingSecondaryText
        }
    }
    SettingsDivider {}
    SettingsRow {
        heading: qsTr("单曲微调步长")
        description: qsTr("播放页和桌面歌词前进 / 后退按钮每次调整的时间量")
        EchoSpinBox {
            implicitWidth: 130
            from: .1
            to: 5
            decimals: 1
            stepSize: .1
            value: section.playback.lyricText.offsetStep
            onValueModified: section.playback.lyricText.offsetStep = value
        }
        Label {
            text: qsTr("秒")
            font: Theme.font(12, Theme.regularWeight)
            color: Theme.floatingSecondaryText
        }
    }
    SettingsDivider {}
    SettingsRow {
        heading: qsTr("歌词过滤")
        description: qsTr("过滤制作信息、版权声明等非歌词内容，在播放页和桌面歌词中生效")
        EchoSwitch {
            colors: Theme.floating
            checked: section.playback.lyricText.filterEnabled
            Accessible.name: qsTr("歌词过滤")
            onToggled: section.playback.lyricText.filterEnabled = checked
        }
    }
    SettingsRow {
        visible: section.playback.lyricText.filterEnabled
        heading: qsTr("过滤表达式")
        description: qsTr("正则表达式，留空使用默认规则")
        statusText: section.playback.lyricText.filterError
        EchoTextField {
            objectName: "lyricFilterPattern"
            colors: Theme.floating
            implicitWidth: 220
            text: section.playback.lyricText.filterPattern
            placeholderText: qsTr("默认过滤规则")
            onEditingFinished: section.playback.lyricText.filterPattern = text
        }
        EchoSoftButton {
            colors: Theme.floating
            text: qsTr("恢复默认")
            onClicked: section.playback.lyricText.filterPattern = ""
        }
    }
}
