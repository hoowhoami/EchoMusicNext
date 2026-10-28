pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Controls
import EchoMusic.Theme
import EchoMusic.Plugins

EchoDialog {
    id: dialog
    objectName: "pluginGrantsDialog"
    required property PluginManager manager
    heading: qsTr("插件授权")
    preferredWidth: 760
    maximumHeight: 680
    windowHeightMargin: 100
    flushBody: true
    onAboutToShow: manager.fileGrants.refresh()
    body: Component {
        ColumnLayout {
            spacing: 12
            EchoTextLine {
                Layout.fillWidth: true
                text: qsTr("宿主与系统资源")
                font: Theme.font(13, Theme.boldWeight)
                color: Theme.floatingText
            }
            EchoBoundedListView {
                id: resources
                objectName: "pluginResourcePermissions"
                Layout.fillWidth: true
                Layout.preferredHeight: implicitHeight
                maximumViewportHeight: 180
                model: dialog.manager.runtime.permissions.records
                spacing: 8
                clip: true
                reuseItems: true
                ScrollBar.vertical: EchoPopupScrollBar {
                    surface: dialog
                    view: resources
                    resourceActive: dialog.visible
                }
                delegate: Item {
                    id: resourceRow
                    required property var modelData
                    width: resources.width
                    height: 48
                    RowLayout {
                        anchors.fill: parent
                        anchors.rightMargin: 24
                        spacing: 12
                        EchoTextLine {
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            text: resourceRow.modelData.pluginName + " · " + resourceRow.modelData.name
                            font: Theme.font(13, Theme.regularWeight)
                            color: Theme.floatingText
                        }
                        EchoSoftButton {
                            objectName: "pluginResourceRevoke-" + resourceRow.modelData.id
                            text: qsTr("撤销")
                            colors: Theme.floating
                            onClicked: dialog.manager.runtime.permissions.revoke(resourceRow.modelData.id)
                        }
                    }
                }
            }
            EchoTextLine {
                Layout.fillWidth: true
                visible: resources.count === 0
                text: qsTr("暂无宿主与系统资源授权")
                font: Theme.font(13, Theme.regularWeight)
                color: Theme.floatingSecondaryText
            }
            EchoTextLine {
                Layout.fillWidth: true
                text: qsTr("文件与目录")
                font: Theme.font(13, Theme.boldWeight)
                color: Theme.floatingText
            }
            EchoTextLine {
                Layout.fillWidth: true
                Layout.rightMargin: 24
                visible: dialog.manager.fileGrants.loading || dialog.manager.fileGrants.errorMessage.length > 0
                text: dialog.manager.fileGrants.loading ? qsTr("正在读取授权记录…") : dialog.manager.fileGrants.errorMessage
                color: Theme.floatingSecondaryText
                font: Theme.font(13, Theme.regularWeight)
                wrap: true
            }
            EchoTextLine {
                Layout.fillWidth: true
                Layout.rightMargin: 24
                visible: list.count === 0 && !dialog.manager.fileGrants.loading
                text: qsTr("暂无已授权文件与目录")
                color: Theme.floatingSecondaryText
                font: Theme.font(13, Theme.regularWeight)
            }
            EchoBoundedListView {
                id: list
                objectName: "pluginFileGrants"
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredHeight: implicitHeight
                maximumViewportHeight: 300
                model: dialog.manager.fileGrants
                reuseItems: true
                cacheBuffer: 120
                clip: true
                spacing: 10
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: EchoPopupScrollBar {
                    surface: dialog
                    view: list
                    resourceActive: dialog.visible
                }
                delegate: Item {
                    id: row
                    required property var grant
                    width: list.width
                    height: content.implicitHeight + 28
                    objectName: "pluginGrant-" + grant.id
                    Rectangle {
                        width: parent.width - 24
                        height: parent.height
                        radius: Theme.itemRadius
                        color: Theme.floatingCard
                        border.color: Theme.floating.subtleBorder
                    }
                    ColumnLayout {
                        id: content
                        x: 14
                        y: 14
                        width: parent.width - 52
                        spacing: 8
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10
                            EchoTextLine {
                                Layout.fillWidth: true
                                Layout.minimumWidth: 0
                                text: row.grant.pluginName + " · " + row.grant.name
                                font: Theme.font(13, Theme.boldWeight)
                                color: Theme.floatingText
                            }
                            EchoTag {
                                text: row.grant.revoked ? qsTr("已撤销") : !row.grant.available ? qsTr("不可用") : row.grant.access === "read-write" ? qsTr("读写") : qsTr("只读")
                                colors: Theme.floating
                            }
                        }
                        EchoTextLine {
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            text: row.grant.displayPath
                            font: Theme.font(12, Theme.regularWeight)
                            color: Theme.floatingSecondaryText
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            EchoSoftButton {
                                objectName: "pluginGrantReauthorize-" + row.grant.id
                                text: qsTr("重新授权")
                                colors: Theme.floating
                                onClicked: dialog.manager.fileGrants.reauthorize(row.grant.id)
                            }
                            EchoSoftButton {
                                objectName: "pluginGrantRevoke-" + row.grant.id
                                text: qsTr("撤销")
                                colors: Theme.floating
                                enabled: !row.grant.revoked
                                onClicked: dialog.manager.fileGrants.revoke(row.grant.id)
                            }
                            Item {
                                Layout.fillWidth: true
                            }
                            EchoSoftButton {
                                objectName: "pluginGrantRemove-" + row.grant.id
                                text: qsTr("移除记录")
                                colors: Theme.floating
                                onClicked: dialog.manager.fileGrants.remove(row.grant.id)
                            }
                        }
                    }
                }
            }
        }
    }
}
