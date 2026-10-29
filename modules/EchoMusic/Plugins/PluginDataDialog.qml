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
    property var pending: ({})
    heading: qsTr("插件数据")
    description: qsTr("管理插件配置和私有文件，用户授权目录中的文件会保留。")
    preferredWidth: 720
    maximumHeight: 640
    flushBody: true
    onAboutToHide: confirmation.close()
    body: Component {
        ColumnLayout {
            spacing: 12
            EchoTextLine {
                visible: list.count === 0
                text: qsTr("暂无插件数据")
                color: Theme.floatingSecondaryText
                font: Theme.font(13, Theme.regularWeight)
            }
            EchoBoundedListView {
                id: list
                objectName: "pluginDataList"
                Layout.fillWidth: true
                Layout.preferredHeight: implicitHeight
                maximumViewportHeight: 400
                model: dialog.runtime.dataEntries
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
                            enabled: !dialog.runtime.loading
                            onClicked: dialog.runtime.openDataDirectory(row.modelData.key)
                        }
                        EchoIconButton {
                            objectName: "pluginClearData-" + row.modelData.id
                            glyph: "trash"
                            text: row.modelData.retained ? qsTr("删除保留数据") : qsTr("清空插件数据")
                            colors: Theme.floating
                            hoverBackground: false
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
                        enabled: !dialog.runtime.loading
                        onClicked: if (dialog.runtime.clearPluginData(dialog.pending.key))
                            confirm.close()
                    }
                }
            }
        }
    }
}
