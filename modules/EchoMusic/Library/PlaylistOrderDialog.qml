pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Controls
import EchoMusic.Theme
import EchoMusic.Library

EchoDialog {
    id: dialog
    objectName: "playlistOrderDialog"
    /// The shared order editor; the caller decides which target is loaded
    /// (sidebar playlist kinds, favorites tracks or owned playlist tracks).
    required property PlaylistOrderModel editor
    /// Sidebar playlist kind (0=自建 / 1=收藏) handed to editor.open().
    property int playlistTab: 0
    /// Account-owned track target: the caller supplies a local list ID and title;
    /// the model loads that list through its tracks entry.
    property bool tracksMode: false
    property real tracksListId: 0
    property string tracksTitle: ""
    // Body/footer are separately loaded components. Expose only their transient
    // controller for cancellation and button gating, never a second order model.
    property EchoListReorder reorderController: null
    readonly property bool reordering: reorderController !== null && reorderController.busy
    readonly property bool loading: editor.busy && !editor.saving && editor.count === 0
    preferredWidth: 540
    height: Math.min(480, parent ? Math.max(120, parent.height / uiScale - 96) : 480)
    maximumHeight: 480
    windowHeightMargin: 96
    heading: dialog.tracksMode ? qsTr("调整歌曲顺序") : editor.tab === 0 ? qsTr("调整自建歌单顺序") : qsTr("调整收藏歌单顺序")
    // 两种模式共用保存到酷狗的说明。
    description: qsTr("调整列表顺序并保存到酷狗")
    closable: !editor.saving
    closePolicy: editor.saving ? Popup.NoAutoClose : Popup.CloseOnEscape | Popup.CloseOnPressOutside
    function load(): void {
        if (dialog.reorderController)
            dialog.reorderController.cancel();
        if (dialog.tracksMode)
            dialog.editor.openTracks(dialog.tracksListId, dialog.tracksTitle);
        else
            dialog.editor.open(dialog.playlistTab);
    }
    onAboutToHide: if (dialog.reorderController)
        dialog.reorderController.cancel()
    onOpened: dialog.load()
    onClosed: {
        if (dialog.reorderController)
            dialog.reorderController.cancel();
        editor.close();
    }
    Connections {
        target: dialog.editor
        function onSaved() {
            dialog.close();
        }
        function onInvalidated() {
            dialog.close();
        }
    }
    body: ColumnLayout {
        spacing: 12
        Label {
            Layout.fillWidth: true
            visible: dialog.tracksMode
            text: dialog.tracksTitle
            wrapMode: Text.Wrap
            color: Theme.floatingText
            font: Theme.font(13, Theme.mediumWeight)
        }
        Label {
            Layout.fillWidth: true
            // 曲目模式仅显示首句；置顶规则说明只用于歌单模式。
            text: dialog.tracksMode ? qsTr("拖动排序，或选中后移动到指定位置。") : qsTr("拖动排序，或选中后移动到指定位置。默认收藏和我喜欢保持置顶。")
            wrapMode: Text.Wrap
            color: Theme.floatingSecondaryText
            font: Theme.font(12, Theme.regularWeight)
        }
        RowLayout {
            Layout.fillHeight: false
            spacing: 5
            visible: dialog.editor.errorMessage.length > 0
            Layout.fillWidth: true
            Label {
                Layout.fillWidth: true
                text: dialog.editor.errorMessage
                wrapMode: Text.Wrap
                color: Theme.floating.dangerText
                font: Theme.font(12, Theme.regularWeight)
            }
            EchoButton {
                text: qsTr("重新加载")
                size: EchoControlSize.Standard
                font: Theme.font(12, Theme.semiboldWeight)
                colors: Theme.floating
                accent: false
                enabled: !dialog.editor.busy && !dialog.reordering
                onClicked: dialog.load()
            }
        }
        EchoSkeleton {
            Layout.fillWidth: true
            implicitHeight: 44
            visible: dialog.loading
            animate: dialog.opened && dialog.loading
            color: Theme.floating.mutedSurface
        }
        Rectangle {
            visible: !dialog.loading
            Layout.fillWidth: true
            implicitHeight: tools.implicitHeight + 12
            color: Theme.floating.mutedSurface
            radius: Theme.cardRadius
            RowLayout {
                id: tools
                anchors.fill: parent
                anchors.margins: 6
                spacing: 6
                Label {
                    Layout.fillWidth: true
                    Layout.minimumWidth: implicitWidth
                    text: dialog.tracksMode ? qsTr("%1 首歌曲").arg(dialog.editor.count) : qsTr("%1 个歌单").arg(dialog.editor.count)
                    color: Theme.floatingSecondaryText
                    font: Theme.font(12, Theme.regularWeight)
                }
                GridLayout {
                    id: toolGrid
                    Layout.preferredWidth: 269
                    Layout.maximumWidth: 269
                    Layout.minimumWidth: 176
                    Layout.fillWidth: true
                    columns: width >= 269 ? 3 : 1
                    columnSpacing: 6
                    rowSpacing: 6
                    RowLayout {
                        Layout.fillHeight: false
                        Layout.alignment: Qt.AlignRight
                        spacing: 6
                        EchoIconButton {
                            glyph: "arrow-up"
                            text: qsTr("上移")
                            Layout.preferredWidth: 32
                            Layout.preferredHeight: 32
                            padding: 8
                            enabled: !dialog.editor.busy && !dialog.reordering && dialog.editor.selected > 0
                            onClicked: {
                                dialog.editor.move(dialog.editor.selected, dialog.editor.selected - 1);
                                list.positionViewAtIndex(dialog.editor.selected, ListView.Contain);
                            }
                        }
                        EchoIconButton {
                            glyph: "arrow-narrow-down"
                            text: qsTr("下移")
                            Layout.preferredWidth: 32
                            Layout.preferredHeight: 32
                            padding: 8
                            enabled: !dialog.editor.busy && !dialog.reordering && dialog.editor.selected >= 0 && dialog.editor.selected + 1 < dialog.editor.count
                            onClicked: {
                                dialog.editor.move(dialog.editor.selected, dialog.editor.selected + 1);
                                list.positionViewAtIndex(dialog.editor.selected, ListView.Contain);
                            }
                        }
                    }
                    Rectangle {
                        visible: toolGrid.columns === 3
                        Layout.preferredWidth: 11
                        Layout.preferredHeight: 16
                        color: "transparent"
                        Rectangle {
                            anchors.centerIn: parent
                            width: 1
                            height: 16
                            color: Theme.floating.controlBorder
                        }
                    }
                    RowLayout {
                        Layout.fillHeight: false
                        Layout.alignment: Qt.AlignRight
                        spacing: 6
                        Label {
                            text: qsTr("移到")
                            color: Theme.floatingSecondaryText
                            font: Theme.font(12, Theme.regularWeight)
                        }
                        EchoSpinBox {
                            id: position
                            objectName: "playlistOrderPosition"
                            from: 1
                            to: Math.max(1, dialog.editor.count)
                            suffix: qsTr("位")
                            size: EchoControlSize.Small
                            suffixFont: Theme.font(11, Theme.regularWeight)
                            onAccepted: {
                                dialog.editor.move(dialog.editor.selected, value - 1);
                                list.positionViewAtIndex(dialog.editor.selected, ListView.Contain);
                            }
                            colors: Theme.floating
                            value: 1
                            Layout.preferredWidth: 92
                            Layout.preferredHeight: 32
                            enabled: !dialog.editor.busy && !dialog.reordering && dialog.editor.count > 0
                            font: Theme.font(12, Theme.mediumWeight)
                            Accessible.name: qsTr("目标位置")
                        }
                        Connections {
                            target: dialog.editor
                            function onChanged() {
                                position.value = Math.max(1, dialog.editor.selected + 1);
                            }
                        }
                        EchoButton {
                            text: qsTr("移动")
                            size: EchoControlSize.Standard
                            font: Theme.font(12, Theme.semiboldWeight)
                            colors: Theme.floating
                            accent: false
                            enabled: position.enabled
                            onClicked: {
                                position.commit();
                                dialog.editor.move(dialog.editor.selected, position.value - 1);
                                list.positionViewAtIndex(dialog.editor.selected, ListView.Contain);
                            }
                        }
                    }
                }
            }
        }
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 60
            clip: true
            ListView {
                id: list
                objectName: "playlistOrderList"
                anchors.fill: parent
                visible: !dialog.loading
                model: dialog.editor
                reuseItems: true
                EchoViewPoolGuard {
                    targetView: list
                }
                interactive: !dialog.reordering
                cacheBuffer: 52 * 6
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: EchoPopupScrollBar {
                    surface: dialog
                    view: list
                    resourceActive: dialog.opened
                }
                property Item reorderController: EchoListReorder {
                    id: orderReorder
                    objectName: "playlistOrderReorder"
                    Component.onCompleted: dialog.reorderController = orderReorder
                    Component.onDestruction: dialog.reorderController = null
                    parent: list
                    view: list
                    sourceModel: dialog.editor
                    rowHeight: 52
                    blocked: dialog.editor.busy
                    resourceActive: dialog.visible && !dialog.loading
                    snapshotForRow: row => ({
                                title: row.title,
                                subtitle: row.subtitle
                            })
                    onStarted: index => dialog.editor.select(index)
                    onMoveRequested: (from, to) => dialog.editor.move(from, to)
                    dragContent: Component {
                        PlaylistOrderRow {
                            objectName: "playlistOrderGhost"
                            index: orderReorder.fromIndex
                            title: orderReorder.snapshot ? orderReorder.snapshot.title : ""
                            subtitle: orderReorder.snapshot ? orderReorder.snapshot.subtitle : ""
                            selected: true
                            lifted: true
                            interactive: false
                        }
                    }
                }
                delegate: EchoReorderRow {
                    id: row
                    objectName: "playlistOrderRow"
                    reorder: orderReorder
                    required property string title
                    required property string subtitle
                    readonly property bool hovered: visualRow.hovered
                    PlaylistOrderRow {
                        id: visualRow
                        objectName: "playlistOrderRowVisual"
                        index: row.index
                        title: row.title
                        subtitle: row.subtitle
                        selected: dialog.editor.selected === row.index
                        interactive: !dialog.editor.busy && !dialog.reordering
                        onSelectedRequested: index => dialog.editor.select(index)
                    }
                }
            }
            Column {
                anchors.fill: parent
                visible: dialog.loading
                Repeater {
                    model: 5
                    delegate: RowLayout {
                        id: loadingRow
                        required property int index
                        width: list.width
                        height: 52
                        spacing: 16
                        EchoSkeleton {
                            Layout.leftMargin: 14
                            implicitWidth: 24
                            implicitHeight: 12
                            animate: dialog.opened && dialog.loading
                            color: Theme.floating.mutedSurface
                        }
                        Column {
                            Layout.fillWidth: true
                            spacing: 8
                            EchoSkeleton {
                                width: parent.width * (loadingRow.index % 2 ? 0.76 : 0.58)
                                height: 12
                                animate: dialog.opened && dialog.loading
                                color: Theme.floating.mutedSurface
                            }
                            EchoSkeleton {
                                width: parent.width * 0.32
                                height: 9
                                animate: dialog.opened && dialog.loading
                                color: Theme.floating.mutedSurface
                            }
                        }
                    }
                }
            }
            Label {
                anchors.centerIn: parent
                visible: dialog.editor.count === 0 && !dialog.loading
                text: dialog.editor.busy ? qsTr("正在加载并排序…") : qsTr("没有可调整顺序的条目")
                color: Theme.floatingSecondaryText
                font: Theme.font(12, Theme.regularWeight)
            }
        }
    }
    footerContent: RowLayout {
        spacing: 8
        EchoButton {
            text: qsTr("还原顺序")
            size: EchoControlSize.Standard
            font: Theme.font(12, Theme.semiboldWeight)
            colors: Theme.floating
            accent: false
            enabled: !dialog.editor.busy && !dialog.reordering && dialog.editor.dirty
            onClicked: dialog.editor.restore()
        }
        Item {
            Layout.fillWidth: true
        }
        // 两种模式共用未保存修改提示。
        Label {
            visible: dialog.editor.dirty
            text: qsTr("有未保存的调整")
            color: Theme.floatingSecondaryText
            font: Theme.font(12, Theme.regularWeight)
        }
        EchoButton {
            text: qsTr("取消")
            size: EchoControlSize.Standard
            font: Theme.font(12, Theme.semiboldWeight)
            colors: Theme.floating
            accent: false
            enabled: !dialog.editor.saving
            onClicked: dialog.close()
        }
        EchoButton {
            text: dialog.editor.saving ? qsTr("正在保存…") : qsTr("保存顺序")
            size: EchoControlSize.Standard
            font: Theme.font(12, Theme.semiboldWeight)
            colors: Theme.floating
            enabled: !dialog.editor.busy && !dialog.reordering && dialog.editor.dirty
            onClicked: dialog.editor.save()
        }
    }
}
