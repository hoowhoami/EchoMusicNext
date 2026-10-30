pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Controls
import EchoMusic.Theme
import EchoMusic.Plugins

EchoDialog {
    id: dialog
    objectName: "pluginDataDialog"
    required property PluginRuntime runtime
    // Empty selection is the separate archive of uninstalled/source-replaced data.
    property string installationId: ""
    readonly property var entries: runtime.dataEntries.filter(entry => installationId.length > 0 ? !entry.retained && entry.key === installationId : entry.retained)
    property var pending: ({})
    readonly property bool pendingCurrent: entries.some(entry => entry.key === pending.key)
    heading: installationId.length > 0 ? qsTr("%1 · 插件数据").arg(entries.length > 0 ? entries[0].name || entries[0].id : qsTr("当前插件")) : qsTr("保留的插件数据")
    description: installationId.length > 0 ? qsTr("管理当前插件的配置和私有文件，用户授权目录中的文件会保留。") : qsTr("管理卸载或更换来源后保留的配置和私有文件。")
    preferredWidth: 720
    maximumHeight: 640
    flushBody: true
    onAboutToHide: confirmation.close()
    onInstallationIdChanged: {
        if (confirmation)
            confirmation.close();
        pending = ({});
    }
    body: Component {
        ColumnLayout {
            spacing: 12
            EchoTextLine {
                visible: list.count === 0
                text: dialog.installationId.length > 0 ? qsTr("当前插件已不可用") : qsTr("暂无保留数据")
                color: Theme.floatingSecondaryText
                font: Theme.font(13, Theme.regularWeight)
            }
            EchoBoundedListView {
                id: list
                objectName: "pluginDataList"
                Layout.fillWidth: true
                Layout.preferredHeight: implicitHeight
                maximumViewportHeight: 400
                model: dialog.entries
                reuseItems: true
                spacing: 10
                clip: true
                ScrollBar.vertical: EchoPopupScrollBar {
                    surface: dialog
                    view: list
                    resourceActive: dialog.visible
                }
                delegate: Item {
                    id: row
                    required property var modelData
                    width: list.width
                    height: 76
                    RowLayout {
                        anchors.fill: parent
                        anchors.rightMargin: 24
                        spacing: 12
                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            spacing: 3
                            EchoTextLine {
                                Layout.fillWidth: true
                                text: (row.modelData.name || row.modelData.id) + (row.modelData.retained ? qsTr(" · 已卸载，数据保留") : "")
                                font: Theme.font(13, Theme.boldWeight)
                                color: Theme.floatingText
                            }
                            EchoTextLine {
                                Layout.fillWidth: true
                                text: row.modelData.source || ""
                                font: Theme.font(12, Theme.regularWeight)
                                color: Theme.floatingSecondaryText
                            }
                            EchoTextLine {
                                Layout.fillWidth: true
                                text: row.modelData.path
                                font: Theme.font(11, Theme.regularWeight)
                                color: Theme.floatingSecondaryText
                            }
                        }
                        EchoIconButton {
                            glyph: "folder-open"
                            text: qsTr("打开数据目录")
                            colors: Theme.floating
                            enabled: !dialog.runtime.loading
                            onClicked: dialog.runtime.openDataDirectory(row.modelData.key)
                        }
                        EchoIconButton {
                            objectName: "pluginClearData-" + row.modelData.id
                            glyph: "trash"
                            text: row.modelData.retained ? qsTr("删除保留数据") : qsTr("清空插件数据")
                            colors: Theme.floating
                            foreground: hovered ? Theme.floating.dangerText : Theme.floatingSecondaryText
                            enabled: !dialog.runtime.loading
                            onClicked: {
                                dialog.pending = row.modelData;
                                confirmation.open();
                            }
                        }
                    }
                }
            }
        }
    }
    EchoPopupLoader {
        id: confirmation
        hostItem: dialog.contentItem
        sourceComponent: EchoDialog {
            id: confirm
            objectName: "pluginClearDataConfirmation"
            heading: qsTr("清除插件数据")
            description: qsTr("确定清除 %1 的配置和私有文件？此操作无法撤销。").arg(dialog.pending.name || dialog.pending.id || "")
            footerContent: Component {
                RowLayout {
                    Item {
                        Layout.fillWidth: true
                    }
                    PluginActionButton {
                        text: qsTr("取消")
                        onClicked: confirm.close()
                    }
                    PluginActionButton {
                        objectName: "pluginConfirmClearData"
                        text: qsTr("清除")
                        danger: true
                        enabled: !dialog.runtime.loading && dialog.pendingCurrent
                        onClicked: if (dialog.pendingCurrent && dialog.runtime.clearPluginData(dialog.pending.key))
                            confirm.close()
                    }
                }
            }
        }
    }
}
