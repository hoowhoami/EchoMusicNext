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
    required property ArtistDetailService service
    readonly property ArtistDetailContext detail: service.contextFor(context)
    readonly property string activeTab: context.query.tab === "albums" ? "albums" : "songs"
    readonly property real expandedHeaderHeight: 176 + (detail.detail.description ? 28 : 0)
    readonly property real songToolbarRowHeight: 56
    readonly property real toolbarHeight: page.songToolbarRowHeight + (page.activeTab === "songs" ? 44 : 0)
    objectName: "artistDetailPage"
    function ensureTabData(): void {
        if (page.activeTab === "albums")
            page.detail.ensureAlbums();
    }
    function selectTab(tab: string): void {
        const query = Object.assign({}, page.context.query);
        query.tab = tab;
        page.router.replaceQuery(query);
    }
    // Open at the pointer or trailing action edge in scene coordinates.
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
        if (!page.pageActive) {
            batchOwner.close();
            introOwner.close();
            songMenuOwner.close();
        } else
            ensureTabData();
    }
    onActiveTabChanged: {
        songMenuOwner.close();
        ensureTabData();
    }
    Component.onCompleted: ensureTabData()
    EchoPageScrollContainer {
        id: viewport
        objectName: "artistDetailViewport"
        visible: !initialSkeleton.active
        anchors.fill: parent
        pageActive: page.pageActive
        scrollView: page.activeTab === "albums" ? albumGrid : songList.listView
        stickyHeaders: [header, toolbar]
        CatalogSongList {
            id: songList
            anchors.fill: parent
            visible: page.activeTab === "songs"
            collection: page.detail.songs
            actions: page.detail.actions
            pageContainer: viewport
            contentInset: 24
            topMargin: page.expandedHeaderHeight + page.toolbarHeight
            loading: page.detail.songsLoading && !page.detail.songs.totalCount
            errorMessage: page.detail.errorMessage
            menuSongId: page.menuSongId
            resourceActive: page.pageActive && visible
            onRetryRequested: page.detail.reload()
            onMoreRequested: (id, sceneX, sceneY) => page.showSongMenu(id, sceneX, sceneY)
        }
        GridView {
            id: albumGrid
            objectName: "artistAlbumGrid"
            // Combine outer and album-section padding; account for the trailing
            // virtual cell gap without adding another right inset.
            x: 32
            width: Math.max(0, parent.width - 64 + 20)
            height: parent.height
            visible: page.activeTab === "albums"
            topMargin: page.expandedHeaderHeight + page.toolbarHeight + 16
            bottomMargin: 48
            clip: true
            reuseItems: true
            cacheBuffer: cellHeight * 3
            boundsBehavior: Flickable.StopAtBounds
            model: page.detail.albums
            readonly property int columns: Math.max(1, Math.floor(width / (180 + 20)))
            readonly property real cardWidth: Math.max(1, width / columns - 20)
            readonly property real cardHeight: (cardWidth - 22) / 1 + 66
            cellWidth: cardWidth + 20
            cellHeight: cardHeight + 20
            ScrollBar.vertical: EchoPageScrollBar {
                view: albumGrid
                container: viewport
                viewActive: page.activeTab === "albums" && page.pageActive
            }
            header: Item {
                width: albumGrid.width - 20
                height: 52
                RowLayout {
                    anchors.fill: parent
                    anchors.bottomMargin: 16
                    spacing: 12
                    EchoSelect {
                        objectName: "artistAlbumSort"
                        model: [qsTr("最热"), qsTr("最新")]
                        currentIndex: page.detail.albumSort === "new" ? 1 : 0
                        implicitWidth: 108
                        onActivated: index => page.detail.setAlbumSort(index === 1 ? "new" : "hot")
                    }
                    Item {
                        Layout.fillWidth: true
                    }
                    EchoTextTagRow {
                        objectName: "artistAlbumTotal"
                        text: qsTr("共 %1 张").arg(Number(page.detail.detail.album_count || 0) || page.detail.albums.totalCount)
                        font: Theme.font(11, Theme.regularWeight)
                        color: Theme.secondaryText
                    }
                }
            }
            delegate: ContentCard {
                id: albumCard
                property bool pooled: false
                width: albumGrid.cardWidth
                height: albumGrid.cardHeight
                navigationAvailable: page.router.available("album-detail")
                resourceActive: page.pageActive && albumGrid.visible && !pooled
                GridView.onPooled: pooled = true
                GridView.onReused: pooled = false
                onOpenRequested: id => page.router.navigate("album-detail", {}, id)
            }
            footer: Item {
                objectName: "artistAlbumEmptyState"
                width: albumGrid.width
                visible: !page.detail.albumsLoading && page.detail.albums.count === 0
                height: visible ? 156 : 0
                ColumnLayout {
                    anchors.centerIn: parent
                    spacing: 8
                    Text {
                        text: page.detail.albumError.length > 0 ? page.detail.albumError : qsTr("暂无专辑")
                        textFormat: Text.PlainText
                        color: Theme.secondaryText
                        font: Theme.font(12, Theme.regularWeight)
                    }
                    EchoSoftButton {
                        Layout.alignment: Qt.AlignHCenter
                        visible: page.detail.albumError.length > 0
                        text: qsTr("重新加载")
                        onClicked: page.detail.ensureAlbums()
                    }
                }
            }
        }
        EchoEnter {
            target: songList
            active: page.pageActive && page.activeTab === "songs"
        }
        EchoEnter {
            target: albumGrid
            active: page.pageActive && page.activeTab === "albums"
        }
        GridView {
            id: albumLoading
            objectName: "artistAlbumLoading"
            x: albumGrid.x
            y: 0
            width: albumGrid.width
            height: albumGrid.height
            topMargin: albumGrid.topMargin
            visible: page.activeTab === "albums" && page.detail.albumsLoading && page.detail.albums.count === 0
            interactive: false
            model: visible ? 8 : 0
            readonly property int columns: albumGrid.columns
            readonly property real cardWidth: albumGrid.cardWidth
            readonly property real cardHeight: albumGrid.cardHeight
            cellWidth: cardWidth + 20
            cellHeight: cardHeight + 20
            delegate: ContentSkeleton {
                width: albumLoading.cardWidth
                height: albumLoading.cardHeight
                resourceActive: albumLoading.visible && page.pageActive
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
                    ArtistDetailHeader {
                        detailContext: page.detail
                        shareId: String(page.detail.detail.id || page.context.resourceId)
                        scrollTop: viewport.scrollTop
                        resourceActive: page.pageActive
                        showPlaybackActions: page.activeTab === "songs"
                        onPlayRequested: page.detail.actions.play()
                        onBatchRequested: batchOwner.open()
                        onIntroRequested: introOwner.open()
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
                objectName: "artistTabDivider"
                x: 24
                y: page.songToolbarRowHeight - 1
                width: Math.max(0, parent.width - 48)
                height: 1
                color: Theme.subtleBorder
            }
            ColumnLayout {
                anchors.fill: parent
                spacing: 0
                RowLayout {
                    objectName: "artistTabToolbarRow"
                    Layout.fillWidth: true
                    Layout.preferredHeight: page.songToolbarRowHeight
                    Layout.minimumHeight: page.songToolbarRowHeight
                    Layout.maximumHeight: page.songToolbarRowHeight
                    Layout.fillHeight: false
                    Layout.leftMargin: 24
                    Layout.rightMargin: 24
                    spacing: 8
                    EchoDetailTabs {
                        Layout.preferredHeight: 56
                        labels: [qsTr("歌曲"), qsTr("专辑")]
                        counts: [page.detail.songs.totalCount > 0 ? String(page.detail.songs.totalCount) : "", page.detail.albums.totalCount > 0 ? String(page.detail.albums.totalCount) : ""]
                        objectNames: ["artistTabSongs", "artistTabAlbums"]
                        currentIndex: page.activeTab === "songs" ? 0 : 1
                        onPicked: index => page.selectTab(index === 0 ? "songs" : "albums")
                    }
                    Item {
                        Layout.fillWidth: true
                    }
                    Loader {
                        visible: page.activeTab === "songs"
                        sourceComponent: Component {
                            EchoSelect {
                                objectName: "artistSongSort"
                                model: [qsTr("最新"), qsTr("最热")]
                                currentIndex: page.detail.sort === "hot" ? 1 : 0
                                implicitWidth: 108
                                onActivated: index => page.detail.setSort(index === 1 ? "hot" : "new")
                            }
                        }
                    }
                    EchoTextField {
                        objectName: "artistSongFilter"
                        visible: page.activeTab === "songs"
                        Layout.preferredWidth: 160
                        Layout.minimumWidth: 80
                        placeholderText: qsTr("搜索歌曲...")
                        text: page.detail.songs.query
                        clearable: true
                        onTextEdited: page.detail.songs.query = text
                    }
                    SongLocateButton {
                        objectName: "artistLocate"
                        visible: page.activeTab === "songs"
                        onClicked: {
                            const index = page.detail.songs.indexOf(page.detail.actions.currentTrackId);
                            if (index >= 0)
                                EchoListPositioning.positionIfNeeded(songList.listView, index, ListView.Center, songList.pageContainer);
                        }
                    }
                    EchoTextField {
                        objectName: "artistAlbumFilter"
                        visible: page.activeTab === "albums"
                        Layout.preferredWidth: 160
                        Layout.minimumWidth: 80
                        placeholderText: qsTr("搜索专辑...")
                        text: page.detail.albums.query
                        clearable: true
                        onTextEdited: page.detail.albums.query = text
                    }
                }
                SongListHeader {
                    visible: page.activeTab === "songs"
                    Layout.fillWidth: true
                    Layout.leftMargin: 24
                    Layout.rightMargin: 24
                    geometry: songList.geometry
                    sortField: page.detail.songs.sortField
                    sortDirection: page.detail.songs.sortDirection
                    onSortPicked: field => page.detail.songs.toggleSort(field)
                }
            }
        }
    }
    Loader {
        id: initialSkeleton
        anchors.fill: parent
        active: page.detail.detailLoading && !page.detail.detail.id
        sourceComponent: SongDetailSkeleton {
            typeLabel: "ARTIST"
            coverRound: true
            resourceActive: page.pageActive
        }
    }
    EchoPopupLoader {
        id: batchOwner
        objectName: "artistBatchDrawerOwner"
        hostItem: page
        sourceComponent: Component {
            SongCatalogBatchDrawer {
                id: batch
                actions: page.detail.actions
                collection: page.detail.songs
                uiScale: page.settings.uiScale
            }
        }
    }
    EchoPopupLoader {
        id: introOwner
        objectName: "artistIntroDialogOwner"
        hostItem: page
        sourceComponent: Component {
            EchoTextDialog {
                id: intro
                heading: qsTr("歌手介绍")
                text: page.detail.detail.description || ""
                preferredWidth: 520
                uiScale: page.settings.uiScale
            }
        }
    }
    EchoPopupLoader {
        id: songMenuOwner
        objectName: "artistSongMenuOwner"
        hostItem: page
        sourceComponent: Component {
            SongContextMenu {
                id: songMenu
                parent: Overlay.overlay
                scale: page.settings.uiScale
                transformOrigin: Popup.TopLeft
                commands: page.detail.actions
                onClosed: page.menuSongId = ""
            }
        }
    }
}
