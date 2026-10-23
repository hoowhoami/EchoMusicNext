pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Controls
import EchoMusic.Theme
import EchoMusic.Settings

ColumnLayout {
    id: list
    required property SettingsDataService service
    required property string providerKey
    property real uiScale: 1
    property string selectedId: ""
    spacing: 8
    RowLayout {
        Layout.fillWidth: true
        Label {
            text: qsTr("可用备份")
            color: Theme.floatingText
            font: Theme.font(12, Theme.semiboldWeight)
        }
        Item {
            Layout.fillWidth: true
        }
        EchoSoftButton {
            objectName: "backupProviderRefresh"
            loading: list.service.providerBusy && !list.service.busy
            text: qsTr("刷新")
            colors: Theme.floating
            enabled: !list.service.providerBusy && !list.service.busy
            onClicked: list.service.refreshProvider(list.providerKey)
        }
        EchoSoftButton {
            text: qsTr("删除")
            danger: true
            enabled: list.selectedId !== "" && !list.service.providerBusy && !list.service.busy && list.service.providers.some(p => p.key === list.providerKey && p.removable)
            colors: Theme.floating
            onClicked: deleteOwner.open()
        }
    }
    ListView {
        id: entries
        objectName: "backupProviderEntries"
        Layout.fillWidth: true
        Layout.preferredHeight: Math.min(256, count * 64)
        clip: true
        reuseItems: true
        cacheBuffer: 64
        spacing: 8
        model: list.service.selectedProvider === list.providerKey ? list.service.providerEntries : []
        EchoWheelScroll.enabled: true
        ScrollBar.vertical: EchoScrollBar {
            wheelView: entries
        }
        delegate: ItemDelegate {
            id: entry
            required property var modelData
            width: ListView.view.width
            height: 56
            enabled: !list.service.providerBusy && !list.service.busy
            leftPadding: 16
            rightPadding: 16
            topPadding: 12
            bottomPadding: 12
            onClicked: list.selectedId = String(modelData.id)
            background: Rectangle {
                radius: 10
                color: entry.hovered || list.selectedId === String(entry.modelData.id) ? Theme.floating.selected : Theme.floating.mutedSurface
                border.width: 1
                border.color: list.selectedId === String(entry.modelData.id) ? Theme.accent : Theme.floating.controlBorder
            }
            contentItem: RowLayout {
                spacing: 12
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 3
                    Label {
                        Layout.fillWidth: true
                        text: String(entry.modelData.name || entry.modelData.id)
                        color: Theme.floatingText
                        font: Theme.font(12, Theme.semiboldWeight)
                        elide: Text.ElideRight
                    }
                    Label {
                        Layout.fillWidth: true
                        text: String(entry.modelData.description || "")
                        visible: text !== ""
                        color: Theme.floatingSecondaryText
                        font: Theme.font(10, Theme.regularWeight)
                        elide: Text.ElideRight
                    }
                }
                ColumnLayout {
                    Label {
                        text: String(entry.modelData.createdAt || "")
                        color: Theme.floatingSecondaryText
                        font: Theme.font(10, Theme.regularWeight)
                        visible: text !== ""
                    }
                    Label {
                        text: entry.modelData.size === undefined ? "" : (Number(entry.modelData.size) / 1048576).toFixed(1) + " MiB"
                        color: Theme.floatingSecondaryText
                        font: Theme.font(10, Theme.regularWeight)
                        visible: text !== ""
                    }
                }
            }
        }
    }
    Label {
        Layout.fillWidth: true
        visible: entries.count === 0
        text: list.service.providerBusy ? qsTr("正在读取备份列表…") : list.service.providerError || qsTr("没有可用备份")
        color: Theme.floatingSecondaryText
        font: Theme.font(10, Theme.regularWeight)
    }
    EchoPopupLoader {
        id: deleteOwner
        hostItem: list
        sourceComponent: Component {
            EchoDialog {
                id: confirm
                uiScale: list.uiScale
                heading: qsTr("删除备份？")
                description: qsTr("此操作将删除插件存储中的所选备份。")
                footerContent: RowLayout {
                    Item {
                        Layout.fillWidth: true
                    }
                    EchoSoftButton {
                        text: qsTr("取消")
                        colors: Theme.floating
                        onClicked: confirm.close()
                    }
                    EchoSoftButton {
                        text: qsTr("删除")
                        danger: true
                        colors: Theme.floating
                        onClicked: {
                            list.service.removeFromProvider(list.providerKey, list.selectedId);
                            list.selectedId = "";
                            confirm.close();
                        }
                    }
                }
            }
        }
    }
    Connections {
        target: list.service
        function onProviderEntriesChanged(): void {
            if (!list.service.providerEntries.some(e => String(e.id) === list.selectedId))
                list.selectedId = "";
        }
    }
}
