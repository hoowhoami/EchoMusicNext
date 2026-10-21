pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Shell
import EchoMusic.Controls
import EchoMusic.Music
import EchoMusic.Theme

// Purchased songs and albums share underlined count tabs.
// Songs use the shared list; albums use a virtualized grid.
RoutePage {
    id: page
    required property PurchasedService service
    objectName: "purchasedPage"
    readonly property int activeTab: service.tab
    readonly property bool songsTab: page.activeTab === 0
    readonly property real expandedHeaderHeight: 176
    readonly property real toolbarRowHeight: 56
    readonly property real toolbarHeight: page.toolbarRowHeight + (page.songsTab ? 44 : 0)
    property string menuSongId: ""
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
    }
    EchoPageScrollContainer {
        id: viewport
        anchors.fill: parent
        visible: page.service.signedIn
        pageActive: page.pageActive
        scrollView: page.songsTab ? songList.listView : albumsGrid
        coordinatedViews: [songList.listView, albumsGrid]
        resetViews: [songList.listView, albumsGrid]
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
                        id: purchasedHeader
                        heading: qsTr("已购音乐")
                        typeLabel: "PURCHASED"
                        subtitle: qsTr("浏览您已购买的单曲和专辑，随时畅听。")
                        playable: page.service.songs.count > 0
                        showPlaybackActions: page.songsTab
                        resourceActive: page.pageActive
                        scrollTop: viewport.scrollTop
                        coverComponent: Component {
                            ThemedIconCover {
                                cornerRadius: Theme.mediaRadius / purchasedHeader.coverScale
                                iconPath: "M6.331 8H17.67a2 2 0 0 1 1.977 2.304l-1.255 8.152A3 3 0 0 1 15.426 21H8.574a3 3 0 0 1-2.965-2.544l-1.255-8.152A2 2 0 0 1 6.331 8M9 11V6a3 3 0 0 1 6 0v5"
                            }
                        }
                        secondaryDetailsComponent: Component {
                            Flow {
                                objectName: "purchasedDetailMetadata"
                                spacing: 12
                                EchoButtonContent {
                                    objectName: "purchasedDetailCount"
                                    width: Math.min(parent.width, implicitWidth + 4)
                                    height: implicitHeight
                                    horizontalAlignment: Qt.AlignLeft
                                    glyph: "shopping-bag"
                                    glyphSize: 12
                                    spacing: 6
                                    lineHeight: 16.5
                                    text: qsTr("%1 首歌曲").arg(page.service.songsTotal)
                                    font: Theme.font(11, Theme.semiboldWeight)
                                    color: Theme.secondaryText
                                }
                                Repeater {
                                    model: page.service.albumsTotal > 0 ? [qsTr("%1 张专辑").arg(page.service.albumsTotal)] : []
                                    delegate: EchoTextTagRow {
                                        required property string modelData
                                        objectName: "purchasedDetailMetadataField"
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
                objectName: "purchasedTabDivider"
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
                    objectName: "purchasedTabToolbarRow"
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
                        labels: [qsTr("单曲"), qsTr("专辑")]
                        // Always show both count badges.
                        counts: [String(page.service.songsTotal), String(page.service.albumsTotal)]
                        objectNames: ["purchasedTabSongs", "purchasedTabAlbums"]
                        currentIndex: page.activeTab
                        onPicked: index => page.service.selectTab(index)
                    }
                    Item {
                        Layout.fillWidth: true
                    }
                    EchoTextField {
                        objectName: "purchasedSongFilter"
                        visible: page.songsTab
                        Layout.preferredWidth: 160
                        Layout.minimumWidth: 80
                        placeholderText: qsTr("搜索歌曲...")
                        text: page.service.songs.query
                        clearable: true
                        onTextEdited: page.service.songs.query = text
                    }
                    EchoTextField {
                        objectName: "purchasedAlbumFilter"
                        visible: !page.songsTab
                        Layout.preferredWidth: 160
                        Layout.minimumWidth: 80
                        placeholderText: qsTr("搜索专辑...")
                        text: page.service.albums.query
                        clearable: true
                        onTextEdited: page.service.albums.query = text
                    }
                    SongLocateButton {
                        objectName: "purchasedLocate"
                        visible: page.songsTab
                        onClicked: {
                            const index = page.service.songs.indexOf(page.service.actions.currentTrackId);
                            if (index >= 0)
                                EchoListPositioning.positionIfNeeded(songList.listView, index, ListView.Center, songList.pageContainer);
                        }
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
            topMargin: page.expandedHeaderHeight + page.toolbarHeight
            loading: page.service.loading && page.service.songs.count === 0
            loadingMore: page.service.loading && page.service.songs.count > 0
            hasMore: page.service.hasMore
            errorMessage: page.service.errorMessage
            resourceActive: page.pageActive && visible
            menuSongId: page.menuSongId
            emptyComponent: purchasedSongEmpty
            loadMoreComponent: purchasedLoadMore
            onLoadMoreRequested: page.service.loadMore()
            onRetryRequested: page.service.refresh()
            onMoreRequested: (id, sceneX, sceneY) => page.showSongMenu(id, sceneX, sceneY)
        }
        ContentGrid {
            id: albumsGrid
            objectName: "purchasedAlbumsGrid"
            visible: !page.songsTab
            x: 24
            width: Math.max(0, parent.width - 48 + 20)
            height: parent.height
            topMargin: page.expandedHeaderHeight + page.toolbarRowHeight + 16
            bottomMargin: 48
            pageActive: page.pageActive
            pageContainer: viewport
            sourceModel: page.service.albums
            loading: page.service.loading && page.service.albums.count === 0
            navigationAvailable: page.router.available("album-detail")
            onOpenRequested: id => page.router.navigate("album-detail", {}, id)
            footer: Item {
                id: albumsFooter
                width: albumsGrid.width - 20
                readonly property int rows: page.service.albums.count
                // 空状态和加载更多入口独立显示。
                readonly property bool emptyState: albumsFooter.rows === 0 && albumsGrid.visible && !albumsGrid.loading
                readonly property bool showMore: page.service.hasMore && !albumsGrid.loading && albumsGrid.visible
                height: albumsFooter.emptyState ? (albumsFooter.showMore ? 384 : 320) : albumsFooter.rows > 0 && albumsFooter.showMore ? 64 : 0
                ColumnLayout {
                    visible: albumsFooter.emptyState
                    anchors.horizontalCenter: parent.horizontalCenter
                    y: 56
                    spacing: 0
                    Rectangle {
                        Layout.alignment: Qt.AlignHCenter
                        implicitWidth: 64
                        implicitHeight: 64
                        radius: Theme.cardRadius
                        color: Theme.withAlpha(Theme.accent, .1)
                        EchoIcon {
                            anchors.centerIn: parent
                            width: 28
                            height: 28
                            name: "shopping-bag"
                            color: Theme.accentText
                        }
                    }
                    Text {
                        Layout.alignment: Qt.AlignHCenter
                        Layout.topMargin: 16
                        text: qsTr("暂无已购专辑")
                        color: Theme.text
                        font: Theme.font(18, Theme.semiboldWeight)
                    }
                    Text {
                        Layout.alignment: Qt.AlignHCenter
                        Layout.topMargin: 8
                        text: qsTr("购买的专辑会展示在这里")
                        color: Theme.secondaryText
                        font: Theme.font(13, Theme.mediumWeight)
                    }
                }
                EchoSoftButton {
                    objectName: "purchasedAlbumsLoadMore"
                    anchors.horizontalCenter: parent.horizontalCenter
                    y: albumsFooter.emptyState ? 336 : 24
                    visible: albumsFooter.showMore
                    text: page.service.loading ? qsTr("加载中...") : qsTr("加载更多")
                    enabled: !page.service.loading
                    onClicked: page.service.loadMore()
                }
            }
        }
        EchoEnter {
            target: songList
            active: page.pageActive && page.songsTab
        }
        EchoEnter {
            target: albumsGrid
            active: page.pageActive && !page.songsTab
        }
    }
    Component {
        id: purchasedSongEmpty
        Item {
            objectName: "purchasedSongsEmptyState"
            implicitHeight: 320
            ColumnLayout {
                anchors.centerIn: parent
                width: Math.min(420, Math.max(0, parent.width - 48))
                spacing: 0
                Rectangle {
                    Layout.alignment: Qt.AlignHCenter
                    implicitWidth: 64
                    implicitHeight: 64
                    radius: Theme.cardRadius
                    color: Theme.withAlpha(Theme.accent, .1)
                    EchoIcon {
                        anchors.centerIn: parent
                        width: 28
                        height: 28
                        name: "shopping-bag"
                        color: Theme.accentText
                    }
                }
                Text {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.topMargin: 16
                    text: qsTr("暂无已购单曲")
                    color: Theme.text
                    font: Theme.font(18, Theme.semiboldWeight)
                }
                Text {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.topMargin: 8
                    text: qsTr("购买的单曲会展示在这里")
                    color: Theme.secondaryText
                    font: Theme.font(13, Theme.mediumWeight)
                }
                // 空状态和加载更多入口独立：容器 total>0 而行尚未映射时可以同时出现。
                EchoSoftButton {
                    objectName: "purchasedSongsLoadMoreEmpty"
                    Layout.alignment: Qt.AlignHCenter
                    Layout.topMargin: 24
                    visible: page.service.hasMore
                    enabled: !page.service.loading
                    text: page.service.loading ? qsTr("加载中...") : qsTr("加载更多")
                    onClicked: page.service.loadMore()
                }
            }
        }
    }
    Component {
        id: purchasedLoadMore
        Item {
            implicitHeight: page.service.hasMore || page.service.loading ? 64 : 0
            EchoSoftButton {
                objectName: "purchasedSongsLoadMore"
                anchors.horizontalCenter: parent.horizontalCenter
                y: 16
                visible: page.service.hasMore
                text: page.service.loading ? qsTr("加载中...") : qsTr("加载更多")
                enabled: !page.service.loading
                onClicked: page.service.loadMore()
            }
        }
    }
    // Signed-out state.
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
                name: "shopping-bag"
                color: Theme.accentText
            }
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.verticalCenter
            anchors.topMargin: 58
            text: qsTr("登录后查看已购音乐")
            color: Theme.text
            font: Theme.font(22, Theme.semiboldWeight)
        }
    }
    EchoPopupLoader {
        id: batchOwner
        objectName: "purchasedBatchDrawerOwner"
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
        objectName: "purchasedSongMenuOwner"
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
}
