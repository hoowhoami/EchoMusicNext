pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Shell
import EchoMusic.Controls
import EchoMusic.Theme
import EchoMusic.Music

// Free-listening catalog owned by DiscoveryService.
// Uses the shared song tab, playback, refresh and batch actions.
RoutePage {
    id: page
    objectName: "freeListenPage"
    required property DiscoveryService service
    readonly property SongCatalogModel collection: service.freeListen
    readonly property SongCatalogActions actions: service.actions(page.collection)
    // Named partState, not state: Item already owns the state-machine property.
    readonly property int partState: service.states[DiscoveryService.FreeListen]
    readonly property bool loading: page.partState === DiscoveryService.Loading || page.partState === DiscoveryService.Idle
    readonly property string errorMessage: page.partState === DiscoveryService.Error ? service.errors[DiscoveryService.FreeListen] : ""
    // Use unfiltered row count for the toolbar so a search with no matches
    // keeps the search box reachable.
    readonly property bool hasResult: page.collection.totalCount > 0
    readonly property bool searching: page.collection.query.trim().length > 0
    function refresh(): void {
        // Clear the writable query property before refreshing.
        // setQuery is not a QML-invokable method.
        page.collection.query = "";
        page.service.loadFreeListen();
    }
    // Open at the pointer or trailing action edge in scene coordinates.
    // Keep the target row highlighted until the menu closes.
    property string menuSongId: ""
    function openSongMenu(id: string, sceneX: real, sceneY: real): void {
        page.menuSongId = id;
        songMenuOwner.open({
            songId: id,
            x: sceneX,
            y: sceneY
        });
    }
    onPageActiveChanged: if (!pageActive) {
        songMenuOwner.close();
        batchOwner.close();
    }

    Component {
        id: heading
        SongDetailHeader {
            id: freeListenHeader
            heading: qsTr("免费听")
            typeLabel: "FREE"
            subtitle: qsTr("概念版免费听推荐流，跟着推着听")
            day: ""
            playable: page.collection.count > 0
            resourceActive: page.pageActive
            loading: page.loading
            scrollTop: catalog.scrollTop
            coverComponent: Component {
                FreeListenCover {
                    cornerRadius: Theme.mediaRadius / freeListenHeader.coverScale
                }
            }
            secondaryDetailsComponent: Component {
                Flow {
                    objectName: "freeListenDetailMetadata"
                    spacing: 8
                    EchoButtonContent {
                        objectName: "freeListenDetailCount"
                        visible: !page.loading
                        width: Math.min(parent.width, implicitWidth + 4)
                        height: implicitHeight
                        horizontalAlignment: Qt.AlignLeft
                        glyph: "play"
                        glyphSize: 12
                        spacing: 6
                        lineHeight: 16.5
                        text: qsTr("%1 首歌曲").arg(page.collection.totalCount)
                        font: Theme.font(11, Theme.semiboldWeight)
                        color: Theme.secondaryText
                    }
                }
            }
            additionalActions: Component {
                EchoSoftButton {
                    objectName: "freeListenRefresh"
                    text: qsTr("刷新")
                    glyph: page.loading ? "" : "refresh"
                    loading: page.loading
                    onClicked: page.refresh()
                }
            }
            onPlayRequested: page.service.play(page.collection)
            onBatchRequested: batchOwner.open()
        }
    }

    SongCatalogView {
        id: catalog
        anchors.fill: parent
        collection: page.collection
        actions: page.actions
        headerComponent: heading
        songTabLabel: qsTr("歌曲")
        menuSongId: page.menuSongId
        showSortHeader: false
        showToolbar: page.hasResult
        loading: page.loading
        errorMessage: page.errorMessage
        resourceActive: page.pageActive
        emptyComponent: emptyCard
        onRetryRequested: page.refresh()
        onMoreRequested: (id, sceneX, sceneY) => page.openSongMenu(id, sceneX, sceneY)
    }

    // Page-specific empty card; other catalog pages keep the shared default.
    Component {
        id: emptyCard
        Item {
            objectName: "freeListenEmptyState"
            implicitHeight: 240
            ColumnLayout {
                anchors.centerIn: parent
                width: Math.min(420, Math.max(0, parent.width - 48))
                spacing: 0
                EchoIcon {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredWidth: 34
                    Layout.preferredHeight: 34
                    name: "music-discount"
                    color: Theme.iconSecondary
                }
                Text {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.topMargin: 14
                    text: page.searching ? qsTr("未找到相关歌曲") : page.errorMessage.length > 0 ? qsTr("暂时无法加载免费听推荐") : qsTr("这次没拿到推荐内容")
                    color: Theme.text
                    font: Theme.font(14, Theme.semiboldWeight)
                }
                Text {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.topMargin: 6
                    Layout.fillWidth: true
                    visible: !page.searching
                    text: page.errorMessage.length > 0 ? qsTr("内容还没能加载出来，请稍后重试。") : qsTr("免费听来自概念版接口，返回空列表时通常是上游临时不可用，稍后重试即可。")
                    color: Theme.secondaryText
                    font: Theme.font(12, Theme.regularWeight)
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                }
                EchoSoftButton {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.topMargin: 20
                    visible: !page.searching
                    text: qsTr("刷新")
                    glyph: "refresh"
                    onClicked: page.refresh()
                }
            }
        }
    }

    EchoPopupLoader {
        id: batchOwner
        objectName: "freeListenBatchDrawerOwner"
        hostItem: page
        sourceComponent: Component {
            SongCatalogBatchDrawer {
                actions: page.actions
                collection: page.collection
                uiScale: page.settings.uiScale
            }
        }
    }

    EchoPopupLoader {
        id: songMenuOwner
        objectName: "freeListenSongMenuOwner"
        hostItem: page
        sourceComponent: Component {
            SongContextMenu {
                id: songMenu
                parent: Overlay.overlay
                uiScale: page.settings.uiScale
                commands: page.actions
                onClosed: page.menuSongId = ""
            }
        }
    }
}
