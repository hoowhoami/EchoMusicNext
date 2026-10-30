pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Theme
import EchoMusic.Controls
import EchoMusic.PluginSdk
import EchoMusic.Plugins

EchoDialog {
    id: dialog
    objectName: "pluginUninstallDialog"
    required property PluginRuntime runtime
    required property string installationId
    property bool removeData: false
    onOpened: removeData = false
    heading: qsTr("卸载插件")
    description: qsTr("确定卸载 %1？插件目录会被删除。").arg(dialog.runtime.record(dialog.installationId).name || "")
    closable: !dialog.runtime.loading
    body: Component {
        RowLayout {
            spacing: 8
            EchoCheckBox {
                checked: dialog.removeData
                onToggled: dialog.removeData = checked
                objectName: "pluginRemoveData"
                Accessible.name: qsTr("同时删除插件私有数据")
                colors: Theme.floating
                enabled: !dialog.runtime.loading
            }
            Label {
                objectName: "pluginRemoveDataLabel"
                Layout.fillWidth: true
                text: qsTr("同时删除插件私有数据（用户授权目录中的文件会保留）")
                color: Theme.floatingText
                font: Theme.font(14, Theme.regularWeight)
                wrapMode: Text.Wrap
                MouseArea {
                    anchors.fill: parent
                    enabled: !dialog.runtime.loading
                    onClicked: dialog.removeData = !dialog.removeData
                }
            }
        }
    }
    footerContent: Component {
        RowLayout {
            spacing: 5
            Item {
                Layout.fillWidth: true
            }
            PluginActionButton {
                text: qsTr("取消")
                enabled: !dialog.runtime.loading
                onClicked: dialog.close()
            }
            PluginActionButton {
                danger: true
                text: qsTr("卸载")
                enabled: !dialog.runtime.loading
                onClicked: dialog.runtime.uninstall(dialog.installationId, dialog.removeData)
            }
        }
    }
}
