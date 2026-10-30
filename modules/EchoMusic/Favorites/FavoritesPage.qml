pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Shell
import EchoMusic.Controls
import EchoMusic.Music
import EchoMusic.Theme
import EchoMusic.Library

// Five-tab favorites hub backed by the service route query. Songs use the
// shared catalog, singers and users use the followed-user grid, and albums
// and videos use shared cards. Liked songs load automatically in global playlist order.
RoutePage {
    id: page
    required property FavoritesService service
    objectName: "favoritesPage"
    readonly property int activeTab: service.tab
    readonly property bool songsTab: page.activeTab === 0
    readonly property bool singersTab: page.activeTab === 1
    readonly property bool usersTab: page.activeTab === 2
    readonly property bool albumsTab: page.activeTab === 3
    readonly property bool videosTab: page.activeTab === 4
    readonly property real expandedHeaderHeight: 176
    readonly property real toolbarRowHeight: 56
    readonly property real toolbarHeight: page.toolbarRowHeight + (page.songsTab ? 44 : 0)
    property string menuSongId: ""
    property real orderTargetListId: 0
    function showSongMenu(id: string, sceneX: real, sceneY: real): void {
        page.menuSongId = id;
        songMenuOwner.open({
            songId: id,
            x: sceneX,
            y: sceneY
        });
    }
    onPageActiveChanged: if (!pageActive) {
        batchOwner.close();
        songMenuOwner.close();
        orderDialogOwner.close();
    }
    function gridMargin(): real {
        return page.expandedHeaderHeight + page.toolbarRowHeight + 16;
    }
    EchoPageScrollContainer {
        id: viewport
        anchors.fill: parent
        visible: page.service.signedIn
        pageActive: page.pageActive
        scrollView: page.songsTab ? songList.listView : page.singersTab ? singersGrid : page.usersTab ? usersGrid : page.albumsTab ? albumsGrid : videosGrid
        coordinatedViews: [songList.listView, singersGrid, usersGrid, albumsGrid, videosGrid]
        resetViews: [songList.listView, singersGrid, usersGrid, albumsGrid, videosGrid]
        stickyHeaders: [header, toolbar]
        EchoPageStickyHeader {
            id: header
            container: viewport
            flowPosition: 0
            height: (heading.item as Item)?.implicitHeight ?? page.expandedHeaderHeight
            Loader {
                id: heading
                anchors.fill: parent
                sourceComponent: Component {
                    SongDetailHeader {
                        id: favoritesHeader
                        heading: qsTr("我最喜爱")
                        typeLabel: "FAVORITES"
                        subtitle: qsTr("集中管理收藏的歌曲、歌手、用户、专辑与视频。")
                        playable: page.service.songs.count > 0
                        showPlaybackActions: page.songsTab
                        resourceActive: page.pageActive
                        scrollTop: viewport.scrollTop
                        coverComponent: Component {
                            ThemedIconCover {
                                cornerRadius: Theme.mediaRadius / favoritesHeader.coverScale
                                iconPath: "M19.5 12.572L12 20l-7.5-7.428A5 5 0 1 1 12 6.006a5 5 0 1 1 7.5 6.572"
                            }
                        }
                        secondaryDetailsComponent: Component {
                            Flow {
                                objectName: "favoritesDetailMetadata"
                                spacing: 12
                                EchoButtonContent {
                                    objectName: "favoritesDetailCount"
                                    width: Math.min(parent.width, implicitWidth + 4)
                                    height: implicitHeight
                                    horizontalAlignment: Qt.AlignLeft
                                    glyph: "heart-filled"
                                    glyphSize: 12
                                    spacing: 6
                                    lineHeight: 16.5
                                    text: qsTr("%1 首歌曲").arg(page.service.songs.totalCount)
                                    font: Theme.font(11, Theme.semiboldWeight)
                                    color: Theme.secondaryText
                                }
                                Repeater {
                                    model: [page.service.singers.count > 0 ? qsTr("%1 位歌手").arg(page.service.singers.count) : "", page.service.albums.count > 0 ? qsTr("%1 张专辑").arg(page.service.albums.count) : "", page.service.videos.count > 0 ? qsTr("%1 个视频").arg(page.service.videos.count) : ""].filter(value => value.length > 0)
                                    delegate: EchoTextTagRow {
                                        required property string modelData
                                        objectName: "favoritesDetailMetadataField"
                                        width: Math.min(parent.width, implicitWidth + 4)
                                        height: implicitHeight
                                        text: modelData
                                        lineHeight: 16.5
                                        font: Theme.font(11, Theme.semiboldWeight)
                                        color: Theme.secondaryText
                                    }
                                }
                            }
                        }
                        onPlayRequested: page.service.actions.play()
                        onBatchRequested: batchOwner.open()
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
                objectName: "favoritesTabDivider"
                x: 24
                y: page.toolbarRowHeight - 1
                width: Math.max(0, parent.width - 48)
                height: 1
                color: Theme.subtleBorder
            }
            ColumnLayout {
                anchors.fill: parent
                spacing: 0
                RowLayout {
                    objectName: "favoritesTabToolbarRow"
                    Layout.fillWidth: true
                    Layout.preferredHeight: page.toolbarRowHeight
                    Layout.minimumHeight: page.toolbarRowHeight
                    Layout.maximumHeight: page.toolbarRowHeight
                    Layout.fillHeight: false
                    Layout.leftMargin: 24
                    Layout.rightMargin: 24
                    spacing: 8
                    EchoDetailTabs {
                        Layout.preferredHeight: 56
                        // 此页使用 14px/600；其它详情页沿用默认 15px/700。
                        labelPixelSize: 14
                        labelWeight: Theme.semiboldWeight
                        labels: [qsTr("歌曲"), qsTr("歌手"), qsTr("用户"), qsTr("专辑"), qsTr("视频")]
                        // Always show the song count; other badges appear once their models have rows.
                        counts: [String(page.service.songs.totalCount), page.service.singers.count > 0 ? String(page.service.singers.count) : "", page.service.users.count > 0 ? String(page.service.users.count) : "", page.service.albums.count > 0 ? String(page.service.albums.count) : "", page.service.videos.count > 0 ? String(page.service.videos.count) : ""]
                        objectNames: ["favoritesTabSongs", "favoritesTabSingers", "favoritesTabUsers", "favoritesTabAlbums", "favoritesTabVideos"]
                        currentIndex: page.activeTab
                        onPicked: index => page.service.selectTab(index)
                    }
                    Item {
                        Layout.fillWidth: true
                    }
                    SongSortButton {
                        objectName: "favoritesSongSort"
                        // 排序要求已登录、拥有喜欢歌单且云端 listid 为正数。
                        visible: page.songsTab && page.service.canAdjustSongOrder
                        onClicked: {
                            page.orderTargetListId = page.service.likedListId;
                            orderDialogOwner.open({
                                tracksListId: page.orderTargetListId,
                                tracksTitle: page.service.likedTitle
                            });
                        }
                    }
                    EchoTextField {
                        objectName: "favoritesSongFilter"
                        visible: page.songsTab
                        Layout.preferredWidth: 160
                        Layout.minimumWidth: 80
                        placeholderText: qsTr("搜索歌曲...")
                        text: page.service.songs.query
                        clearable: true
                        onTextEdited: page.service.songs.query = text
                    }
                    SongLocateButton {
                        objectName: "favoritesLocate"
                        visible: page.songsTab
                        onClicked: {
                            const index = page.service.songs.indexOf(page.service.actions.currentTrackId);
                            if (index >= 0)
                                EchoListPositioning.positionIfNeeded(songList.listView, index, ListView.Center, songList.pageContainer);
                        }
                    }
                    EchoTextField {
                        objectName: "favoritesSingerFilter"
                        visible: page.singersTab
                        Layout.preferredWidth: 160
                        Layout.minimumWidth: 80
                        placeholderText: qsTr("搜索歌手...")
                        clearable: true
                        onTextEdited: page.service.setSingerQuery(text)
                    }
                    EchoTextField {
                        objectName: "favoritesAlbumFilter"
                        visible: page.albumsTab
                        Layout.preferredWidth: 160
                        Layout.minimumWidth: 80
                        placeholderText: qsTr("搜索专辑...")
                        text: page.service.albums.query
                        clearable: true
                        onTextEdited: page.service.albums.query = text
                    }
                }
                SongListHeader {
                    visible: page.songsTab
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
        CatalogSongList {
            id: songList
            anchors.fill: parent
            visible: page.songsTab
            collection: page.service.songs
            actions: page.service.actions
            pageContainer: viewport
            contentInset: 24
            // Hidden song views retain their own column-header space.
            topMargin: page.expandedHeaderHeight + page.toolbarRowHeight + 44
            loading: page.service.loading && page.service.songs.totalCount === 0
            loadingMore: page.service.loading && page.service.songs.count > 0
            hasMore: page.service.hasMore
            errorMessage: page.service.errorMessage
            resourceActive: page.pageActive && visible
            menuSongId: page.menuSongId
            onLoadMoreRequested: page.service.loadMore()
            onRetryRequested: page.service.refresh()
            onMoreRequested: (id, sceneX, sceneY) => page.showSongMenu(id, sceneX, sceneY)
        }
        EchoEnter {
            target: songList
            active: page.pageActive && page.songsTab
        }
        EchoEnter {
            target: singersGrid
            active: page.pageActive && page.singersTab
        }
        EchoEnter {
            target: usersGrid
            active: page.pageActive && page.usersTab
        }
        EchoEnter {
            target: albumsGrid
            active: page.pageActive && page.albumsTab
        }
        EchoEnter {
            target: videosGrid
            active: page.pageActive && page.videosTab
        }
        FavoriteUserGrid {
            id: singersGrid
            objectName: "favoritesSingersGrid"
            visible: page.singersTab
            x: 24
            width: Math.max(0, parent.width - 48 + 20)
            height: parent.height
            topMargin: page.gridMargin()
            bottomMargin: 48
            pageActive: page.pageActive
            pageContainer: viewport
            collection: page.service.singers
            loading: page.service.loading && page.service.singers.count === 0
            singer: true
            emptyText: qsTr("暂无关注歌手")
            navigationAvailable: page.router.available("artist-detail")
            onOpenRequested: id => page.router.navigate("artist-detail", {}, id)
        }
        FavoriteUserGrid {
            id: usersGrid
            objectName: "favoritesUsersGrid"
            visible: page.usersTab
            x: 24
            width: Math.max(0, parent.width - 48 + 20)
            height: parent.height
            topMargin: page.gridMargin()
            bottomMargin: 48
            pageActive: page.pageActive
            pageContainer: viewport
            collection: page.service.users
            loading: page.service.loading && page.service.users.count === 0
            singer: false
            emptyText: qsTr("暂无关注用户")
        }
        ContentGrid {
            id: albumsGrid
            objectName: "favoritesAlbumsGrid"
            visible: page.albumsTab
            x: 24
            width: Math.max(0, parent.width - 48 + 20)
            height: parent.height
            topMargin: page.gridMargin()
            bottomMargin: 48
            pageActive: page.pageActive
            pageContainer: viewport
            sourceModel: page.service.albums
            navigationAvailable: page.router.available("album-detail")
            onOpenRequested: id => page.router.navigate("album-detail", {}, id)
            footer: Item {
                width: albumsGrid.width - 20
                height: albumsGrid.count === 0 && albumsGrid.visible && !albumsGrid.loading ? 320 : 0
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    y: 150
                    visible: albumsGrid.count === 0 && !albumsGrid.loading
                    text: qsTr("暂无收藏专辑")
                    textFormat: Text.PlainText
                    color: Theme.secondaryText
                    font: Theme.font(14, Theme.mediumWeight)
                }
            }
        }
        ContentGrid {
            id: videosGrid
            objectName: "favoritesVideosGrid"
            visible: page.videosTab
            x: 24
            width: Math.max(0, parent.width - 48 + 20)
            height: parent.height
            topMargin: page.gridMargin()
            bottomMargin: 48
            pageActive: page.pageActive
            pageContainer: viewport
            video: true
            sourceModel: page.service.videos
            loading: page.service.loading && page.service.videos.count === 0
            // The production MV route resolves this video ID through video_detail.
            hasMore: page.service.hasMore
            loadingMore: page.service.loading && page.service.videos.count > 0
            navigationAvailable: page.router.available("mv-detail")
            onLoadMoreRequested: page.service.loadMore()
            onOpenRequested: id => page.router.navigate("mv-detail", {}, id)
            footer: Item {
                id: videosFooter
                width: videosGrid.width - 20
                readonly property int rows: page.service.videos.count
                readonly property bool emptyState: videosFooter.rows === 0 && videosGrid.visible && !videosGrid.loading
                height: videosFooter.emptyState ? 320 : videosFooter.rows > 0 && !videosGrid.loading ? 64 : 0
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    y: 150
                    visible: videosFooter.emptyState
                    text: qsTr("暂无收藏视频")
                    textFormat: Text.PlainText
                    color: Theme.secondaryText
                    font: Theme.font(14, Theme.mediumWeight)
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    y: 24
                    // 有后续页时保留加载入口；加载中和耗尽时分别显示相应状态。
                    visible: videosFooter.rows > 0 && !videosGrid.loading
                    text: page.service.hasMore ? (videosGrid.loadingMore ? qsTr("加载更多中...") : qsTr("继续下滑加载更多")) : qsTr("没有更多了")
                    textFormat: Text.PlainText
                    color: Theme.secondaryText
                    font: Theme.font(12, Theme.regularWeight)
                }
            }
        }
    }
    // Signed-out state with a centered icon and title.
    Item {
        anchors.fill: parent
        visible: !page.service.signedIn
        Rectangle {
            anchors.centerIn: parent
            width: 72
            height: 72
            radius: Theme.dialogRadius
            color: Theme.withAlpha(Theme.accent, .1)
            EchoIcon {
                anchors.centerIn: parent
                width: 32
                height: 32
                name: "heart"
                color: Theme.accentText
            }
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.verticalCenter
            anchors.topMargin: 58
            text: qsTr("登录后查看我最喜爱")
            color: Theme.text
            font: Theme.font(22, Theme.semiboldWeight)
        }
    }
    EchoPopupLoader {
        id: batchOwner
        objectName: "favoritesBatchDrawerOwner"
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
        id: songMenuOwner
        objectName: "favoritesSongMenuOwner"
        hostItem: page
        sourceComponent: Component {
            SongContextMenu {
                id: songMenu
                parent: Overlay.overlay
                scale: page.settings.uiScale
                transformOrigin: Popup.TopLeft
                commands: page.service.actions
                onClosed: page.menuSongId = ""
            }
        }
    }
    // 保存成功后清空页面排序态和搜索词；服务负责重新加载歌曲。
    Connections {
        target: page.service.order
        enabled: orderDialogOwner.item !== null
        function onSaved(): void {
            // Only this page's open editor owns its presentation reset. Other
            // owned playlists and the sidebar share the same native model.
            if (page.service.order.mode !== 1 || page.service.order.listId !== page.orderTargetListId)
                return;
            page.service.songs.clearSort();
            page.service.songs.query = "";
        }
    }
    // 共享排序编辑器的 tracks 模式。
    EchoPopupLoader {
        id: orderDialogOwner
        objectName: "favoritesOrderDialogOwner"
        hostItem: page
        sourceComponent: Component {
            PlaylistOrderDialog {
                editor: page.service.order
                tracksMode: true
                uiScale: page.settings.uiScale
            }
        }
    }
}
