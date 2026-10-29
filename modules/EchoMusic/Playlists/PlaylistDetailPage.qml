pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import EchoMusic.Shell
import EchoMusic.Controls
import EchoMusic.Music
import EchoMusic.Details
import EchoMusic.Library

RoutePage {
    id: page
    required property PlaylistDetailService service
    readonly property PlaylistDetailContext detail: service.contextFor(context)
    readonly property bool commentsActive: String(context.query.tab || "songs") === "comments"
    objectName: "playlistDetailPage"
    onPageActiveChanged: if (!pageActive) {
        batchOwner.close();
        introOwner.close();
        songMenuOwner.close();
        orderDialogOwner.close();
        editDialogOwner.close();
    }
    // Open at the pointer or keyboard-reported row edge in scene coordinates.
    // Keep the target row highlighted until the menu closes.
    property string menuSongId: ""
    // One open command's local target survives the mutation's synchronous
    // library/detail invalidation before PlaylistOrderModel emits saved().
    property real orderTargetListId: 0
    function showSongMenu(id: string, sceneX: real, sceneY: real): void {
        page.menuSongId = id;
        songMenuOwner.open({
            songId: id,
            x: sceneX,
            y: sceneY
        });
    }
    SongCatalogView {
        id: catalog
        anchors.fill: parent
        visible: !initialSkeleton.active
        collection: page.detail.songs
        actions: page.detail.actions
        resourceActive: page.pageActive && visible
        locateTrackId: String(page.context.query.locateTrack || "")
        locateToken: String(page.context.query.locateToken || "")
        locateComplete: page.detail.complete
        loading: page.detail.loading && !page.detail.songs.totalCount
        errorMessage: page.detail.errorMessage
        secondaryActive: page.commentsActive
        secondaryTabCount: page.detail.comments.totalText === "—" ? "" : page.detail.comments.totalText
        onTabPicked: index => page.router.replaceQuery(Object.assign({}, page.context.query, {
                tab: index === 0 ? "songs" : "comments"
            }))
        secondaryBody: Component {
            CommentsView {
                controller: page.detail.comments
                pageContainer: catalog.pageContainer
                showSections: false
                topMargin: catalog.expandedHeaderHeight + 76
                resourceActive: page.pageActive && page.commentsActive
                uiScale: page.settings.uiScale
                Component.onCompleted: catalog.secondaryView = listView
                Component.onDestruction: if (catalog.secondaryView === listView)
                    catalog.secondaryView = null
            }
        }
        menuSongId: page.menuSongId
        expandedHeaderHeight: 176 + (page.detail.detail.description ? 28 : 0)
        onRetryRequested: page.detail.reload()
        onMoreRequested: (id, sceneX, sceneY) => page.showSongMenu(id, sceneX, sceneY)
        extraActions: page.detail.orderListId > 0 ? orderAction : null
        headerComponent: Component {
            PlaylistDetailHeader {
                detailContext: page.detail
                shareFallbackId: page.context.resourceId
                scrollTop: catalog.scrollTop
                showPlaybackActions: !page.commentsActive
                resourceActive: catalog.resourceActive
                onPlayRequested: page.detail.actions.play()
                onBatchRequested: batchOwner.open()
                onIntroRequested: introOwner.open()
                onEditRequested: editDialogOwner.open({
                    cloudId: page.detail.editListId
                })
            }
        }
    }
    EchoEnter {
        target: catalog
        active: page.pageActive && !initialSkeleton.active
    }
    Loader {
        id: initialSkeleton
        anchors.fill: parent
        active: page.detail.loading && !page.detail.detail.global_id
        sourceComponent: SongDetailSkeleton {
            typeLabel: "PLAYLIST"
            resourceActive: page.pageActive
        }
    }
    Component {
        id: orderAction
        SongSortButton {
            objectName: "playlistSongSort"
            onClicked: {
                page.orderTargetListId = page.detail.orderListId;
                orderDialogOwner.open({
                    tracksListId: page.orderTargetListId,
                    tracksTitle: page.detail.detail.title || ""
                });
            }
        }
    }
    EchoPopupLoader {
        id: editDialogOwner
        objectName: "playlistEditDialogOwner"
        hostItem: page
        sourceComponent: Component {
            PlaylistEditDialog {
                editor: page.service.editor
                uiScale: page.settings.uiScale
            }
        }
    }
    EchoPopupLoader {
        id: orderDialogOwner
        objectName: "playlistOrderDialogOwner"
        hostItem: page
        sourceComponent: Component {
            PlaylistOrderDialog {
                editor: page.service.order
                tracksMode: true
                uiScale: page.settings.uiScale
            }
        }
    }
    Connections {
        target: page.service.order
        enabled: orderDialogOwner.item !== null
        function onSaved(): void {
            if (page.service.order.mode !== 1 || page.service.order.listId !== page.orderTargetListId)
                return;
            page.detail.songs.clearSort();
            page.detail.songs.query = "";
        }
    }
    EchoPopupLoader {
        id: batchOwner
        objectName: "discoveryBatchDrawerOwner"
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
        hostItem: page
        sourceComponent: Component {
            EchoTextDialog {
                id: intro
                heading: qsTr("歌单介绍")
                text: page.detail.detail.description || ""
                preferredWidth: 520
                uiScale: page.settings.uiScale
            }
        }
    }
    EchoPopupLoader {
        id: songMenuOwner
        objectName: "playlistSongMenuOwner"
        hostItem: page
        sourceComponent: Component {
            SongContextMenu {
                id: songMenu
                parent: Overlay.overlay
                scale: page.settings.uiScale
                transformOrigin: Popup.TopLeft
                commands: page.detail.actions
                removeVisible: page.detail.owned
                onRemoveTriggered: page.detail.removeSong(songMenu.songId)
                onClosed: page.menuSongId = ""
            }
        }
    }
}
