pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Controls
import EchoMusic.Theme
import EchoMusic.Settings

EchoDialog {
    id: dialog
    required property SettingsDataService service
    property bool restoreSettings: !!service.confirmation.settings && !!service.confirmation.summary?.settings
    property bool restorePlugins: !!service.confirmation.plugins && !!service.confirmation.summary?.plugins
    objectName: "pluginBackupRestoreConfirmation"
    preferredWidth: 520
    flushBody: true
    windowHeightMargin: 80
    heading: qsTr("从备份恢复")
    description: qsTr("恢复会覆盖所选项目的同名设置，并在完成后自动重启 EchoMusicNext。目标设备已有的其他插件不会被删除。")
    onClosed: service.answerConfirmation(false)
    body: SettingsScrollBody {
        surface: dialog
        Label {
            Layout.fillWidth: true
            text: qsTr("备份版本：EchoMusicNext %1\n创建时间：%2").arg(dialog.service.confirmation.summary?.appVersion || "").arg(dialog.service.confirmation.summary?.createdAt || "")
            color: Theme.floatingSecondaryText
            font: Theme.font(12, Theme.regularWeight)
            wrapMode: Text.Wrap
        }
        SettingsRow {
            heading: qsTr("应用设置")
            description: qsTr("%1 项可恢复设置").arg(dialog.service.confirmation.summary?.settingCount || 0)
            EchoSwitch {
                checked: dialog.restoreSettings
                enabled: !!dialog.service.confirmation.settings && !!dialog.service.confirmation.summary?.settings
                colors: Theme.floating
                onToggled: dialog.restoreSettings = checked
            }
        }
        SettingsRow {
            heading: qsTr("插件与插件数据")
            description: qsTr("%1 个插件").arg(dialog.service.confirmation.summary?.pluginCount || 0)
            EchoSwitch {
                checked: dialog.restorePlugins
                enabled: !!dialog.service.confirmation.plugins && !!dialog.service.confirmation.summary?.plugins
                colors: Theme.floating
                onToggled: dialog.restorePlugins = checked
            }
        }
    }
    footerContent: RowLayout {
        Item {
            Layout.fillWidth: true
        }
        EchoSoftButton {
            text: qsTr("取消")
            colors: Theme.floating
            onClicked: dialog.close()
        }
        EchoSoftButton {
            objectName: "pluginBackupConfirmRestore"
            text: qsTr("恢复并重启")
            accent: true
            enabled: dialog.restoreSettings || dialog.restorePlugins
            colors: Theme.floating
            onClicked: dialog.service.answerConfirmation(true, dialog.restoreSettings, dialog.restorePlugins)
        }
    }
}
