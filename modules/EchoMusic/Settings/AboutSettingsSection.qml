pragma ComponentBehavior: Bound
import QtQuick
import EchoMusic.Controls
import EchoMusic.Theme
import EchoMusic.Shell
import EchoMusic.Settings

SettingsSection {
    id: section
    required property ShellSettings settings
    required property WindowController windowService
    required property AboutService about
    heading: qsTr("关于")
    glyph: "info-circle"
    SettingsRow {
        heading: qsTr("自动检查更新")
        description: qsTr("启动时自动检查是否有新版本")
        EchoSwitch {
            checked: section.about.autoCheck
            colors: Theme.floating
            Accessible.name: qsTr("自动检查更新")
            onToggled: section.about.autoCheck = checked
        }
    }
    SettingsDivider {}
    SettingsRow {
        heading: qsTr("检查预发布版本")
        description: qsTr("开启后可收到 Alpha/Beta/RC 版本更新推送")
        EchoSwitch {
            checked: section.about.prerelease
            colors: Theme.floating
            Accessible.name: qsTr("检查预发布版本")
            onToggled: section.about.prerelease = checked
        }
    }
    SettingsDivider {}
    SettingsRow {
        heading: qsTr("静默安装")
        description: section.about.canInstall ? qsTr("Windows 更新安装时不弹出安装向导；Linux 仍需系统授权") : section.about.updateMessage
        EchoSwitch {
            enabled: section.about.canInstall
            checked: section.about.silentUpdate
            colors: Theme.floating
            Accessible.name: qsTr("静默安装")
            onToggled: section.about.silentUpdate = checked
        }
    }
    SettingsDivider {}
    SettingsRow {
        heading: qsTr("当前版本")
        description: "Version v" + section.about.version + (section.about.version.indexOf("-") >= 0 ? " Prerelease" : " Release")
        statusText: section.about.statusText
        controlGap: 8
        EchoSoftButton {
            objectName: "settingsChangelog"
            text: qsTr("更新日志")
            colors: Theme.floating
            onClicked: changelogOwner.open()
        }
        EchoSoftButton {
            objectName: "settingsCheckUpdates"
            text: section.about.checking ? qsTr("检查中...") : qsTr("检查更新")
            enabled: !section.about.checking
            accent: true
            colors: Theme.floating
            onClicked: section.about.check()
        }
    }
    SettingsDivider {}
    SettingsRow {
        heading: qsTr("项目源码")
        description: qsTr("开源共享于 GitHub")
        EchoIconButton {
            glyph: "external-link"
            text: qsTr("查看源码")
            colors: Theme.floating
            onClicked: section.about.openRepository()
        }
    }
    SettingsDivider {}
    SettingsRow {
        heading: qsTr("免责声明")
        description: qsTr("查看法律条款与免责声明")
        EchoIconButton {
            objectName: "settingsDisclaimer"
            glyph: "chevron-right"
            text: qsTr("查看声明")
            colors: Theme.floating
            onClicked: disclaimerOwner.open()
        }
    }
    EchoPopupLoader {
        id: changelogOwner
        objectName: "settingsChangelogDialogOwner"
        hostItem: section
        sourceComponent: Component {
            SettingsChangelogDialog {
                id: changelog
                preferredWidth: 520
                uiScale: section.settings.uiScale
                heading: qsTr("更新日志")
                markdown: section.about.changelog
            }
        }
    }
    EchoPopupLoader {
        id: disclaimerOwner
        objectName: "settingsDisclaimerDialogOwner"
        hostItem: section
        sourceComponent: Component {
            SettingsDisclaimerDialog {
                id: disclaimer
                uiScale: section.settings.uiScale
            }
        }
    }
    Component.onDestruction: {
        changelogOwner.close();
        disclaimerOwner.close();
    }
}
