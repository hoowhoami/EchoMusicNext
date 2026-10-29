pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import EchoMusic.Shell
import EchoMusic.Controls
import EchoMusic.Music
import EchoMusic.Details

RoutePage {
    id: page
    required property AlbumDetailService service
    readonly property AlbumDetailContext detail: service.contextFor(context)
    readonly property bool commentsActive: String(context.query.tab || "songs") === "comments"
    objectName: "albumDetailPage"
    onPageActiveChanged: if (!page.pageActive) {
        batchOwner.close();
        introOwner.close();
        songMenuOwner.close();
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
    SongCatalogView {
        id: catalog
        anchors.fill: parent
        visible: !initialSkeleton.active
        collection: page.detail.songs
        actions: page.detail.actions
        resourceActive: page.pageActive && visible
        loading: page.detail.songsLoading && !page.detail.songs.totalCount
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
        headerComponent: Component {
            AlbumDetailHeader {
                detailContext: page.detail
                shareId: String(page.detail.detail.id || page.context.resourceId)
                router: page.router
                scrollTop: catalog.scrollTop
                showPlaybackActions: !page.commentsActive
                resourceActive: catalog.resourceActive
                onPlayRequested: page.detail.actions.play()
                onBatchRequested: batchOwner.open()
                onIntroRequested: introOwner.open()
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
        active: page.detail.detailLoading && !page.detail.detail.id
        sourceComponent: SongDetailSkeleton {
            typeLabel: "ALBUM"
            resourceActive: page.pageActive
        }
    }
    EchoPopupLoader {
        id: batchOwner
        objectName: "albumBatchDrawerOwner"
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
        objectName: "albumIntroDialogOwner"
        hostItem: page
        sourceComponent: Component {
            EchoTextDialog {
                id: intro
                heading: qsTr("专辑介绍")
                text: page.detail.detail.description || ""
                preferredWidth: 520
                uiScale: page.settings.uiScale
            }
        }
    }
    EchoPopupLoader {
        id: songMenuOwner
        objectName: "albumSongMenuOwner"
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
