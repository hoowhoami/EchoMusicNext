pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import EchoMusic.Controls

GridView {
    id: grid
    property bool pageActive: true
    property EchoPageScrollContainer pageContainer: null
    property bool artist: false
    property bool video: false
    property bool navigationAvailable: false
    // The view swaps in a bounded placeholder count while loading.
    // The catalog model retains ownership of the real rows.
    property var sourceModel: null
    property bool loading: false
    property bool presentationReady: false
    readonly property int columns: Math.max(1, Math.floor(width / ((video ? 220 : 180) + 20)))
    readonly property real cardWidth: Math.max(1, width / columns - 20)
    readonly property real cardHeight: (cardWidth - (artist ? 26 : video ? 18 : 22)) / (video ? 1.78 : 1) + (artist ? 70 : video ? 62 : 66)
    signal openRequested(string id)
    /// Optional load-more sentinel for paginated grids. Defaults off.
    property bool hasMore: false
    property bool loadingMore: false
    property string errorMessage: ""
    // Local filtering can hide every loaded row while later pages still exist.
    property int loadedItemCount: count
    signal loadMoreRequested
    EchoScrollLoadMore {
        view: grid
        active: grid.pageActive && grid.visible
        hasMore: grid.hasMore
        loading: grid.loading || grid.loadingMore
        failed: grid.errorMessage.length > 0
        itemCount: grid.loadedItemCount
        onLoadMoreRequested: grid.loadMoreRequested()
    }
    cellWidth: cardWidth + 20
    cellHeight: cardHeight + 20
    clip: true
    reuseItems: true
    cacheBuffer: cellHeight * 3
    boundsBehavior: Flickable.StopAtBounds
    function syncPresentation(): void {
        if (!grid.presentationReady)
            return;
        // Separate model/delegate bindings run in either order. Detach rows
        // before switching so ContentCard never receives integer placeholders
        // without its required catalog roles (including pooled delegates).
        // Drain the previous pool: its cards still belong to the old row model.
        const reuse = grid.reuseItems;
        grid.reuseItems = false;
        grid.model = null;
        grid.delegate = grid.loading ? skeletonDelegate : cardDelegate;
        grid.model = grid.loading ? grid.columns * 2 : grid.sourceModel;
        grid.reuseItems = reuse;
    }
    onLoadingChanged: syncPresentation()
    onSourceModelChanged: if (!grid.loading)
        syncPresentation()
    onColumnsChanged: if (grid.loading)
        syncPresentation()
    Component.onCompleted: {
        presentationReady = true;
        syncPresentation();
    }
    ScrollBar.vertical: EchoPageScrollBar {
        objectName: "gridScrollbar"
        view: grid
        container: grid.pageContainer
        viewActive: grid.visible && grid.pageActive
    }
    Component {
        id: cardDelegate
        ContentCard {
            id: card
            width: grid.cardWidth
            height: grid.cardHeight
            artist: grid.artist
            video: grid.video
            navigationAvailable: grid.navigationAvailable
            property bool pooled: false
            resourceActive: grid.pageActive && !pooled
            GridView.onPooled: pooled = true
            GridView.onReused: pooled = false
            onOpenRequested: id => grid.openRequested(id)
        }
    }
    Component {
        id: skeletonDelegate
        ContentSkeleton {
            property bool pooled: false
            width: grid.cardWidth
            height: grid.cardHeight
            video: grid.video
            resourceActive: grid.pageActive && !pooled
            GridView.onPooled: pooled = true
            GridView.onReused: pooled = false
        }
    }
    EchoSpinner {
        objectName: "gridMoreSpinner"
        parent: grid
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 12
        width: 18
        height: 18
        z: 2
        visible: grid.pageActive && grid.visible && grid.loadingMore && !grid.loading
        resourceActive: visible
    }
}
