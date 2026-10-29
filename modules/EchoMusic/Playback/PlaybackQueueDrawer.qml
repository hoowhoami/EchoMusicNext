pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Theme
import EchoMusic.Controls
import EchoMusic.Playback

Popup {
    id: drawer
    objectName: "playbackQueueDrawer"
    required property PlaybackService playback
    property real uiScale: 1
    property real playerBarHeight: 88
    readonly property PlaybackQueueCatalog catalog: playback.queueCatalog
    readonly property PlaybackQueueModel viewedQueue: catalog.viewModel
    readonly property bool narrow: parent && parent.width / uiScale <= 420
    readonly property real safeTop: Math.max(46, 35 / uiScale) + (narrow ? 4 : 16)
    readonly property real safeBottom: playerBarHeight + 8 + (narrow ? 8 : 12)
    signal addToPlaylistRequested
    function locateCurrent() {
        if (viewedQueue && viewedQueue.currentIndex >= 0)
            EchoListPositioning.positionIfNeeded(list, viewedQueue.currentIndex, ListView.Center, null);
    }
    parent: Overlay.overlay
    width: Math.max(0, Math.min(380, parent ? parent.width / uiScale - (narrow ? 16 : 24) : 380))
    height: Math.max(0, parent ? parent.height / uiScale - safeTop - safeBottom : 600)
    x: parent ? parent.width - (width + (narrow ? 8 : 12)) * uiScale : 0
    y: safeTop * uiScale
    scale: uiScale
    transformOrigin: Popup.TopLeft
    padding: 0
    modal: true
    focus: true
    dim: true
    Overlay.modal: Rectangle {
        color: Theme.withAlpha("black", .12)
    }
    onAboutToHide: queueReorder.cancel()
    onOpened: {
        catalog.resetPreview();
        locateCurrent();
    }
    EchoPopupLifecycle {
        surface: drawer
    }
    background: EchoFloatingSurface {
        radius: Theme.popoverRadius
        border.width: 1
        border.color: Theme.floating.surfaceOutline
        dialogShadow: true
    }
    contentItem: ColumnLayout {
        spacing: 0
        Item {
            objectName: "queueHeader"
            Layout.fillWidth: true
            // Two 28px rows, a 6px gap and 8px vertical insets. Search context
            // shares the title row, so changing queues never changes this height.
            implicitHeight: 78
            Layout.minimumHeight: implicitHeight
            Layout.maximumHeight: implicitHeight
            ColumnLayout {
                id: headerContent
                anchors.fill: parent
                anchors.margins: 16
                anchors.topMargin: 8
                anchors.bottomMargin: 8
                spacing: 6
                RowLayout {
                    Layout.fillHeight: false
                    Layout.fillWidth: true
                    spacing: 8
                    RowLayout {
                        id: headingText
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        spacing: 8
                        Label {
                            Layout.alignment: Qt.AlignBaseline
                            visible: text.length > 0
                            text: drawer.catalog.viewStatus
                            font: Theme.font(14, Theme.semiboldWeight)
                            color: Theme.floating.text
                        }
                        Label {
                            objectName: "queueTitle"
                            Layout.alignment: Qt.AlignBaseline
                            Layout.fillWidth: !subtitle.visible
                            Layout.minimumWidth: 0
                            text: drawer.catalog.viewTitle
                            font: Theme.font(16, Theme.boldWeight)
                            color: Theme.floating.text
                            textFormat: Text.PlainText
                            maximumLineCount: 1
                            wrapMode: Text.NoWrap
                            elide: Text.ElideRight
                        }
                        Label {
                            id: subtitle
                            objectName: "queueSubtitle"
                            Layout.alignment: Qt.AlignBaseline
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            visible: text.length > 0
                            text: drawer.catalog.viewSubtitle
                            font: Theme.font(11, Theme.regularWeight)
                            color: Theme.floating.secondaryText
                            textFormat: Text.PlainText
                            maximumLineCount: 1
                            wrapMode: Text.NoWrap
                            elide: Text.ElideRight
                        }
                    }
                    EchoIconButton {
                        objectName: "queueCloseButton"
                        colors: Theme.floating
                        glyph: "x"
                        text: qsTr("关闭")
                        tooltipText: ""
                        implicitWidth: 28
                        implicitHeight: 28
                        padding: 6
                        onClicked: drawer.close()
                    }
                }
                // Fixed count/navigation/actions columns; only the spacer stretches.
                RowLayout {
                    Layout.fillHeight: false
                    Layout.fillWidth: true
                    spacing: 10
                    Label {
                        Layout.preferredWidth: 54
                        text: qsTr("%1 首").arg(drawer.viewedQueue ? drawer.viewedQueue.count : 0)
                        font: Theme.font(11, Theme.regularWeight)
                        color: Theme.floating.secondaryText
                    }
                    RowLayout {
                        objectName: "queueSwitcherArrows"
                        Layout.fillWidth: false
                        Layout.fillHeight: false
                        visible: drawer.catalog.previewCount > 1
                        spacing: 12
                        Repeater {
                            model: [-1, 1]
                            delegate: EchoIconButton {
                                colors: Theme.floating
                                required property int modelData
                                glyph: modelData < 0 ? "arrow-left" : "arrow-right"
                                text: modelData < 0 ? qsTr("上一队列") : qsTr("下一队列")
                                implicitWidth: 24
                                implicitHeight: 28
                                padding: 4.5
                                enabled: modelData < 0 ? drawer.catalog.previewIndex > 0 : drawer.catalog.previewIndex < drawer.catalog.previewCount - 1
                                background: Item {}
                                onClicked: drawer.catalog.switchPreview(modelData)
                            }
                        }
                    }
                    Item {
                        Layout.fillWidth: true
                    }
                    RowLayout {
                        objectName: "queueHeaderActions"
                        // Keep actions at intrinsic width; the spacer owns free space.
                        Layout.fillWidth: false
                        Layout.fillHeight: false
                        spacing: 4
                        EchoIconButton {
                            objectName: "queueScrollTopButton"
                            colors: Theme.floating
                            glyph: "arrow-up"
                            text: qsTr("回到顶部")
                            implicitWidth: 28
                            implicitHeight: 28
                            padding: 6
                            onClicked: list.positionViewAtBeginning()
                        }
                        EchoIconButton {
                            objectName: "queueLocateButton"
                            colors: Theme.floating
                            glyph: "current-location"
                            text: qsTr("定位当前歌曲")
                            tooltipText: text
                            implicitWidth: 28
                            implicitHeight: 28
                            padding: 6
                            onClicked: drawer.locateCurrent()
                        }
                        EchoIconButton {
                            objectName: "queueAddButton"
                            colors: Theme.floating
                            glyph: "text-plus"
                            text: qsTr("添加到")
                            tooltipText: text
                            implicitWidth: 28
                            implicitHeight: 28
                            padding: 6
                            enabled: drawer.viewedQueue && drawer.viewedQueue.count > 0
                            onClicked: drawer.addToPlaylistRequested()
                        }
                        EchoIconButton {
                            objectName: "queueClearButton"
                            colors: Theme.floating
                            glyph: "trash"
                            text: drawer.catalog.viewActive ? qsTr("清空当前队列") : qsTr("删除历史队列")
                            tooltipText: text
                            implicitWidth: 28
                            implicitHeight: 28
                            padding: 6
                            enabled: drawer.viewedQueue && drawer.viewedQueue.count > 0
                            onClicked: drawer.catalog.removeViewed()
                        }
                    }
                }
            }
            Row {
                objectName: "queueProgressTrack"
                visible: drawer.catalog.previewCount > 1
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                spacing: 2
                Repeater {
                    model: drawer.catalog.previewCount
                    delegate: Rectangle {
                        required property int index
                        width: (parent.width - (Math.max(1, drawer.catalog.previewCount) - 1) * 2) / Math.max(1, drawer.catalog.previewCount)
                        height: 1
                        color: Theme.floating.hover
                    }
                }
            }
            Rectangle {
                objectName: "queueProgressThumb"
                visible: drawer.catalog.previewCount > 1 && drawer.catalog.previewIndex >= 0
                anchors.bottom: parent.bottom
                height: 2
                radius: Theme.microRadius
                width: drawer.catalog.previewCount ? (parent.width - (drawer.catalog.previewCount - 1) * 2) / drawer.catalog.previewCount : parent.width
                x: Math.max(0, drawer.catalog.previewIndex) * (width + 2)
                color: Theme.accent
                Behavior on x {
                    NumberAnimation {
                        duration: Theme.store.motionEnabled ? 300 : 0
                        easing.type: Easing.OutCubic
                    }
                }
            }
        }
        EchoSoftButton {
            colors: Theme.floating
            Layout.alignment: Qt.AlignHCenter
            Layout.maximumWidth: drawer.width * .7
            Layout.topMargin: 8
            Layout.bottomMargin: 8
            visible: !drawer.catalog.viewActive && drawer.viewedQueue && drawer.viewedQueue.count > 0
            implicitHeight: 28
            accent: true
            glyph: "play"
            text: qsTr("继续播放")
            onClicked: drawer.playback.resumeViewedQueue()
        }
        ListView {
            id: list
            objectName: "playbackQueueList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            // Match PlayerQueueVirtualList's horizontal list padding. The
            // popup scrollbar keeps its own surface-edge positioning.
            Layout.leftMargin: 10
            Layout.rightMargin: 10
            clip: true
            reuseItems: true
            EchoViewPoolGuard {
                targetView: list
            }
            interactive: !queueReorder.busy
            cacheBuffer: 56 * 10
            model: drawer.visible ? drawer.viewedQueue : null
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: EchoPopupScrollBar {
                surface: drawer
                view: list
                resourceActive: drawer.visible
            }
            Keys.onLeftPressed: drawer.catalog.switchPreview(-1)
            Keys.onRightPressed: drawer.catalog.switchPreview(1)
            delegate: EchoReorderRow {
                id: row
                objectName: "queueRow"
                reorder: queueReorder
                required property string resourceId
                required property string title
                required property string artistText
                required property string coverUrl
                required property bool isCurrent
                readonly property bool hovered: visualRow.hovered
                readonly property bool active: visualRow.active
                PlaybackQueueRow {
                    id: visualRow
                    objectName: "queueRowVisual"
                    index: row.index
                    title: row.title
                    artistText: row.artistText
                    coverUrl: row.coverUrl
                    isCurrent: row.isCurrent
                    viewActive: drawer.catalog.viewActive
                    playing: drawer.playback.playing
                    resourceActive: row.contentActive
                    actionsEnabled: !queueReorder.busy
                    onPlayRequested: index => drawer.playback.playViewedIndex(index)
                    onRemoveRequested: index => drawer.playback.removeViewedIndex(index)
                }
            }
            property Item reorderController: PlaybackQueueReorder {
                id: queueReorder
                parent: list
                view: list
                queue: drawer.viewedQueue
                viewActive: drawer.catalog.viewActive
                playing: drawer.playback.playing
                resourceActive: drawer.visible
            }
            Column {
                anchors.centerIn: parent
                spacing: 16
                visible: list.count === 0
                EchoIcon {
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: 36
                    height: 36
                    name: "player-pause"
                    color: Theme.floating.secondaryText
                }
                Label {
                    text: qsTr("当前暂无队列")
                    color: Theme.floating.secondaryText
                    font: Theme.font(14, Theme.regularWeight)
                }
            }
        }
    }
}
