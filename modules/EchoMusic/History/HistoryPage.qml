pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Shell
import EchoMusic.Controls
import EchoMusic.Music
import EchoMusic.Theme

RoutePage {
    id: page
    required property HistoryService service
    readonly property string activeTab: context.query.tab === "stats" ? "stats" : "songs"
    readonly property real expandedHeaderHeight: 176
    readonly property real toolbarHeight: 56 + (page.activeTab === "songs" ? 44 : 0)
    readonly property var stats: service.stats
    // EchoMusic's media queries use the zoom-adjusted window, not the page
    // width remaining after the sidebar and content padding.
    readonly property real responsiveWidth: Window.window ? Window.window.width / settings.uiScale : width
    objectName: "historyPage"
    function selectTab(tab: string): void {
        const query = Object.assign({}, page.context.query);
        query.tab = tab;
        page.router.replaceQuery(query);
    }
    // Open at the pointer or keyboard-reported row edge in scene coordinates.
    // Keep the target row highlighted until the menu closes.
    property string menuSongId: ""
    function showSongMenu(id: string, sceneX: real, sceneY: real): void {
        page.menuSongId = id;
        songMenuOwner.open({
            songId: id,
            x: sceneX,
            y: sceneY
        });
    }
    onPageActiveChanged: {
        if (page.pageActive)
            page.service.refresh();
        else {
            page.service.statsShare.cancel();
            clearOwner.close();
            songMenuOwner.close();
        }
    }
    onActiveTabChanged: {
        songMenuOwner.close();
        page.service.statsShare.cancel();
    }
    EchoPageScrollContainer {
        id: viewport
        anchors.fill: parent
        pageActive: page.pageActive
        scrollView: page.activeTab === "stats" ? statsView : songList.listView
        stickyHeaders: [header, toolbar]
        CatalogSongList {
            id: songList
            anchors.fill: parent
            visible: page.activeTab === "songs"
            collection: page.service.songs
            actions: page.service.actions
            pageContainer: viewport
            contentInset: 24
            topMargin: page.expandedHeaderHeight + page.toolbarHeight
            loading: page.service.loading && !page.service.songs.totalCount
            errorMessage: page.service.errorMessage
            menuSongId: page.menuSongId
            resourceActive: page.pageActive && visible
            onRetryRequested: page.service.refresh()
            onMoreRequested: (id, sceneX, sceneY) => page.showSongMenu(id, sceneX, sceneY)
        }
        EchoEnter {
            target: songList
            active: page.pageActive && page.activeTab === "songs"
        }
        EchoEnter {
            target: statsView
            active: page.pageActive && page.activeTab === "stats"
        }
        ListView {
            id: statsView
            objectName: "historyStatsView"
            x: 24
            width: Math.max(0, parent.width - 48)
            height: parent.height
            visible: page.activeTab === "stats"
            // Content padding scrolls with the page body.
            topMargin: page.expandedHeaderHeight + page.toolbarHeight + 20
            bottomMargin: 48
            interactive: true
            boundsBehavior: Flickable.StopAtBounds
            model: 1
            ScrollBar.vertical: EchoPageScrollBar {
                view: statsView
                container: viewport
                viewActive: page.activeTab === "stats" && page.pageActive
            }
            delegate: Item {
                width: statsView.width
                height: statsColumn.implicitHeight + 20
                HistoryStatsContent {
                    id: statsColumn
                    width: parent.width
                    viewportWidth: page.responsiveWidth
                    stats: page.stats
                    resourceActive: page.pageActive && page.activeTab === "stats"
                }
            }
        }
        EchoPageStickyHeader {
            id: header
            container: viewport
            flowPosition: 0
            height: (heading.item as Item)?.implicitHeight ?? page.expandedHeaderHeight
            Loader {
                id: heading
                anchors.fill: parent
                sourceComponent: Component {
                    HistoryHeader {
                        service: page.service
                        scrollTop: viewport.scrollTop
                        resourceActive: page.pageActive
                        showPlaybackActions: page.activeTab === "songs"
                        showClear: page.activeTab === "songs"
                        onShareRequested: page.service.statsShare.share(page)
                        onPlayRequested: page.service.actions.play()
                        onBatchRequested: batchOwner.open()
                        onClearRequested: clearOwner.open()
                    }
                }
            }
        }
        EchoPageStickyHeader {
            id: toolbar
            container: viewport
            flowPosition: page.expandedHeaderHeight
            stickyTop: header.height
            height: page.toolbarHeight
            Rectangle {
                objectName: "historyTabDivider"
                x: 24
                y: 55
                width: Math.max(0, parent.width - 48)
                height: 1
                color: Theme.subtleBorder
            }
            ColumnLayout {
                anchors.fill: parent
                spacing: 0
                RowLayout {
                    objectName: "historyTabToolbarRow"
                    Layout.fillWidth: true
                    Layout.preferredHeight: 56
                    Layout.minimumHeight: 56
                    Layout.maximumHeight: 56
                    Layout.fillHeight: false
                    Layout.leftMargin: 24
                    Layout.rightMargin: 24
                    spacing: 8
                    EchoDetailTabs {
                        Layout.preferredHeight: 56
                        labels: [qsTr("歌曲"), qsTr("统计")]
                        counts: [page.service.songs.totalCount, ""]
                        objectNames: ["historyTabSongs", "historyTabStats"]
                        currentIndex: page.activeTab === "songs" ? 0 : 1
                        onPicked: index => page.selectTab(index === 0 ? "songs" : "stats")
                    }
                    Item {
                        Layout.fillWidth: true
                    }
                    EchoTextField {
                        objectName: "historySongFilter"
                        visible: page.activeTab === "songs"
                        Layout.preferredWidth: 160
                        Layout.minimumWidth: 80
                        placeholderText: qsTr("搜索歌曲...")
                        text: page.service.songs.query
                        clearable: true
                        onTextEdited: page.service.songs.query = text
                    }
                    SongLocateButton {
                        objectName: "historyLocate"
                        visible: page.activeTab === "songs"
                        onClicked: {
                            const index = page.service.songs.indexOf(page.service.actions.currentTrackId);
                            if (index >= 0)
                                EchoListPositioning.positionIfNeeded(songList.listView, index, ListView.Center, songList.pageContainer);
                        }
                    }
                    RowLayout {
                        objectName: "historyLatestPlayed"
                        visible: page.activeTab === "stats" && page.responsiveWidth >= 640
                        spacing: 6
                        EchoIcon {
                            Layout.preferredWidth: 14
                            Layout.preferredHeight: 14
                            name: "clock"
                            color: Theme.secondaryText
                        }
                        EchoTextTagRow {
                            text: qsTr("最近 %1").arg(page.stats.latestPlayedLabel || qsTr("未知"))
                            font: Theme.font(12, Theme.semiboldWeight)
                            color: Theme.secondaryText
                        }
                    }
                }
                SongListHeader {
                    visible: page.activeTab === "songs"
                    Layout.fillWidth: true
                    Layout.leftMargin: 24
                    Layout.rightMargin: 24
                    geometry: songList.geometry
                    sortField: page.service.songs.sortField
                    sortDirection: page.service.songs.sortDirection
                    onSortPicked: field => page.service.songs.toggleSort(field)
                }
            }
        }
    }
    EchoPopupLoader {
        id: batchOwner
        objectName: "historyBatchDrawerOwner"
        hostItem: page
        sourceComponent: Component {
            SongCatalogBatchDrawer {
                id: batch
                actions: page.service.actions
                collection: page.service.songs
                uiScale: page.settings.uiScale
            }
        }
    }
    EchoPopupLoader {
        id: clearOwner
        objectName: "historyClearDialogOwner"
        hostItem: page
        sourceComponent: Component {
            EchoDialog {
                id: clearDialog
                heading: qsTr("清空播放历史")
                description: qsTr("将删除当前账号的全部本地播放历史，此操作无法撤销。")
                preferredWidth: 440
                uiScale: page.settings.uiScale
                footerContent: Component {
                    RowLayout {
                        spacing: 8
                        Item {
                            Layout.fillWidth: true
                        }
                        EchoSoftButton {
                            objectName: "historyClearCancel"
                            text: qsTr("取消")
                            onClicked: clearOwner.close()
                        }
                        EchoSoftButton {
                            objectName: "historyClearConfirm"
                            text: qsTr("清空")
                            enabled: page.service.songs.count > 0
                            onClicked: {
                                page.service.clear();
                                clearOwner.close();
                            }
                        }
                    }
                }
            }
        }
    }
    EchoPopupLoader {
        id: songMenuOwner
        objectName: "historySongMenuOwner"
        hostItem: page
        sourceComponent: Component {
            SongContextMenu {
                id: songMenu
                parent: Overlay.overlay
                scale: page.settings.uiScale
                transformOrigin: Popup.TopLeft
                commands: page.service.actions
                removeVisible: true
                removeAfterExtensions: true
                removeLabel: qsTr("从播放历史中移除")
                onRemoveTriggered: page.service.remove(songMenu.songId)
                onClosed: page.menuSongId = ""
            }
        }
    }
}
