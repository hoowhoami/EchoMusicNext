pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Controls
import EchoMusic.Theme
import EchoMusic.Shell

SettingsSection {
    id: section
    required property WindowController windowService
    readonly property TaskbarLyricController taskbar: windowService.taskbar
    readonly property TaskbarLyricPreferences style: taskbar.settings
    heading: qsTr("任务栏歌词")
    glyph: "layout-bottombar"
    visible: taskbar.available

    SettingsRow {
        heading: qsTr("启用任务栏歌词")
        description: qsTr("在任务栏空闲区域显示当前歌词，自动避开按钮与托盘")
        EchoSwitch {
            colors: Theme.floating
            checked: section.taskbar.enabled
            enabled: section.taskbar.available
            Accessible.name: qsTr("启用任务栏歌词")
            onToggled: section.taskbar.enabled = checked
        }
    }
    SettingsDivider {}
    SettingsRow {
        heading: qsTr("显示位置")
        description: qsTr("自动模式优先显示在所选一侧，空间不足时换到另一侧；固定模式贴住任务栏边缘；拖拽模式可手动摆放")
        EchoSelect {
            colors: Theme.floating
            implicitWidth: 180
            model: [qsTr("自动靠左"), qsTr("自动靠右"), qsTr("固定靠左"), qsTr("固定靠右"), qsTr("手动拖拽")]
            // The picker order is TaskbarLyricPreferences::Position's declaration
            // order, so the index is the preference value itself.
            currentIndex: section.style.position
            onActivated: section.style.position = currentIndex
        }
    }
    SettingsDivider {}
    SettingsRow {
        heading: qsTr("最大宽度")
        description: qsTr("超出空闲区域时自动收缩（160 - 800 像素）")
        EchoSpinBox {
            implicitWidth: 130
            // The picker owns the range; the preferences clamp the stored
            // value to the same bounds (see TaskbarLyricPreferences).
            from: 160
            to: 800
            stepSize: 20
            value: section.style.maxWidth
            // Every field commits on leaving the input (EchoSpinBox), so the
            // steppers and typed input both arrive here.
            onValueModified: section.style.maxWidth = Math.round(value)
        }
    }
    SettingsDivider {}
    SettingsRow {
        heading: qsTr("歌词字号")
        description: qsTr("任务栏内歌词文字大小（11 - 22 像素）")
        EchoSpinBox {
            implicitWidth: 130
            // Same split as the width picker above.
            from: 11
            to: 22
            stepSize: 1
            value: section.style.fontSize
            // Same commit path as the width field above.
            onValueModified: section.style.fontSize = Math.round(value)
        }
    }
    SettingsDivider {}
    SettingsRow {
        heading: qsTr("显示翻译")
        description: qsTr("歌词带翻译时在任务栏内一并显示")
        EchoSwitch {
            colors: Theme.floating
            checked: section.style.showTranslation
            Accessible.name: qsTr("显示翻译")
            onToggled: section.style.showTranslation = checked
        }
    }
    SettingsDivider {}
    SettingsRow {
        heading: qsTr("逐字进度")
        description: qsTr("逐字歌词按时间着色，普通歌词整行高亮")
        EchoSwitch {
            colors: Theme.floating
            checked: section.style.wordByWord
            Accessible.name: qsTr("逐字进度")
            onToggled: section.style.wordByWord = checked
        }
    }
}
