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
    required property ShortcutService shortcuts
    heading: qsTr("快捷键")
    glyph: "keyboard"
    RowLayout {
        Layout.fillWidth: true
        Layout.fillHeight: false
        Layout.leftMargin: 4
        Layout.rightMargin: 4
        spacing: 16
        Label {
            Layout.fillWidth: true
            text: qsTr("功能说明")
            font: Theme.font(12, Theme.semiboldWeight)
            color: Theme.floatingSecondaryText
        }
        Repeater {
            model: [qsTr("快捷键"), qsTr("全局快捷键")]
            Label {
                required property string modelData
                Layout.preferredWidth: 140
                text: modelData
                font: Theme.font(12, Theme.semiboldWeight)
                horizontalAlignment: Text.AlignHCenter
                color: Theme.floatingSecondaryText
            }
        }
        Item {
            Layout.preferredWidth: 40
        }
    }
    SettingsDivider {}
    ColumnLayout {
        Layout.fillWidth: true
        spacing: 12
        // Fifteen fixed commands form a bounded settings section,
        // instantiated only while the section is open.
        Repeater {
            model: section.shortcuts
            RowLayout {
                id: entry
                required property int index
                required property string shortcutTitle
                required property string shortcutDescription
                required property string localLabel
                required property string globalLabel
                required property string globalError
                required property bool modified
                Layout.fillWidth: true
                Layout.fillHeight: false
                spacing: 16
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    spacing: 4
                    Label {
                        Layout.fillWidth: true
                        text: entry.shortcutTitle
                        font: Theme.font(13, Theme.semiboldWeight)
                        color: Theme.floatingText
                        wrapMode: Text.Wrap
                    }
                    Label {
                        Layout.fillWidth: true
                        text: entry.shortcutDescription
                        font: Theme.font(12, Theme.regularWeight)
                        color: Theme.floatingSecondaryText
                        wrapMode: Text.Wrap
                    }
                }
                ShortcutRecorder {
                    objectName: "settingsShortcutLocal" + entry.index
                    Layout.preferredWidth: 140
                    service: section.shortcuts
                    row: entry.index
                    shortcutLabel: entry.localLabel
                    transform: Translate {
                        x: 4
                    }
                }
                ShortcutRecorder {
                    objectName: "settingsShortcutGlobal" + entry.index
                    Layout.preferredWidth: 140
                    service: section.shortcuts
                    row: entry.index
                    global: true
                    shortcutLabel: entry.globalLabel
                    errorText: entry.globalError
                    transform: Translate {
                        x: 4
                    }
                }
                Item {
                    Layout.preferredWidth: 40
                    implicitHeight: reset.implicitHeight
                    EchoSoftButton {
                        id: reset
                        anchors.centerIn: parent
                        opacity: entry.modified ? 1 : 0
                        enabled: entry.modified
                        Accessible.ignored: !entry.modified
                        text: qsTr("重置")
                        colors: Theme.floating
                        onClicked: section.shortcuts.resetBinding(entry.index)
                    }
                }
            }
        }
    }
    SettingsDivider {}
    SettingsRow {
        heading: qsTr("启用全局快捷键")
        description: qsTr("应用在后台也可响应快捷键")
        statusText: section.shortcuts.registrationError
        EchoSwitch {
            objectName: "settingsGlobalShortcuts"
            checked: section.shortcuts.globalEnabled
            colors: Theme.floating
            Accessible.name: qsTr("启用全局快捷键")
            onToggled: section.shortcuts.globalEnabled = checked
        }
    }
    SettingsDivider {}
    SettingsRow {
        heading: qsTr("快捷键冲突提示")
        description: qsTr("录制或注册快捷键发生冲突时弹出提示")
        EchoSwitch {
            checked: section.shortcuts.conflictPrompt
            colors: Theme.floating
            Accessible.name: qsTr("快捷键冲突提示")
            onToggled: section.shortcuts.conflictPrompt = checked
        }
    }
    SettingsDivider {}
    SettingsRow {
        heading: qsTr("屏蔽默认按键行为")
        description: qsTr("非输入状态下屏蔽方向键、Home/End、PageUp/PageDown 的默认操作；空格始终用于播放/暂停")
        EchoSwitch {
            checked: section.shortcuts.suppressDefault
            colors: Theme.floating
            Accessible.name: qsTr("屏蔽默认按键行为")
            onToggled: section.shortcuts.suppressDefault = checked
        }
    }
    SettingsDivider {}
    SettingsRow {
        heading: qsTr("恢复默认")
        description: qsTr("恢复所有快捷键为默认")
        EchoSoftButton {
            text: qsTr("恢复默认")
            colors: Theme.floating
            onClicked: section.shortcuts.resetAll()
        }
    }
}
