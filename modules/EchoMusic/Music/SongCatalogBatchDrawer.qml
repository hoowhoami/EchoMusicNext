pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Controls
import EchoMusic.Theme
import EchoMusic.Music

EchoDrawer {
    id: drawer
    objectName: "discoveryBatchDrawer"
    required property SongCatalogActions actions
    required property SongCatalogModel collection
    preferredWidth: 420
    playerBottomInset: 88
    /// Optional page-owned destructive action (Cloud: batch delete). The
    /// confirmation dialog is created on demand inside the drawer; the owning
    /// page drives removeBusy and closes the drawer when the service settles.
    property bool enableRemove: false
    property string removeLabel: qsTr("删除")
    property string removeDialogTitle: qsTr("删除云盘歌曲")
    property string removeDialogDescription: qsTr("确认从云盘删除选中的 %1 首歌曲？此操作无法撤销。").arg(collection.selectedCount)
    property string removeConfirmLabel: qsTr("确认删除")
    property bool removeBusy: false
    readonly property bool compactViewport: parent && parent.width / uiScale <= 420
    signal removeConfirmed(var ids)
    onAboutToShow: collection.clearSelection()
    contentItem: ColumnLayout {
        spacing: 0
        EchoDrawerHeader {
            Layout.fillWidth: true
            heading: qsTr("批量操作")
            headingFont: Theme.font(18, Theme.semiboldWeight)
            closeDisabled: drawer.removeBusy
            onCloseRequested: drawer.close()
            leadingContent: Component {
                Rectangle {
                    implicitWidth: 42
                    implicitHeight: 42
                    radius: Theme.cardRadius
                    color: Theme.withAlpha(Theme.accent, .1)
                    EchoIcon {
                        anchors.centerIn: parent
                        width: 22
                        height: 22
                        name: "list"
                        color: Theme.floating.accentText
                    }
                }
            }
        }
        Control {
            objectName: "batchSelection"
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            Layout.bottomMargin: 10
            padding: 8
            topPadding: 10
            bottomPadding: 12
            background: Item {
                Rectangle {
                    anchors.bottom: parent.bottom
                    width: parent.width
                    height: 1
                    color: Theme.floating.subtleBorder
                }
            }
            contentItem: RowLayout {
                spacing: 12
                EchoCheckBox {
                    objectName: "batchSelectAll"
                    colors: Theme.floating
                    enabled: drawer.collection.totalCount > 0 && !drawer.removeBusy
                    Accessible.name: qsTr("全选歌曲")
                    checkState: drawer.collection.selectedCount === 0 ? Qt.Unchecked : drawer.collection.selectedCount === drawer.collection.totalCount ? Qt.Checked : Qt.PartiallyChecked
                    onToggled: drawer.collection.selectAll(checkState !== Qt.Unchecked)
                }
                Text {
                    objectName: "batchSelectionCount"
                    text: drawer.collection.selectedCount + "/" + drawer.collection.totalCount
                    color: Theme.floatingSecondaryText
                    font: Theme.font(12, Theme.semiboldWeight)
                }
                Item {
                    Layout.fillWidth: true
                }
            }
        }
        ListView {
            id: songs
            objectName: "discoveryBatchSongs"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.bottomMargin: 8
            clip: true
            reuseItems: true
            EchoViewPoolGuard {
                targetView: songs
            }
            cacheBuffer: 140
            model: drawer.collection.totalCount
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: EchoPopupScrollBar {
                surface: drawer
                view: songs
                resourceActive: drawer.visible
            }
            delegate: Item {
                id: rowSlot
                required property int index
                readonly property var entry: {
                    return drawer.collection.count >= 0 && drawer.collection.selectedCount >= 0 ? drawer.collection.sourceItem(index) : ({});
                }
                property bool pooled: false
                width: songs.width
                height: 64
                ListView.onPooled: pooled = true
                ListView.onReused: pooled = false
                ItemDelegate {
                    id: row
                    objectName: "batchSongRow"
                    readonly property var entry: rowSlot.entry
                    anchors.fill: parent
                    anchors.leftMargin: 20
                    anchors.rightMargin: 20
                    leftPadding: 8
                    rightPadding: 8
                    enabled: !drawer.removeBusy
                    hoverEnabled: true
                    Accessible.name: qsTr("选择歌曲：%1").arg(entry.title || "")
                    onClicked: drawer.collection.toggleSelected(entry.resourceId)
                    background: Rectangle {
                        radius: Theme.itemRadius
                        color: row.entry.selected ? Theme.floating.selected : row.hovered ? Theme.floating.hover : "transparent"
                    }
                    contentItem: RowLayout {
                        spacing: 8
                        EchoCheckBox {
                            objectName: "batchSongCheck"
                            checked: row.entry.selected === true
                            colors: Theme.floating
                            Accessible.name: row.Accessible.name
                            onToggled: drawer.collection.toggleSelected(row.entry.resourceId)
                        }
                        EchoCover {
                            objectName: "batchSongCover"
                            Layout.preferredWidth: 42
                            Layout.preferredHeight: 42
                            source: row.entry.coverUrl || ""
                            colors: Theme.floating
                            resourceActive: drawer.visible && !rowSlot.pooled
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            spacing: 2
                            EchoTextTagRow {
                                Layout.fillWidth: true
                                Layout.minimumWidth: 0
                                text: row.entry.title || ""
                                font: Theme.font(13, Theme.semiboldWeight)
                                color: Theme.floatingText
                            }
                            EchoTextTagRow {
                                Layout.fillWidth: true
                                Layout.minimumWidth: 0
                                text: row.entry.artistText || ""
                                font: Theme.font(12, Theme.regularWeight)
                                color: Theme.floatingSecondaryText
                            }
                        }
                    }
                }
            }
            EchoTextTagRow {
                anchors.top: parent.top
                anchors.topMargin: 64
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.margins: 20
                visible: drawer.collection.totalCount === 0
                text: qsTr("暂无歌曲")
                font: Theme.font(13, Theme.semiboldWeight)
                color: Theme.floatingSecondaryText
                fillTextWidth: true
                horizontalAlignment: Text.AlignHCenter
            }
        }
        Control {
            objectName: "batchFooter"
            Layout.fillWidth: true
            leftPadding: drawer.compactViewport ? 14 : 20
            rightPadding: leftPadding
            topPadding: drawer.compactViewport ? 12 : 16
            bottomPadding: topPadding
            background: Item {
                Rectangle {
                    width: parent.width
                    height: 1
                    color: Theme.floating.subtleBorder
                }
            }
            contentItem: RowLayout {
                spacing: 8
                Flow {
                    objectName: "batchActions"
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    Layout.fillHeight: false
                    spacing: 8
                    EchoSoftButton {
                        objectName: "batchPlay"
                        colors: Theme.floating
                        accent: true
                        glyph: "play"
                        text: qsTr("播放")
                        enabled: drawer.collection.selectedCount > 0 && !drawer.removeBusy
                        onClicked: {
                            drawer.actions.play("", true, true);
                            drawer.close();
                        }
                    }
                    EchoSoftButton {
                        objectName: "batchAddTo"
                        colors: Theme.floating
                        glyph: "text-plus"
                        text: qsTr("添加到")
                        enabled: drawer.collection.selectedCount > 0 && !drawer.removeBusy
                        onClicked: drawer.actions.addSelection()
                    }
                }
                EchoSoftButton {
                    objectName: "batchRemove"
                    Layout.alignment: Qt.AlignTop
                    colors: Theme.floating
                    danger: true
                    glyph: "trash"
                    text: drawer.removeLabel
                    visible: drawer.enableRemove
                    loading: drawer.removeBusy
                    enabled: visible && drawer.collection.selectedCount > 0 && !drawer.removeBusy
                    onClicked: removeConfirmOwner.open()
                }
            }
        }
    }
    EchoPopupLoader {
        id: removeConfirmOwner
        objectName: "batchRemoveConfirmOwner"
        // Drawer is a Popup, not an Item; its contentItem is the first real
        // Item in the drawer's own scene layer.
        hostItem: drawer.contentItem
        sourceComponent: Component {
            EchoDialog {
                id: removeConfirm
                objectName: "batchRemoveConfirm"
                heading: drawer.removeDialogTitle
                description: drawer.removeDialogDescription
                preferredWidth: 420
                uiScale: drawer.uiScale
                closable: !drawer.removeBusy
                footerContent: Component {
                    RowLayout {
                        spacing: 8
                        Item {
                            Layout.fillWidth: true
                        }
                        EchoSoftButton {
                            text: qsTr("取消")
                            enabled: !drawer.removeBusy
                            onClicked: removeConfirmOwner.close()
                        }
                        EchoSoftButton {
                            objectName: "batchRemoveConfirmAction"
                            danger: true
                            text: drawer.removeConfirmLabel
                            loading: drawer.removeBusy
                            enabled: !drawer.removeBusy
                            onClicked: {
                                const ids = typeof drawer.collection.selectedIds === "function" ? drawer.collection.selectedIds() : [];
                                if (ids.length > 0)
                                    drawer.removeConfirmed(ids);
                            }
                        }
                    }
                }
            }
        }
    }
}
