pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Controls
import EchoMusic.Theme

// Shared virtual song body for detail/discovery catalogs. It owns only the
// result and loading ListViews; page headers, toolbars and scroll containers
// stay with the page so a second tab can reuse the same viewport.
Item {
    id: root
    objectName: "catalogSongList"
    required property SongCatalogModel collection
    required property SongCatalogActions actions
    required property EchoPageScrollContainer pageContainer
    Binding {
        target: root.pageContainer
        property: "dataRecovering"
        value: root.collection.cacheRecovering || (root.collection.querying && root.recoveryPending)
    }
    property bool recoveryPending: false
    Connections {
        target: root.collection
        function onCacheRecoveryChanged(): void {
            if (root.collection.cacheRecovering)
                root.recoveryPending = true;
        }
        function onChanged(): void {
            if (!root.collection.cacheRecovering && !root.collection.querying)
                root.recoveryPending = false;
        }
    }
    property real contentInset: 24
    property real topMargin: 0
    property real bottomMargin: 48
    property bool loading: false
    onLoadingChanged: {
        if (!loading && resourceActive && collection.count > 0)
            initialInsert.restart();
        schedulePreload();
    }
    // The catalog owns this one-shot. Destroying a route cancels it before a
    // deferred QML function can outlive its evaluation context.
    Timer {
        id: initialInsert
        interval: 0
        onTriggered: if (root.resourceActive && root.visible && !root.loading && root.collection.count > 0)
            listMotion.promote()
    }
    property string errorMessage: ""
    property bool resourceActive: true
    /// The row with an open context menu retains its context-target fill.
    property string menuSongId: ""
    /// Optional page-specific empty placeholder; null keeps the shared footer.
    property Component emptyComponent: null
    readonly property bool filteredEmpty: collection.totalCount > 0 && collection.query.trim().length > 0
    /// Optional pagination: the page keeps its own re-entrancy guard by only
    /// reporting hasMore while a request is not in flight. Defaults off so
    /// every existing catalog page is unchanged.
    property bool hasMore: false
    property bool loadingMore: false
    signal loadMoreRequested
    property int preloadRowCount: -1
    onHasMoreChanged: schedulePreload()
    onLoadingMoreChanged: schedulePreload()
    onResourceActiveChanged: {
        if (!resourceActive)
            initialInsert.stop();
        schedulePreload();
    }
    onErrorMessageChanged: schedulePreload()
    readonly property string activeTrackId: actions.currentTrackId
    readonly property alias listView: songs
    readonly property SongListGeometry geometry: songs.geometry
    function animateInsertion(): void {
        listMotion.promote();
    }
    signal retryRequested
    signal moreRequested(string id, real sceneX, real sceneY)
    function captureScrollAnchor(): var {
        if (!root.resourceActive || root.pageContainer.scrollView !== songs || root.pageContainer.scrollTop <= 0)
            return ({});
        const index = songs.indexAt(1, Math.max(0, songs.contentY + root.pageContainer.occlusionHeight + 1));
        const item = songs.itemAtIndex(index) as SongRow;
        return item ? {
            id: item.resourceId,
            delta: songs.contentY - item.y
        } : ({});
    }
    function restoreScrollAnchor(anchor): bool {
        if (!anchor.id || root.pageContainer.scrollView !== songs)
            return false;
        const index = root.collection.indexOf(anchor.id);
        if (index < 0)
            return false;
        songs.positionViewAtIndex(index, ListView.Beginning);
        songs.forceLayout();
        const item = songs.itemAtIndex(index);
        if (!item)
            return false;
        songs.contentY = item.y + anchor.delta;
        return true;
    }
    Connections {
        target: root.pageContainer
        enabled: root.resourceActive && songs.visible && root.pageContainer.scrollView === songs
        function onCaptureAnchorRequested(): void {
            root.pageContainer.capturedAnchor = root.captureScrollAnchor();
        }
        function onRestoreAnchorRequested(anchor): void {
            root.pageContainer.anchorRestored = root.restoreScrollAnchor(anchor);
        }
    }
    function schedulePreload(): void {
        if (root.resourceActive && root.visible && !preloadTimer.running)
            preloadTimer.start();
    }
    Timer {
        id: preloadTimer
        interval: 0
        repeat: false
        onTriggered: root.checkLoadMore()
    }
    function checkLoadMore(): void {
        if (!root.resourceActive || !root.hasMore || root.loadingMore || root.loading || root.errorMessage.length > 0 || !songs.visible || songs.height <= 0)
            return;
        if (root.preloadRowCount !== root.collection.count) {
            root.preloadRowCount = root.collection.count;
            songs.forceLayout();
        }
        // Fill short pages and prefetch one viewport before the tail. The
        // domain owner still owns the request/page guard and failure retry.
        const remaining = songs.contentHeight - (songs.contentY - songs.originY + songs.height);
        if (remaining <= Math.max(240, songs.height))
            root.loadMoreRequested();
    }
    Connections {
        target: root.collection
        function onChanged(): void {
            root.schedulePreload();
        }
    }
    ListView {
        id: songs
        objectName: "discoverySongs"
        x: root.contentInset
        width: Math.max(0, parent.width - root.contentInset * 2)
        height: parent.height
        topMargin: root.topMargin
        bottomMargin: root.bottomMargin
        // Keep one role model for the view's entire lifetime. Replacing it
        // with an integer skeleton model races required-role delegate
        // teardown/creation during a successful favorite -> reload.
        model: root.collection
        visible: !root.loading
        reuseItems: true
        EchoViewPoolGuard {
            targetView: songs
        }
        cacheBuffer: 240
        boundsBehavior: Flickable.StopAtBounds
        onContentYChanged: root.schedulePreload()
        onContentHeightChanged: root.schedulePreload()
        onHeightChanged: root.schedulePreload()
        onVisibleChanged: root.schedulePreload()
        ScrollBar.vertical: EchoPageScrollBar {
            view: songs
            container: root.pageContainer
            viewActive: root.resourceActive
        }
        readonly property QtObject _echoUiColumns: songs.geometry
        readonly property SongListGeometry geometry: SongListGeometry {
            rowCount: root.collection.count
        }
        delegate: songDelegate
        EchoListInsertMotion {
            id: listMotion
            view: songs
            active: root.resourceActive && !root.loading
            occlusionHeight: root.pageContainer.occlusionHeight
        }
        Component {
            id: songDelegate
            SongRow {
                id: row
                required privilegeTags
                required qualityLabel
                required catalogPlayable
                required original
                required property bool presentationReady
                property bool pooled: false
                width: songs.width
                geometry: songs.geometry
                active: resourceId.length > 0 && root.activeTrackId === resourceId
                playing: root.actions.playing
                playbackLoading: root.actions.playbackLoading
                resourceActive: root.resourceActive && !root.loading && !pooled
                playbackAvailable: !root.loading
                actionsAvailable: true
                favorite: {
                    return root.actions.favoriteRevision >= 0 && root.actions.isFavorite(resourceId);
                }
                ListView.onPooled: pooled = true
                ListView.onReused: pooled = false
                Binding {
                    target: row.contentItem
                    property: "visible"
                    value: row.presentationReady
                }
                Loader {
                    anchors.fill: parent
                    active: !row.presentationReady && row.resourceActive
                    sourceComponent: SongSkeletonRow {
                        geometry: songs.geometry
                        animate: row.resourceActive
                    }
                }
                contextTarget: root.menuSongId.length > 0 && resourceId === root.menuSongId
                onPlayRequested: id => root.actions.playOrToggle(id)
                onDoubleClicked: if (!row.pendingPlay)
                    root.actions.play(resourceId, false)
                onFavoriteRequested: id => root.actions.toggleFavorite(id)
                onDetailsRequested: id => root.actions.openDetails(id)
                onCommentsRequested: id => root.actions.openComments(id)
                onArtistRequested: id => root.actions.openArtist(id)
                onAlbumRequested: id => root.actions.openAlbum(id)
                onMvRequested: id => root.actions.openMv(id)
                onMoreRequested: (id, sceneX, sceneY) => root.moreRequested(id, sceneX, sceneY)
                TapHandler {
                    acceptedButtons: Qt.RightButton
                    onTapped: eventPoint => {
                        const point = row.mapToItem(null, eventPoint.position.x, eventPoint.position.y);
                        root.moreRequested(row.resourceId, point.x, point.y);
                    }
                }
            }
        }
        footer: Item {
            id: catalogFooter
            objectName: "songCatalogEmptyState"
            width: songs.width
            readonly property bool empty: !root.loading && !root.loadingMore && !root.hasMore && !root.collection.querying && !root.collection.cacheRecovering && root.collection.count === 0
            // Partial-result errors appear below populated rows with an inline retry.
            readonly property bool errorState: !root.loading && root.errorMessage.length > 0
            readonly property Item emptyLoaderItem: emptyLoader.item as Item
            visible: empty || errorState
            height: errorState ? errorLoader.implicitHeight : empty ? (emptyLoaderItem ? emptyLoaderItem.implicitHeight : 156) : 0
            Loader {
                id: emptyLoader
                anchors.left: parent.left
                anchors.right: parent.right
                active: catalogFooter.empty && !catalogFooter.errorState && !root.filteredEmpty && root.emptyComponent !== null
                sourceComponent: root.emptyComponent
            }
            ColumnLayout {
                anchors.centerIn: parent
                visible: !emptyLoader.active && catalogFooter.empty && !catalogFooter.errorState
                spacing: 8
                Text {
                    objectName: "songCatalogEmptyText"
                    Layout.maximumWidth: Math.max(0, catalogFooter.width - 48)
                    text: root.filteredEmpty ? qsTr("未找到匹配歌曲") : qsTr("暂无歌曲")
                    textFormat: Text.PlainText
                    color: Theme.secondaryText
                    font: Theme.font(12, Theme.regularWeight)
                }
            }
            Loader {
                id: errorLoader
                anchors.left: parent.left
                anchors.right: parent.right
                active: root.resourceActive && root.visible && catalogFooter.errorState
                sourceComponent: EchoLoadError {
                    message: root.errorMessage
                    onRetryRequested: root.retryRequested()
                }
            }
        }
    }
    EchoSpinner {
        objectName: "catalogMoreSpinner"
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 12
        width: 18
        height: 18
        visible: root.resourceActive && !root.loading && root.loadingMore && root.collection.totalCount > 0 && root.errorMessage.length === 0
        resourceActive: visible
    }
    // A separate, bounded presentation view never receives song roles and
    // has no business state. It releases all eight skeletons when idle.
    ListView {
        id: loadingSongs
        objectName: "catalogLoadingSongs"
        x: songs.x
        width: songs.width
        height: songs.height
        topMargin: songs.topMargin
        bottomMargin: songs.bottomMargin
        contentY: songs.contentY
        visible: root.loading
        model: root.loading && root.visible ? 8 : 0
        interactive: false
        reuseItems: true
        EchoViewPoolGuard {
            targetView: loadingSongs
        }
        cacheBuffer: 0
        delegate: SongSkeletonRow {
            property bool pooled: false
            width: loadingSongs.width
            geometry: songs.geometry
            animate: root.resourceActive && root.loading && !pooled
            ListView.onPooled: pooled = true
            ListView.onReused: pooled = false
        }
    }
}
