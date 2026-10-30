pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import EchoMusic.Theme
import EchoMusic.Controls
import EchoMusic.Music
import EchoMusic.Search

EchoPageScrollContainer {
    id: page
    objectName: "searchPage"
    required property SearchModel service
    property real viewportWidth: width
    // Shell zoom factor for the context menu; the route page forwards it.
    property real uiScale: 1
    /// The row with an open context menu retains its context-target fill.
    property string menuSongId: ""
    // Open at the pointer or keyboard-reported row edge in scene coordinates.
    function showSongMenu(id: string, sceneX: real, sceneY: real): void {
        page.menuSongId = id;
        songMenuOwner.open({
            songId: id,
            x: sceneX,
            y: sceneY
        });
    }
    property bool resultNavigationAvailable: false
    signal resultOpenRequested(string id)
    signal kindPicked(int kind)
    readonly property bool hasQuery: service.keyword.length > 0
    readonly property bool songs: service.kind === SearchModel.Song || service.kind === SearchModel.Lyric
    readonly property bool initialLoading: hasQuery && service.state === SearchModel.Loading && service.count === 0
    readonly property bool initialError: hasQuery && service.state === SearchModel.Error && service.count === 0
    readonly property bool showResults: hasQuery && !initialLoading && !initialError
    readonly property bool showAlbum: viewportWidth >= 768
    scrollView: showResults ? (songs ? songList : grid) : staticScroll
    coordinatedViews: [songList, grid]
    resetViews: [staticScroll]
    stickyHeaders: [regularHeader, toolbar, pinnedHeader]
    property bool pinned: false
    onScrollTopChanged: Qt.callLater(updatePinned)
    onScrolled: schedulePreload()
    function updatePinned() {
        const next = hasQuery && service.kind !== SearchModel.Song && scrollTop > 80;
        if (pinned === next)
            return;
        preservePosition(() => {
            pinned = next;
        });
    }
    readonly property real headerHeight: regularHeader.implicitHeight
    readonly property real resultStart: headerHeight + 16
    property string lastKeyword: ""
    property real modelResetOffset: 0
    onPositionCommitted: modelRestoreTask.stop()
    // A category reset changes both the proxy rows and the active view. Their
    // synchronous fixups must not become the page's new shared scroll offset.
    // Restore after that transaction and native layout, within this page's life.
    Timer {
        id: modelRestoreTask
        interval: 0
        property bool settling: false
        onTriggered: {
            if (!page.pageActive || !page.showResults)
                return;
            // EchoViewPoolGuard reattaches changed models on an owned zero
            // timer. Let that queued projection retire before restoring once.
            if (settling) {
                settling = false;
                restart();
                return;
            }
            if (page.scrollView.moving || page.scrollView.EchoWheelScroll.running)
                return;
            if (page.songs)
                songList.forceLayout();
            else
                grid.forceLayout();
            page.scrollTo(page.modelResetOffset);
        }
    }
    // Equivalent to the original intersection observer's 240px bottom margin.
    // Pagination errors inhibit automatic replay; only the retry action resumes.
    function checkPreload() {
        if (!pageActive || !showResults || !service.hasMore || service.errorMessage.length || service.state !== SearchModel.Ready)
            return;
        const view = songs ? songList : grid;
        if (view.footerItem && view.footerItem.y - view.contentY <= view.height + 240)
            service.loadMore();
    }
    function schedulePreload() {
        Qt.callLater(checkPreload);
    }
    Connections {
        target: page.service
        function onModelAboutToBeReset() {
            if (page.pageActive && !modelRestoreTask.running)
                page.modelResetOffset = page.scrollTop;
        }
        function onModelReset() {
            if (page.pageActive) {
                modelRestoreTask.settling = true;
                modelRestoreTask.restart();
            }
        }
        function onChanged() {
            if (page.lastKeyword !== page.service.keyword) {
                page.lastKeyword = page.service.keyword;
                modelRestoreTask.stop();
                page.modelResetOffset = 0;
                page.resetPosition();
            }
            Qt.callLater(page.updatePinned);
            page.schedulePreload();
        }
    }
    Flickable {
        id: staticScroll
        anchors.fill: parent
        visible: !page.showResults
        contentWidth: width
        contentHeight: Math.max(height, staticContent.height + 40)
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: EchoPageScrollBar {
            objectName: "discoveryScrollbar"
            view: staticScroll
            container: page
            viewActive: staticScroll.visible
        }
        Item {
            id: staticContent
            width: staticScroll.width
            height: page.hasQuery ? page.resultStart + (page.initialLoading ? skeleton.implicitHeight : 320) : page.headerHeight
            SearchHeader {
                width: parent.width
                height: implicitHeight
                service: page.service
                onKindPicked: kind => page.kindPicked(kind)
            }
            SearchResultsSkeleton {
                id: skeleton
                x: parent.width < 768 ? 24 : 40
                y: page.resultStart
                width: Math.max(0, parent.width - x * 2)
                visible: page.initialLoading
                songs: page.songs
                video: page.service.kind === SearchModel.Mv
                geometry: songList.geometry
                showAlbum: page.showAlbum
                lyricColumn: page.service.kind === SearchModel.Lyric
                pageActive: page.pageActive && visible
            }
            Column {
                visible: page.initialError
                x: 40
                y: page.resultStart + 130
                width: parent.width - 80
                spacing: 16
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: page.service.errorMessage
                    color: Theme.secondaryText
                    font: Theme.font(14, Theme.mediumWeight)
                }
                EchoSoftButton {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: qsTr("重新搜索")
                    onClicked: page.service.retry()
                }
            }
        }
    }
    SongList {
        id: songList
        objectName: "searchResults"
        x: 40
        width: parent.width - 80
        height: parent.height
        visible: page.showResults && page.songs
        model: visible && page.pageActive ? page.service.results : null
        pageActive: page.pageActive && visible
        pageContainer: page
        playbackAvailable: true
        activeTrackId: page.service.currentTrackId
        playing: page.service.playing
        showOriginal: true
        actionsAvailable: true
        menuSongId: page.menuSongId
        favoriteLookup: id => page.service.favoriteRevision >= 0 && page.service.isFavorite(id)
        onPlayRequested: id => page.service.playSong(id)
        onFavoriteRequested: id => page.service.toggleFavorite(id)
        onDetailsRequested: id => page.service.openDetails(id)
        onArtistRequested: id => page.service.openArtist(id)
        onAlbumRequested: id => page.service.openAlbum(id)
        onMvRequested: id => page.service.openMv(id)
        onMoreRequested: (id, sceneX, sceneY) => page.showSongMenu(id, sceneX, sceneY)
        lyricColumn: page.service.kind === SearchModel.Lyric
        showAlbum: page.showAlbum
        topMargin: (page.pinned ? 72 : page.resultStart) + toolbar.height
        footer: Item {
            width: songList.width
            height: (page.service.results.count === 0 ? 180 : 0) + status.implicitHeight
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                y: 80
                visible: page.service.results.count === 0
                text: page.service.results.query.trim().length ? qsTr("未找到相关歌曲") : qsTr("暂无歌曲")
                color: Theme.secondaryText
                font: Theme.decoratedFont(14, Theme.regularWeight, 0, true, false)
            }
            SearchStatus {
                id: status
                width: parent.width
                height: implicitHeight
                y: page.service.results.count === 0 ? 180 : 0
                service: page.service
                listPadding: 48
            }
        }
    }
    ContentGrid {
        id: grid
        objectName: "searchResultGrid"
        x: 40
        width: parent.width - 80 + 20
        height: parent.height
        visible: page.showResults && !page.songs
        sourceModel: visible && page.pageActive ? page.service.results : null
        pageActive: page.pageActive && visible
        pageContainer: page
        artist: page.service.kind === SearchModel.Artist
        video: page.service.kind === SearchModel.Mv
        navigationAvailable: page.resultNavigationAvailable
        onOpenRequested: id => page.resultOpenRequested(id)
        topMargin: page.pinned ? 72 : page.resultStart
        footer: Item {
            width: grid.width - 20
            height: (page.service.results.count === 0 ? 320 : 0) + gridStatus.implicitHeight
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                y: 150
                visible: page.service.results.count === 0
                text: qsTr("暂无搜索结果")
                color: Theme.secondaryText
                font: Theme.font(14, Theme.regularWeight)
            }
            SearchStatus {
                id: gridStatus
                width: parent.width
                height: implicitHeight
                y: page.service.results.count === 0 ? 320 : 0
                service: page.service
                listPadding: 24
            }
        }
    }
    // Header/toolbar live outside recycled content, so keyboard focus and filter
    // input survive delegate pooling and category model resets.
    EchoPageStickyHeader {
        id: regularHeader
        container: page
        sticky: false
        implicitHeight: inlineHeading.implicitHeight
        height: implicitHeight
        visible: page.showResults && !page.pinned
        SearchHeader {
            id: inlineHeading
            anchors.fill: parent
            service: page.service
            onKindPicked: kind => page.kindPicked(kind)
        }
    }
    EchoPageStickyHeader {
        id: toolbar
        objectName: "searchSongToolbar"
        container: page
        leftInset: 40
        rightInset: 40
        height: songToolbar.implicitHeight
        flowPosition: page.pinned ? 72 : page.resultStart
        stickyTop: page.pinned ? 50 : 0
        visible: page.showResults && page.songs
        SearchSongToolbar {
            id: songToolbar
            anchors.fill: parent
            service: page.service
            showAlbum: page.showAlbum
            geometry: songList.geometry
        }
    }
    EchoPageStickyHeader {
        id: pinnedHeader
        container: page
        visible: page.pinned
        height: 56
        Rectangle {
            anchors.fill: parent
            color: "transparent"
            SearchHeader {
                anchors.fill: parent
                service: page.service
                pinned: true
                onKindPicked: kind => page.kindPicked(kind)
            }
            Rectangle {
                anchors.bottom: parent.bottom
                // Align the divider with the tabs, rather than the full-page backdrop.
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.leftMargin: 40
                anchors.rightMargin: 40
                height: 0.5
                color: Theme.subtleBorder
            }
        }
    }

    EchoPopupLoader {
        id: songMenuOwner
        objectName: "searchSongMenuOwner"
        hostItem: page
        sourceComponent: Component {
            SongContextMenu {
                id: songMenu
                parent: Overlay.overlay
                scale: page.uiScale
                transformOrigin: Popup.TopLeft
                commands: page.service
                onClosed: page.menuSongId = ""
            }
        }
    }
}
