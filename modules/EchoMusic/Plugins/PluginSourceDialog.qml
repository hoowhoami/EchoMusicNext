pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Theme
import EchoMusic.Controls
import EchoMusic.Plugins

EchoDialog {
    id: dialog
    objectName: "pluginSourceDialog"
    required property PluginManager manager
    signal networkSettingsRequested
    heading: qsTr("插件源")
    description: qsTr("添加 GitHub 仓库地址后，EchoMusic 会读取 echo-plugins.json 索引并同步插件清单。")
    preferredWidth: 680
    maximumHeight: 720
    windowHeightMargin: 120
    flushBody: true
    readonly property bool compactLayout: parent && parent.width / uiScale <= 720
    EchoPopupLoader {
        id: failureOwner
        objectName: "pluginSourceNetworkErrorOwner"
        hostItem: dialog.contentItem
        sourceComponent: PluginNetworkErrorDialog {
            uiScale: dialog.uiScale
            onNetworkSettingsRequested: dialog.networkSettingsRequested()
        }
    }
    body: Component {
        ColumnLayout {
            spacing: 16
            GridLayout {
                id: add
                Connections {
                    target: dialog.manager
                    function onSourceAdded(success) {
                        if (success) {
                            url.clear();
                            name.clear();
                        }
                    }
                }
                Layout.fillWidth: true
                Layout.rightMargin: 24
                columns: dialog.compactLayout ? 1 : 3
                columnSpacing: 10
                rowSpacing: 10
                EchoTextField {
                    id: url
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    Layout.preferredWidth: 280
                    clearable: true
                    colors: Theme.floating
                    placeholderText: "https://github.com/owner/repo"
                    Accessible.name: qsTr("插件源仓库地址")
                }
                EchoTextField {
                    id: name
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    Layout.preferredWidth: 180
                    clearable: true
                    colors: Theme.floating
                    placeholderText: qsTr("显示名称，可选")
                    Accessible.name: placeholderText
                }
                PluginActionButton {
                    accent: true
                    loading: dialog.manager.catalogLoading
                    text: qsTr("添加")
                    glyph: "plus"
                    colors: Theme.floating
                    enabled: url.text.trim().length > 0 && !dialog.manager.catalogLoading && !dialog.manager.busy
                    onClicked: dialog.manager.addSource(url.text, name.text)
                }
            }
            EchoBoundedListView {
                id: sourceList
                maximumViewportHeight: 480
                objectName: "pluginSourceList"
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredHeight: implicitHeight
                model: dialog.manager.sources
                reuseItems: true
                cacheBuffer: 140
                clip: true
                spacing: 10
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: EchoPopupScrollBar {
                    surface: dialog
                    view: sourceList
                    resourceActive: dialog.visible
                }
                delegate: Item {
                    id: row
                    required property var pluginRecord
                    width: sourceList.width
                    height: content.implicitHeight + 28
                    Rectangle {
                        x: 0
                        width: parent.width - 24
                        height: parent.height
                        radius: Theme.itemRadius
                        color: Theme.floatingCard
                        border.color: Theme.floating.subtleBorder
                    }
                    GridLayout {
                        id: content
                        columns: dialog.compactLayout ? 1 : 2
                        columnSpacing: 16
                        rowSpacing: 16
                        x: 14
                        y: 14
                        width: parent.width - 52
                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            spacing: 4
                            EchoTextTagRow {
                                Layout.fillWidth: true
                                text: row.pluginRecord.name
                                font: Theme.font(14, Theme.extraBoldWeight)
                                color: Theme.floatingText
                                EchoTag {
                                    visible: !!row.pluginRecord.official
                                    text: qsTr("官方")
                                    height: 20
                                    font: Theme.font(11, Theme.extraBoldWeight)
                                    radius: Theme.controlRadius
                                    borderVisible: false
                                    colors: Theme.floating
                                    tone: EchoTag.Accent
                                }
                            }
                            Label {
                                Layout.fillWidth: true
                                text: row.pluginRecord.url
                                elide: Text.ElideRight
                                font: Theme.font(12, Theme.regularWeight)
                                color: Theme.floatingSecondaryText
                            }
                            Label {
                                text: qsTr("%1 个插件").arg(row.pluginRecord.pluginCount || 0) + (row.pluginRecord.lastFetchedAt ? " · " + new Date(row.pluginRecord.lastFetchedAt).toLocaleString(Qt.locale(), "yyyy/M/d HH:mm:ss") : "")
                                font: Theme.font(12, Theme.regularWeight)
                                color: Theme.floatingSecondaryText
                            }
                            Label {
                                Layout.fillWidth: true
                                visible: !!row.pluginRecord.lastError
                                text: row.pluginRecord.lastError || ""
                                font: Theme.font(12, Theme.semiboldWeight)
                                color: Theme.dark ? "#fbbf24" : "#b45309"
                                wrapMode: Text.Wrap
                            }
                            PluginActionButton {
                                objectName: "pluginSourceNetworkDetailsAction"
                                text: qsTr("错误详情")
                                colors: Theme.floating
                                visible: !!row.pluginRecord.lastError && !!row.pluginRecord.lastErrorDetail
                                onClicked: failureOwner.open({
                                    failure: Object.assign({
                                        sourceName: row.pluginRecord.name
                                    }, row.pluginRecord.lastErrorDetail)
                                })
                            }
                        }
                        RowLayout {
                            Layout.fillHeight: false
                            Layout.fillWidth: dialog.compactLayout
                            spacing: 6
                            EchoSwitch {
                                colors: Theme.floating
                                checked: !!row.pluginRecord.enabled
                                enabled: !dialog.manager.catalogLoading && !dialog.manager.busy
                                Accessible.name: qsTr("启用 %1").arg(row.pluginRecord.name)
                                onToggled: dialog.manager.patchSource(row.pluginRecord.id, row.pluginRecord.name, checked)
                            }
                            Item {
                                Layout.fillWidth: true
                                visible: dialog.compactLayout
                            }
                            EchoIconButton {
                                implicitWidth: 32
                                implicitHeight: 32
                                padding: 9
                                glyph: "trash"
                                colors: Theme.floating
                                text: qsTr("删除插件源")
                                enabled: !row.pluginRecord.official && !dialog.manager.catalogLoading && !dialog.manager.busy
                                onClicked: dialog.manager.removeSource(row.pluginRecord.id)
                            }
                        }
                    }
                }
            }
        }
    }
}
