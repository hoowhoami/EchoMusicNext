pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Controls
import EchoMusic.Music
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
    property real contentInset: 24
    property real topMargin: 0
    property real bottomMargin: 48
    property bool loading: false
    property string errorMessage: ""
    property bool resourceActive: true
    /// The row with an open context menu retains its context-target fill.
    property string menuSongId: ""
    /// Optional page-specific empty placeholder; null keeps the shared footer.
    property Component emptyComponent: null
    /// Optional pagination: the page keeps its own re-entrancy guard by only
    /// reporting hasMore while a request is not in flight. Defaults off so
    /// every existing catalog page is unchanged.
    property bool hasMore: false
    property bool loadingMore: false
    signal loadMoreRequested
    /// Optional explicit footer below populated rows, such as a load-more action.
    property Component loadMoreComponent: null
    readonly property string activeTrackId: actions.currentTrackId
    readonly property alias listView: songs
    readonly property SongListGeometry geometry: songs.geometry
    // A routed source-collection request. Deferred until its real rows are ready.
    property string locateTrackId: ""
    property string locateToken: ""
    property bool locateComplete: true
    property string locateMessage: ""
    property string handledLocateToken: ""
    property bool locateQueued: false
    function scheduleLocate(): void {
        if (locateQueued || !locateToken || locateToken === handledLocateToken)
            return;
        locateQueued = true;
        Qt.callLater(function () {
            root.locateQueued = false;
            root.applyLocate();
        });
    }
    function applyLocate(): void {
        if (!locateToken || locateToken === handledLocateToken)
            return;
        if (!resourceActive || !locateTrackId || locateTrackId !== activeTrackId) {
            handledLocateToken = locateToken;
            return;
        }
        if (loading || !songs.visible || songs.height <= 0)
            return;
        if (collection.query.length) {
            collection.query = "";
            scheduleLocate();
            return;
        }
        const row = collection.indexOf(locateTrackId);
        if (row >= 0) {
            songs.forceLayout();
            songs.positionViewAtIndex(row, ListView.Center);
            handledLocateToken = locateToken;
            locateMessage = "";
        } else if (locateComplete || errorMessage.length) {
            handledLocateToken = locateToken;
            locateMessage = errorMessage.length ? qsTr("未能读取来源歌曲列表，无法定位") : qsTr("当前曲目已不在来源列表中");
        }
    }
    onLocateTokenChanged: {
        locateMessage = "";
        scheduleLocate();
    }
    onResourceActiveChanged: scheduleLocate()
    onActiveTrackIdChanged: scheduleLocate()
    onLoadingChanged: scheduleLocate()
    onLocateCompleteChanged: scheduleLocate()
    onErrorMessageChanged: scheduleLocate()
    onHeightChanged: scheduleLocate()
    Component.onCompleted: scheduleLocate()
    Connections {
        target: root.collection
        function onChanged(): void {
            root.scheduleLocate();
        }
    }
    signal retryRequested
    signal moreRequested(string id, real sceneX, real sceneY)
    function checkLoadMore(): void {
        if (!root.hasMore || root.loadingMore || root.loading || !songs.visible)
            return;
        // contentY changes fire for every pixel of the tail; the guard above
        // plus the page-side requestPending flag bound this to one request.
        if (songs.atYEnd && songs.contentHeight > songs.height)
            root.loadMoreRequested();
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
        cacheBuffer: 240
        boundsBehavior: Flickable.StopAtBounds
        onMovementEnded: root.checkLoadMore()
        onAtYEndChanged: root.checkLoadMore()
        ScrollBar.vertical: EchoPageScrollBar {
            view: songs
            container: root.pageContainer
            viewActive: root.resourceActive
        }
        readonly property SongListGeometry geometry: SongListGeometry {
            rowCount: root.collection.count
        }
        delegate: songDelegate
        Component {
            id: songDelegate
            SongRow {
                id: row
                required privilegeTags
                required qualityLabel
                required catalogPlayable
                required original
                property bool pooled: false
                width: songs.width
                geometry: songs.geometry
                active: resourceId.length > 0 && root.activeTrackId === resourceId
                playing: root.actions.playing
                resourceActive: root.resourceActive && !root.loading && !pooled
                playbackAvailable: !root.loading
                actionsAvailable: true
                favorite: {
                    return root.actions.favoriteRevision >= 0 && root.actions.isFavorite(resourceId);
                }
                ListView.onPooled: pooled = true
                ListView.onReused: pooled = false
                contextTarget: root.menuSongId.length > 0 && resourceId === root.menuSongId
                onPlayRequested: id => root.actions.playOrToggle(id)
                onDoubleClicked: root.actions.play(resourceId, false)
                onFavoriteRequested: id => root.actions.toggleFavorite(id)
                onDetailsRequested: id => root.actions.openDetails(id)
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
            readonly property bool empty: !root.loading && root.collection.count === 0
            // Partial-result errors appear below populated rows with an inline retry.
            readonly property bool inlineError: !root.loading && root.errorMessage.length > 0 && root.collection.count > 0
            readonly property bool pagination: root.collection.count > 0 && !root.loading && !inlineError && (root.loadingMore || root.loadMoreComponent !== null)
            readonly property Item emptyLoaderItem: emptyLoader.item as Item
            visible: empty || inlineError || pagination
            height: empty ? (emptyLoaderItem ? emptyLoaderItem.implicitHeight : 156) : inlineError ? 56 : pagination ? (loadMoreLoader.item ? (loadMoreLoader.item as Item).implicitHeight : 44) : 0
            Loader {
                id: emptyLoader
                anchors.left: parent.left
                anchors.right: parent.right
                active: catalogFooter.empty && root.emptyComponent !== null
                sourceComponent: root.emptyComponent
            }
            ColumnLayout {
                anchors.centerIn: parent
                visible: !emptyLoader.active && (catalogFooter.empty || catalogFooter.inlineError)
                spacing: 8
                Text {
                    Layout.maximumWidth: Math.max(0, catalogFooter.width - 48)
                    text: root.errorMessage.length > 0 ? root.errorMessage : root.collection.query.length ? qsTr("未找到匹配歌曲") : qsTr("暂无歌曲")
                    textFormat: Text.PlainText
                    color: Theme.secondaryText
                    font: Theme.font(12, Theme.regularWeight)
                }
                EchoSoftButton {
                    Layout.alignment: Qt.AlignHCenter
                    visible: root.errorMessage.length > 0
                    text: qsTr("重新加载")
                    onClicked: root.retryRequested()
                }
            }
            Loader {
                id: loadMoreLoader
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.right: parent.right
                active: catalogFooter.pagination
                sourceComponent: root.loadMoreComponent !== null ? root.loadMoreComponent : defaultLoadMore
            }
            Component {
                id: defaultLoadMore
                Item {
                    implicitHeight: 44
                    Text {
                        anchors.centerIn: parent
                        text: qsTr("正在加载更多…")
                        textFormat: Text.PlainText
                        color: Theme.secondaryText
                        font: Theme.font(12, Theme.regularWeight)
                    }
                }
            }
        }
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
