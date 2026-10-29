pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import EchoMusic.Controls

ListView {
    id: list
    property bool pageActive: true
    property EchoPageScrollContainer pageContainer: null
    property bool lyricColumn: false
    property bool showAlbum: true
    property bool showOriginal: false
    property bool playbackAvailable: false
    property string activeTrackId: ""
    property bool playing: false
    /// Search rows surface the same context menu and row actions as the
    /// shared catalogs; the page injects the favorite lookup so this control
    /// keeps no business state.
    property bool actionsAvailable: false
    property var favoriteLookup: null
    property string menuSongId: ""
    signal favoriteRequested(string id)
    signal detailsRequested(string id)
    signal artistRequested(string id)
    signal albumRequested(string id)
    signal mvRequested(string id)
    readonly property SongListGeometry geometry: SongListGeometry {
        rowCount: list.count
    }
    signal playRequested(string id)
    signal moreRequested(string id, real sceneX, real sceneY)
    clip: true
    reuseItems: true
    EchoViewPoolGuard {
        targetView: list
    }
    cacheBuffer: 60 * 10
    boundsBehavior: Flickable.StopAtBounds
    ScrollBar.vertical: EchoPageScrollBar {
        objectName: "songScrollbar"
        view: list
        container: list.pageContainer
        viewActive: list.visible && list.pageActive
    }
    delegate: SongRow {
        id: row
        required original
        required privilegeTags
        required qualityLabel
        showOriginal: list.showOriginal
        width: list.width
        geometry: list.geometry
        lyricColumn: list.lyricColumn
        showAlbum: list.showAlbum
        playbackAvailable: list.playbackAvailable
        active: resourceId.length > 0 && resourceId === list.activeTrackId
        playing: list.playing
        actionsAvailable: list.actionsAvailable
        favorite: list.favoriteLookup !== null && list.favoriteLookup(resourceId)
        contextTarget: list.menuSongId.length > 0 && resourceId === list.menuSongId
        // Pooling is transient view state, never song/selection business state.
        property bool pooled: false
        resourceActive: list.pageActive && !pooled
        ListView.onPooled: pooled = true
        ListView.onReused: pooled = false
        onPlayRequested: id => list.playRequested(id)
        onFavoriteRequested: id => list.favoriteRequested(id)
        onDetailsRequested: id => list.detailsRequested(id)
        onArtistRequested: id => list.artistRequested(id)
        onAlbumRequested: id => list.albumRequested(id)
        onMvRequested: id => list.mvRequested(id)
        onMoreRequested: (id, sceneX, sceneY) => list.moreRequested(id, sceneX, sceneY)
        TapHandler {
            acceptedButtons: Qt.RightButton
            onTapped: eventPoint => {
                const point = row.mapToItem(null, eventPoint.position.x, eventPoint.position.y);
                list.moreRequested(row.resourceId, point.x, point.y);
            }
        }
    }
}
