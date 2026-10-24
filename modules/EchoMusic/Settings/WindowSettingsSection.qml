pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Controls
import EchoMusic.Shell
import EchoMusic.Theme

ColumnLayout {
    id: section
    required property ShellSettings settings
    required property WindowController windowService
    spacing: 24
    SettingsSection {
        Layout.fillWidth: true
        heading: qsTr("窗口行为")
        glyph: "picture-in-picture"
        SettingsRow {
            heading: qsTr("记住窗口大小")
            description: qsTr("下次启动时恢复窗口大小和位置")
            EchoSwitch {
                objectName: "settingsRememberWindow"
                checked: section.windowService.rememberSize
                Accessible.name: qsTr("记住窗口大小")
                onToggled: section.windowService.rememberSize = checked
            }
        }
        SettingsDivider {}
        SettingsRow {
            heading: qsTr("关闭行为")
            description: qsTr("点击窗口关闭按钮时的应用行为")
            EchoSelect {
                objectName: "settingsCloseBehavior"
                implicitWidth: 180
                model: [qsTr("最小化到托盘"), qsTr("彻底退出程序")]
                colors: Theme.floating
                currentIndex: section.windowService.closeToTray ? 0 : 1
                availableOptions: [section.windowService.trayAvailable, true]
                Accessible.name: qsTr("关闭行为")
                onActivated: section.windowService.closeToTray = currentIndex === 0
            }
        }
    }
    SettingsSection {
        Layout.fillWidth: true
        visible: section.windowService.taskbarButton.available
        heading: qsTr("系统集成")
        glyph: "settings"
        SettingsRow {
            heading: qsTr("任务栏封面预览")
            description: qsTr("悬停任务栏图标时显示专辑封面")
            EchoSwitch {
                objectName: "settingsTaskbarCoverPreview"
                checked: section.windowService.taskbarButton.coverPreviewEnabled
                Accessible.name: qsTr("任务栏封面预览")
                onToggled: section.windowService.taskbarButton.coverPreviewEnabled = checked
            }
        }
        SettingsDivider {}
        SettingsRow {
            heading: qsTr("任务栏播放进度")
            description: qsTr("在任务栏显示播放进度")
            EchoSwitch {
                objectName: "settingsTaskbarProgress"
                checked: section.windowService.taskbarButton.progressEnabled
                Accessible.name: qsTr("任务栏播放进度")
                onToggled: section.windowService.taskbarButton.progressEnabled = checked
            }
        }
    }
    SettingsSection {
        Layout.fillWidth: true
        visible: section.windowService.macOS
        heading: qsTr("系统集成")
        glyph: "settings"
        SettingsRow {
            heading: qsTr("后台运行时在 Dock 栏中隐藏")
            description: qsTr("关闭窗口后隐藏 Dock 图标，恢复窗口时重新显示；仅在「最小化到托盘」时生效")
            EchoSwitch {
                checked: section.windowService.hideDockInBackground
                enabled: section.windowService.closeToTray
                Accessible.name: qsTr("后台运行时在 Dock 栏中隐藏")
                onToggled: section.windowService.hideDockInBackground = checked
            }
        }
        SettingsDivider {}
        SettingsRow {
            heading: qsTr("后台运行时在菜单栏中隐藏")
            description: qsTr("关闭窗口后隐藏菜单栏图标，恢复窗口时重新显示；仅在「最小化到托盘」时生效")
            EchoSwitch {
                checked: section.windowService.hideMenuBarInBackground
                enabled: section.windowService.closeToTray
                Accessible.name: qsTr("后台运行时在菜单栏中隐藏")
                onToggled: section.windowService.hideMenuBarInBackground = checked
            }
        }
        Label {
            Layout.fillWidth: true
            visible: section.windowService.hideDockInBackground && section.windowService.hideMenuBarInBackground
            text: qsTr("最小化到托盘且两个图标均隐藏时，可通过 Finder、Spotlight 或显示窗口全局快捷键恢复。")
            color: Theme.floatingSecondaryText
            font: Theme.font(12, Theme.regularWeight)
            wrapMode: Text.Wrap
        }
    }
    SettingsSection {
        Layout.fillWidth: true
        heading: qsTr("启动行为")
        glyph: "player-play"
        SettingsRow {
            heading: qsTr("开机自启动")
            description: qsTr("登录系统时自动启动应用")
            EchoSwitch {
                objectName: "settingsAutoLaunch"
                checked: section.windowService.autoLaunch
                Accessible.name: qsTr("开机自启动")
                onToggled: section.windowService.autoLaunch = checked
            }
        }
        SettingsDivider {}
        SettingsRow {
            heading: qsTr("启动时最小化到托盘")
            description: qsTr("启动后隐藏主窗口，在托盘中运行")
            EchoSwitch {
                objectName: "settingsStartMinimized"
                checked: section.windowService.startMinimized
                enabled: section.windowService.trayAvailable
                Accessible.name: qsTr("启动时最小化到托盘")
                onToggled: section.windowService.startMinimized = checked
            }
        }
    }
    Label {
        Layout.fillWidth: true
        visible: section.windowService.systemError.length > 0
        text: section.windowService.systemError
        color: "#ef4444"
        font: Theme.font(12, Theme.regularWeight)
        wrapMode: Text.Wrap
    }
}
