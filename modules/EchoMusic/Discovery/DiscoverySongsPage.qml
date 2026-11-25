pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Shell
import EchoMusic.Controls
import EchoMusic.Theme
import EchoMusic.Music

RoutePage {
    id: page
    required property DiscoveryService service
    property bool ranking: false
    readonly property SongCatalogModel collection: ranking ? service.rankingSongs : service.dailySongs
    readonly property int part: ranking ? (service.ranks.length > 0 ? DiscoveryService.RankSongs : service.states[DiscoveryService.RankCatalog] === DiscoveryService.Error ? DiscoveryService.RankCatalog : DiscoveryService.RankTop) : DiscoveryService.Daily
    readonly property bool loading: service.states[part] === DiscoveryService.Loading || service.states[part] === DiscoveryService.Idle
    readonly property bool ranksLoading: ranking && service.ranks.length === 0 && [DiscoveryService.RankTop, DiscoveryService.RankCatalog].some(part => service.states[part] === DiscoveryService.Idle || service.states[part] === DiscoveryService.Loading)
    readonly property string rankDescription: String(service.selectedRank.description || "").split(/\r?\n/).map(line => line.trim()).filter(line => line.length && !/^更新频率\s*[:：]/.test(line)).join(" · ")
    objectName: ranking ? "rankingPage" : "dailyRecommendPage"
    onPageActiveChanged: if (!pageActive) {
        batchOwner.close();
        selectorOwner.close();
        introOwner.close();
        songMenuOwner.close();
    }
    // Open at the pointer or trailing action edge in scene coordinates.
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
        collection: page.collection
        actions: page.service.actions(page.collection)
        resourceActive: page.pageActive && visible
        loading: page.loading
        expandedHeaderHeight: 176
        errorMessage: page.service.states[page.part] === DiscoveryService.Error ? page.service.errors[page.part] : ""
        menuSongId: page.menuSongId
        onRetryRequested: page.service.retry(page.part)
        onMoreRequested: (id, sceneX, sceneY) => page.showSongMenu(id, sceneX, sceneY)
        headerComponent: Component {
            SongDetailHeader {
                id: detail
                heading: page.ranking ? qsTr("排行榜") : qsTr("每日推荐")
                typeLabel: page.ranking ? "RANK" : "RECOMMEND"
                subtitle: page.ranking ? "" : page.service.dailySubtitle || qsTr("为你量身定制的每日歌单")
                detailsComponent: page.ranking && page.rankDescription.length > 0 ? rankIntroduction : null
                secondaryDetailsComponent: Component {
                    Flow {
                        objectName: "discoveryDetailMetadata"
                        spacing: 8
                        // Reserve 4px after each field in addition to Flow's 8px gap.
                        EchoButtonContent {
                            objectName: "discoveryDetailCount"
                            visible: !page.ranking || page.service.rankTotal > 0
                            width: Math.min(parent.width, implicitWidth + 4)
                            height: implicitHeight
                            horizontalAlignment: Qt.AlignLeft
                            glyph: "play"
                            glyphSize: 12
                            spacing: 6
                            lineHeight: 16.5
                            text: qsTr("%1 首歌曲").arg(page.ranking ? page.service.rankTotal : page.collection.totalCount)
                            font: Theme.font(11, Theme.semiboldWeight)
                            color: Theme.secondaryText
                        }
                        Repeater {
                            model: page.ranking ? [page.service.rankTotal > page.collection.totalCount ? qsTr("已加载 %1 首").arg(page.collection.totalCount) : "", page.service.selectedRank.update_frequency ? qsTr("更新频率：%1").arg(page.service.selectedRank.update_frequency) : "", page.service.selectedRank.published_at ? page.service.selectedRank.published_at + qsTr(" 更新") : ""].filter(value => value.length > 0) : page.service.dailyDate.length ? [page.service.dailyDate + qsTr(" 推荐")] : []
                            delegate: EchoTextTagRow {
                                required property string modelData
                                objectName: "discoveryDetailMetadataField"
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
                day: page.service.dailyDate.length ? String(Number(page.service.dailyDate.slice(-2))) : page.service.todayLabel
                ranking: page.ranking
                scrollTop: catalog.scrollTop
                playable: page.collection.count > 0
                resourceActive: catalog.resourceActive
                loading: page.ranksLoading || !page.ranking && page.loading
                leadingActions: page.ranking ? chooseRank : null
                coverComponent: Component {
                    DiscoveryThemedCover {
                        cornerRadius: Theme.mediaRadius / detail.coverScale
                        day: detail.day
                        ranking: detail.ranking
                        resourceActive: detail.resourceActive
                    }
                }
                onPlayRequested: page.service.play(page.collection)
                onBatchRequested: batchOwner.open()
                onSelectRequested: selectorOwner.open()
                onIntroRequested: if (page.rankDescription.length)
                    introOwner.open()
            }
        }
    }
    Loader {
        id: initialSkeleton
        anchors.fill: parent
        active: page.ranking ? page.ranksLoading : page.loading
        sourceComponent: SongDetailSkeleton {
            typeLabel: page.ranking ? "RANK" : "RECOMMEND"
            resourceActive: page.pageActive
        }
    }
    Component {
        id: rankIntroduction
        EchoActionButton {
            id: introduction
            objectName: "rankingIntroduction"
            text: page.rankDescription
            padding: 0
            font: Theme.font(13, Theme.semiboldWeight)
            Accessible.name: qsTr("查看完整榜单简介")
            background: null
            contentItem: EchoTextTagRow {
                objectName: "rankingIntroductionText"
                text: introduction.text
                font: introduction.font
                color: Theme.secondaryText
                fillTextWidth: true
            }
            onClicked: introOwner.open()
        }
    }
    Component {
        id: chooseRank
        EchoSoftButton {
            objectName: "discoveryChooseRank"
            text: page.service.selectedRank.title || qsTr("排行榜选择")
            enabled: page.service.ranks.length > 0
            glyph: "chevron-down"
            onClicked: selectorOwner.open()
        }
    }
    EchoPopupLoader {
        id: batchOwner
        objectName: "discoveryBatchDrawerOwner"
        hostItem: page
        sourceComponent: Component {
            SongCatalogBatchDrawer {
                id: batch
                actions: page.service.actions(page.collection)
                collection: page.collection
                uiScale: page.settings.uiScale
            }
        }
    }
    EchoPopupLoader {
        id: selectorOwner
        objectName: "discoveryRankSelectorOwner"
        hostItem: page
        sourceComponent: Component {
            EchoDialog {
                id: selector
                objectName: "discoveryRankSelector"
                heading: qsTr("排行榜选择")
                preferredWidth: 520
                uiScale: page.settings.uiScale
                property int activeTab: 0
                readonly property var options: activeTab === 0 ? page.service.recommendedRanks : page.service.ranks
                readonly property int directoryPart: activeTab === 0 ? DiscoveryService.RankTop : DiscoveryService.RankCatalog
                onOpened: activeTab = page.service.selectedRank.id && !page.service.recommendedRanks.some(rank => rank.id === page.service.selectedRank.id) ? 1 : 0
                body: Component {
                    ColumnLayout {
                        width: parent.width
                        spacing: 12
                        EchoTabBar {
                            objectName: "discoveryRankGroups"
                            Layout.fillWidth: true
                            labels: [qsTr("推荐"), qsTr("全部")]
                            currentIndex: selector.activeTab
                            onPicked: index => selector.activeTab = index
                        }
                        ListView {
                            id: ranks
                            objectName: "discoveryRankOptions"
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            Layout.minimumHeight: 0
                            Layout.preferredHeight: implicitHeight
                            readonly property real rowHeight: EchoControlSize.heightFor(EchoControlSize.Compact)
                            readonly property var rows: {
                                const result = [];
                                let row = [], used = 0;
                                for (const rank of selector.options) {
                                    const itemWidth = Math.min(width, Math.ceil(optionMetrics.advanceWidth(String(rank.title))) + 32);
                                    if (row.length && used + spacing + itemWidth > width) {
                                        result.push(row);
                                        row = [];
                                        used = 0;
                                    }
                                    row.push({
                                        rank: rank,
                                        itemWidth: itemWidth
                                    });
                                    used += (row.length > 1 ? spacing : 0) + itemWidth;
                                }
                                if (row.length)
                                    result.push(row);
                                return result;
                            }
                            spacing: 8
                            implicitHeight: rows.length ? Math.max(0, Math.min(360, rows.length * (rowHeight + spacing) - spacing)) : 80
                            model: width > 0 && height > 0 ? rows : []
                            currentIndex: rows.findIndex(row => row.some(item => item.rank.id === page.service.selectedRank.id))
                            FontMetrics {
                                id: optionMetrics
                                font: Theme.font(12, Theme.semiboldWeight)
                            }
                            reuseItems: true
                            EchoViewPoolGuard {
                                targetView: ranks
                            }
                            cacheBuffer: rowHeight
                            boundsBehavior: Flickable.StopAtBounds
                            clip: true
                            ScrollBar.vertical: EchoPopupScrollBar {
                                surface: selector
                                view: ranks
                                resourceActive: selector.visible
                            }
                            Connections {
                                target: selector
                                function onOpened(): void {
                                    ranks.positionViewAtIndex(Math.max(0, ranks.currentIndex), ListView.Contain);
                                }
                            }
                            delegate: Row {
                                id: optionRow
                                required property var modelData
                                width: ranks.width
                                height: ranks.rowHeight
                                spacing: ranks.spacing
                                Repeater {
                                    model: optionRow.modelData
                                    delegate: EchoSoftButton {
                                        required property var modelData
                                        objectName: "discoveryRankOption"
                                        readonly property var rankId: modelData.rank.id
                                        width: modelData.itemWidth
                                        height: ranks.rowHeight
                                        size: EchoControlSize.Compact
                                        leftPadding: 16
                                        rightPadding: 16
                                        colors: Theme.floating
                                        text: modelData.rank.title
                                        accent: rankId === page.service.selectedRank.id
                                        Accessible.role: Accessible.RadioButton
                                        Accessible.checked: accent
                                        onClicked: {
                                            const id = rankId;
                                            selector.close();
                                            page.service.selectRank(id);
                                        }
                                    }
                                }
                            }
                        }
                        Label {
                            Layout.fillWidth: true
                            visible: selector.options.length === 0 || page.service.states[selector.directoryPart] === DiscoveryService.Error
                            text: page.service.errors[selector.directoryPart] || (page.service.states[selector.directoryPart] < DiscoveryService.Ready ? qsTr("正在加载榜单…") : qsTr("暂时没有榜单"))
                            wrapMode: Text.Wrap
                            color: Theme.floatingSecondaryText
                            font: Theme.font(12, Theme.regularWeight)
                        }
                        EchoSoftButton {
                            visible: page.service.states[selector.directoryPart] === DiscoveryService.Error
                            text: qsTr("重试")
                            colors: Theme.floating
                            onClicked: page.service.retry(selector.directoryPart)
                        }
                    }
                }
            }
        }
    }
    EchoPopupLoader {
        id: introOwner
        hostItem: page
        sourceComponent: Component {
            EchoTextDialog {
                id: intro
                objectName: "rankingIntroductionDialog"
                heading: page.service.selectedRank.title || qsTr("榜单简介")
                preferredWidth: 520
                uiScale: page.settings.uiScale
                text: page.rankDescription
            }
        }
    }
    EchoPopupLoader {
        id: songMenuOwner
        objectName: "discoverySongMenuOwner"
        hostItem: page
        sourceComponent: Component {
            SongContextMenu {
                id: songMenu
                parent: Overlay.overlay
                uiScale: page.settings.uiScale
                commands: page.service.actions(page.collection)
                onClosed: page.menuSongId = ""
            }
        }
    }
}
