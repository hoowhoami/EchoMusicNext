pragma ComponentBehavior: Bound
import QtQuick
import EchoMusic.Controls
import EchoMusic.Theme
import EchoMusic.Shell
import EchoMusic.Plugins

SettingsSection {
    id: section
    required property ShellSettings settings
    required property WindowController windowService
    property PluginManager pluginManager: null
    property RouteController router: null
    heading: qsTr("插件")
    glyph: "puzzle"
    SettingsRow {
        heading: qsTr("插件管理")
        description: qsTr("浏览在线插件源，管理已安装插件和插件设置") + (section.pluginManager && section.pluginManager.runtime.count > 0 ? qsTr("（%1/%2 个已启用）").arg(section.pluginManager.runtime.enabledCount).arg(section.pluginManager.runtime.count) : "")
        EchoSoftButton {
            text: qsTr("管理面板")
            colors: Theme.floating
            enabled: section.pluginManager !== null && section.router !== null
            onClicked: section.router.navigate("plugin-management")
        }
    }
    SettingsDivider {}
    SettingsRow {
        heading: qsTr("插件文档")
        description: qsTr("了解如何开发和使用插件")
        EchoSoftButton {
            text: qsTr("查看文档")
            colors: Theme.floating
            enabled: section.pluginManager !== null
            onClicked: section.pluginManager.openDocumentation()
        }
    }
}
