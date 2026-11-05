pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Shell
import EchoMusic.Controls
import EchoMusic.Music
import EchoMusic.Theme

RoutePage {
    id: page
    objectName: "explorePage"
    required property ExploreService service
    readonly property bool songTab: service.tab === 1 || service.tab === 3
    readonly property SongCatalogModel songs: service.tab === 1 ? service.rankSongs : service.newSongs
    readonly property SongCatalogActions actions: service.tab === 1 ? service.rankActions : service.newActions
    signal batchRequested
    function choose(kind: string): void {
        chooserOwner.open({
            kind: kind
        });
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
        chooserOwner.close();
        songMenuOwner.close();
    }
    readonly property int activeTab: service.tab
    onActiveTabChanged: {
        chooserOwner.close();
        songMenuOwner.close();
    }
    Component {
        id: heading
        ExploreHeader {
            service: page.service
            onPickRequested: kind => page.choose(kind)
            onBatchRequested: page.batchRequested()
        }
    }
    Loader {
        id: rankSection
        anchors.fill: parent
        active: page.service.tab === 1
        sourceComponent: Component {
            SongCatalogView {
                scrollStateKey: "explore-rank-songs"
                EchoPopupLoader {
                    id: batchOwner
                    objectName: "discoveryBatchDrawerOwner"
                    hostItem: page
                    sourceComponent: Component {
                        SongCatalogBatchDrawer {
                            id: batch
                            collection: page.service.rankSongs
                            actions: page.service.rankActions
                            uiScale: page.settings.uiScale
                        }
                    }
                }
                Connections {
                    target: page
                    function onBatchRequested(): void {
                        if (page.service.tab === 1)
                            batchOwner.open();
                    }
                    function onPageActiveChanged(): void {
                        if (!page.pageActive)
                            batchOwner.close();
                    }
                }
                collection: page.service.rankSongs
                actions: page.service.rankActions
                headerComponent: heading
                expandedHeaderHeight: 156
                contentInset: 40
                headerTopMargin: 16
                compactSongToolbar: true
                songTabVariant: "explore"
                menuSongId: page.menuSongId
                loading: page.service.loading
                errorMessage: page.service.errorMessage
                resourceActive: page.pageActive
                onRetryRequested: page.service.retry()
                onMoreRequested: (id, sceneX, sceneY) => page.openSongMenu(id, sceneX, sceneY)
            }
        }
    }
    Loader {
        id: newSection
        anchors.fill: parent
        active: page.service.tab === 3
        sourceComponent: Component {
            SongCatalogView {
                scrollStateKey: "explore-new-songs"
                EchoPopupLoader {
                    id: batchOwner
                    objectName: "discoveryBatchDrawerOwner"
                    hostItem: page
                    sourceComponent: Component {
                        SongCatalogBatchDrawer {
                            id: batch
                            collection: page.service.newSongs
                            actions: page.service.newActions
                            uiScale: page.settings.uiScale
                        }
                    }
                }
                Connections {
                    target: page
                    function onBatchRequested(): void {
                        if (page.service.tab === 3)
                            batchOwner.open();
                    }
                    function onPageActiveChanged(): void {
                        if (!page.pageActive)
                            batchOwner.close();
                    }
                }
                collection: page.service.newSongs
                actions: page.service.newActions
                headerComponent: heading
                expandedHeaderHeight: 156
                contentInset: 40
                headerTopMargin: 16
                compactSongToolbar: true
                songTabVariant: "explore"
                menuSongId: page.menuSongId
                loading: page.service.loading
                errorMessage: page.service.errorMessage
                resourceActive: page.pageActive
                onRetryRequested: page.service.retry()
                onMoreRequested: (id, sceneX, sceneY) => page.openSongMenu(id, sceneX, sceneY)
            }
        }
    }
    EchoPageScrollContainer {
        id: viewport
        objectName: "exploreCardsViewport"
        anchors.fill: parent
        visible: !page.songTab
        pageActive: page.pageActive && visible
        scrollView: page.service.tab === 4 ? (artistSection.item as ArtistDirectory) : grid
        stickyHeaders: [cardsHeader, cardFilters]
        ContentGrid {
            id: grid
            objectName: "exploreCards"
            x: 40
            // Grid cells include their trailing gap; match the shared search grid.
            width: Math.max(0, parent.width - 80 + 20)
            height: parent.height
            topMargin: cardsHeader.height + cardFilters.height + 16
            bottomMargin: 40
            sourceModel: page.service.tab === 4 ? null : page.service.cards
            pageContainer: viewport
            pageActive: viewport.pageActive && visible
            visible: page.service.tab !== 4 && !page.service.loading
            navigationAvailable: page.router.available(page.service.tab === 0 ? "playlist-detail" : page.service.tab === 2 ? "album-detail" : "artist-detail")
            onOpenRequested: id => page.router.navigate(page.service.tab === 0 ? "playlist-detail" : page.service.tab === 2 ? "album-detail" : "artist-detail", {}, id)
            footer: emptyState
        }
        Loader {
            id: artistSection
            x: 40
            width: Math.max(0, parent.width - 80 + 20)
            height: parent.height
            active: page.service.tab === 4
            visible: active && !page.service.loading
            sourceComponent: ArtistDirectory {
                objectName: "exploreArtistDirectory"
                collection: page.service.cards
                avatarLimit: 20
                pageContainer: viewport
                pageActive: viewport.pageActive && artistSection.visible
                topMargin: cardsHeader.height + cardFilters.height + 16
                bottomMargin: 40
                navigationAvailable: page.router.available("artist-detail")
                navigationHeader: artistLettersHeader
                emptyFooter: emptyState
                onOpenRequested: id => page.router.navigate("artist-detail", {}, id)
            }
        }
        GridLayout {
            id: loadingGrid
            x: 40
            y: cardsHeader.height + cardFilters.height + 16
            width: Math.max(0, parent.width - 80)
            visible: page.service.loading
            columns: Math.max(1, Math.floor((width + 20) / 200))
            columnSpacing: 20
            rowSpacing: 20
            Repeater {
                model: 8
                delegate: ContentSkeleton {
                    Layout.fillWidth: true
                    Layout.preferredWidth: (loadingGrid.width - 20 * (loadingGrid.columns - 1)) / loadingGrid.columns
                    resourceActive: viewport.pageActive
                }
            }
        }
        EchoPageStickyHeader {
            id: cardsHeader
            objectName: "exploreCardsHeader"
            container: viewport
            flowPosition: 16
            height: 96
            z: 2
            ExploreHeader {
                anchors.fill: parent
                service: page.service
                showToolbar: false
            }
        }
        EchoPageStickyHeader {
            id: cardFilters
            objectName: "exploreCardFilters"
            container: viewport
            flowPosition: 112
            sticky: false
            height: 60
            z: 1
            ExploreHeader {
                anchors.fill: parent
                service: page.service
                showNavigation: false
                onPickRequested: kind => page.choose(kind)
            }
        }
    }
    Component {
        id: artistLettersHeader
        Item {
            width: parent.width
            implicitHeight: letterViewport.visible ? 36 : 0
            Flickable {
                id: letterViewport
                objectName: "exploreArtistLetters"
                width: parent.width
                height: 26
                contentWidth: letters.implicitWidth
                contentHeight: 26
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                visible: page.service.tab === 4 && !page.service.loading && page.service.cards.count > 0
                Row {
                    id: letters
                    spacing: 2
                    Repeater {
                        model: [qsTr("全部")].concat(page.service.artistLetters)
                        delegate: EchoSoftButton {
                            required property string modelData
                            implicitHeight: 26
                            implicitWidth: Math.max(26, contentItem.implicitWidth + 12)
                            leftPadding: 6
                            rightPadding: 6
                            text: modelData
                            accent: page.service.artistLetter === modelData
                            onClicked: page.service.selectLetter(modelData)
                        }
                    }
                }
            }
        }
    }
    Component {
        id: emptyState
        Item {
            objectName: "exploreEmptyState"
            width: Math.max(0, page.width - 80)
            // Zero geometry does not suppress painting of centered children.
            visible: !page.service.loading && page.service.cards.count === 0
            height: visible ? 220 : 0
            ColumnLayout {
                anchors.centerIn: parent
                spacing: 12
                Label {
                    text: page.service.errorMessage || (page.service.tab === 2 ? qsTr("暂无专辑") : page.service.tab === 4 ? qsTr("暂无歌手") : qsTr("暂无歌单"))
                    font: Theme.font(14, Theme.regularWeight)
                    color: Theme.secondaryText
                }
                EchoSoftButton {
                    Layout.alignment: Qt.AlignHCenter
                    visible: page.service.errorMessage.length > 0
                    text: qsTr("重新加载")
                    onClicked: page.service.retry()
                }
            }
        }
    }
    EchoPopupLoader {
        id: chooserOwner
        objectName: "explorePickerOwner"
        hostItem: page
        sourceComponent: Component {
            EchoOptionPicker {
                id: chooser
                objectName: "explorePicker"
                property string kind: "category"
                uiScale: page.settings.uiScale
                heading: kind === "category" ? qsTr("歌单分类") : kind === "rank" ? qsTr("排行榜选择") : kind === "region" ? qsTr("专辑分类") : kind === "sex" ? qsTr("性别筛选") : qsTr("类型筛选")
                preferredWidth: kind === "sex" || kind === "artistType" ? 360 : 500
                groups: kind === "category" ? page.service.categories.map(g => ({
                            label: g.name,
                            options: g.children.map(v => ({
                                        id: v.id,
                                        label: v.name
                                    }))
                        })) : [
                    {
                        label: "",
                        options: kind === "rank" ? page.service.ranks.map(v => ({
                                    id: String(v.id),
                                    label: v.title
                                })) : kind === "region" ? [
                            {
                                id: "all",
                                label: qsTr("全部")
                            },
                            {
                                id: "chn",
                                label: qsTr("华语")
                            },
                            {
                                id: "eur",
                                label: qsTr("欧美")
                            },
                            {
                                id: "jpn",
                                label: qsTr("日本")
                            },
                            {
                                id: "kor",
                                label: qsTr("韩国")
                            }
                        ] : kind === "sex" ? page.service.sexFilters : page.service.typeFilters
                    }
                ]
                selectedId: kind === "region" ? page.service.region : kind === "sex" ? page.service.sex : kind === "artistType" ? page.service.artistType : kind === "category" ? page.service.category : page.service.rankId
                onChosen: id => {
                    if (kind === "category")
                        page.service.selectCategory(id);
                    else if (kind === "rank")
                        page.service.selectRank(id);
                    else if (kind === "region")
                        page.service.selectRegion(id);
                    else if (kind === "sex")
                        page.service.selectSex(id);
                    else
                        page.service.selectArtistType(id);
                }
            }
        }
    }
    EchoPopupLoader {
        id: songMenuOwner
        objectName: "exploreSongMenuOwner"
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
