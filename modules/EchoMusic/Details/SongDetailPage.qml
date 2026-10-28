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
    required property DetailsService service
    readonly property SongDetailContext detail: service.songFor(context)
    readonly property bool music: !context.query.type || context.query.type === "music"
    readonly property bool commentTab: !music || String(context.query.tab || context.query.mainTab || "detail") === "comment"
    objectName: "songDetailPage"
    property string expandedRanking: ""
    function rankingItems(rank: var): var {
        const matched = page.detail.rankingDetails.filter(item => rank.platform > 0 && item.platform === rank.platform || rank.platform_name && item.platform_name === rank.platform_name);
        return matched.length ? matched : page.detail.rankings.length <= 1 ? page.detail.rankingDetails : [];
    }
    function countText(value: var): string {
        if (value === undefined || value === null || !Number.isFinite(Number(value)))
            return "—";
        const number = Number(value);
        return number < 10000 ? String(number) : (number / 10000).toFixed(number >= 100000 ? 0 : 1).replace(/\.0$/, "") + "w";
    }
    function chooseTab(index: int): void {
        const query = Object.assign({}, context.query, {
            tab: index === 0 ? "detail" : "comment"
        });
        delete query.mainTab;
        router.replaceQuery(query);
    }
    function syncCommentTab(): void {
        const tabs = ["all", "classify", "hotword"];
        const index = tabs.indexOf(String(context.query.commentTab || "all"));
        if (index >= 0 && detail.comments.tab !== index)
            detail.comments.tab = index;
    }
    Component.onCompleted: syncCommentTab()
    Connections {
        target: page.context
        function onQueryChanged(): void {
            page.syncCommentTab();
        }
    }
    EchoPageScrollContainer {
        id: viewport
        anchors.fill: parent
        pageActive: page.pageActive
        backToTopThreshold: 360
        scrollView: comments.listView
        stickyHeaders: page.music ? [header, tabs] : [header]
        CommentsView {
            id: comments
            anchors.fill: parent
            controller: page.detail.comments
            pageContainer: viewport
            topMargin: 196 + (page.music ? 52 : 20)
            showComments: page.commentTab
            showSections: page.music
            onSectionPicked: index => page.router.replaceQuery(Object.assign({}, page.context.query, {
                    commentTab: ["all", "classify", "hotword"][index]
                }))
            resourceActive: page.pageActive
            uiScale: page.settings.uiScale
            detailContent: Component {
                Column {
                    id: detailSections
                    width: comments.listView.width
                    spacing: 16
                    Repeater {
                        model: [
                            {
                                title: qsTr("可选音质"),
                                tags: page.detail.qualities
                            },
                            {
                                title: qsTr("可用音效"),
                                tags: page.detail.effects
                            }
                        ]
                        delegate: Column {
                            id: tagGroup
                            required property var modelData
                            width: detailSections.width
                            visible: modelData.tags.length > 0
                            height: visible ? implicitHeight : 0
                            spacing: 12
                            Label {
                                text: tagGroup.modelData.title
                                font: Theme.font(14, Theme.boldWeight)
                                color: Theme.text
                            }
                            Flow {
                                width: parent.width
                                spacing: 10
                                Repeater {
                                    model: tagGroup.modelData.tags
                                    delegate: EchoTag {
                                        required property string modelData
                                        text: modelData
                                        tone: EchoTag.Accent
                                        size: EchoTag.Small
                                    }
                                }
                            }
                        }
                    }
                    Column {
                        width: parent.width
                        visible: page.detail.rankings.length > 0
                        spacing: 12
                        Label {
                            text: qsTr("榜单成就")
                            font: Theme.font(14, Theme.boldWeight)
                            color: Theme.text
                        }
                        Label {
                            width: parent.width
                            visible: String(page.detail.detail.ranking_summary || "").length > 0
                            text: "• " + (page.detail.detail.ranking_summary || "")
                            color: Theme.secondaryText
                            font: Theme.font(12, Theme.regularWeight)
                            wrapMode: Text.Wrap
                        }
                        Repeater {
                            model: page.detail.rankings
                            delegate: Rectangle {
                                id: ranking
                                required property var modelData
                                required property int index
                                readonly property string cardKey: String(modelData.platform || modelData.platform_name || "rank") + ":" + index
                                readonly property bool expanded: page.expandedRanking === cardKey
                                readonly property var items: page.rankingItems(modelData)
                                width: parent.width
                                implicitHeight: rankContent.implicitHeight
                                radius: Theme.cardRadius
                                color: Theme.withAlpha(Theme.colors.tone, .06)
                                border.width: 1
                                border.color: Theme.withAlpha(Theme.colors.tone, .10)
                                ColumnLayout {
                                    id: rankContent
                                    width: parent.width
                                    spacing: 0
                                    AbstractButton {
                                        id: rankToggle
                                        objectName: "songRankingToggle"
                                        Layout.fillWidth: true
                                        implicitHeight: rankRow.implicitHeight + 36
                                        Accessible.name: ranking.modelData.platform_name || qsTr("榜单详情")
                                        onClicked: page.expandedRanking = ranking.expanded ? "" : ranking.cardKey
                                        contentItem: RowLayout {
                                            id: rankRow
                                            spacing: 14
                                            EchoCover {
                                                Layout.leftMargin: 18
                                                Layout.preferredWidth: 42
                                                Layout.preferredHeight: 42
                                                source: (ranking.items.find(item => String(item.logo_pic || "").length) || {}).logo_pic || ranking.modelData.platform_logo || ""
                                                resourceActive: page.pageActive
                                                cornerRadius: Theme.cardRadius
                                            }
                                            ColumnLayout {
                                                Layout.fillWidth: true
                                                Layout.minimumWidth: 0
                                                spacing: 4
                                                Label {
                                                    Layout.fillWidth: true
                                                    text: ranking.modelData.platform_name || qsTr("未知平台")
                                                    color: Theme.text
                                                    font: Theme.font(16, Theme.boldWeight)
                                                    elide: Text.ElideRight
                                                }
                                                Label {
                                                    Layout.fillWidth: true
                                                    text: qsTr("累计上榜：%1次    最近上榜：%2").arg(ranking.modelData.ranking_times === null ? "—" : ranking.modelData.ranking_times).arg(ranking.modelData.last_time || qsTr("未知"))
                                                    font: Theme.font(12, Theme.regularWeight)
                                                    color: Theme.secondaryText
                                                    wrapMode: Text.Wrap
                                                }
                                            }
                                            EchoTag {
                                                text: qsTr("第 %1 名").arg(ranking.modelData.ranking_num === null ? "—" : ranking.modelData.ranking_num)
                                                tone: EchoTag.Accent
                                                size: EchoTag.Small
                                                font: Theme.font(14, Theme.boldWeight)
                                                implicitHeight: 30
                                                radius: 15
                                            }
                                            EchoIcon {
                                                Layout.rightMargin: 18
                                                name: "chevron-down"
                                                rotation: ranking.expanded ? 180 : 0
                                                Behavior on rotation {
                                                    enabled: page.pageActive && Theme.store.motionEnabled
                                                    NumberAnimation {
                                                        duration: 180
                                                        easing.type: Easing.OutCubic
                                                    }
                                                }
                                                Layout.preferredWidth: 18
                                                Layout.preferredHeight: 18
                                                color: Theme.secondaryText
                                            }
                                        }
                                        background: Rectangle {
                                            radius: Theme.cardRadius
                                            color: rankToggle.hovered ? Theme.withAlpha(Theme.colors.tone, .04) : "transparent"
                                        }
                                    }
                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        Layout.leftMargin: 18
                                        Layout.rightMargin: 18
                                        Layout.bottomMargin: ranking.expanded ? 18 : 0
                                        visible: ranking.expanded
                                        spacing: 10
                                        Repeater {
                                            model: ranking.items
                                            delegate: RowLayout {
                                                id: rankingEntry
                                                required property var modelData
                                                Layout.fillWidth: true
                                                spacing: 12
                                                ColumnLayout {
                                                    Layout.fillWidth: true
                                                    Layout.minimumWidth: 0
                                                    Label {
                                                        Layout.fillWidth: true
                                                        text: rankingEntry.modelData.name || ""
                                                        color: Theme.text
                                                        font: Theme.font(13, Theme.semiboldWeight)
                                                        elide: Text.ElideRight
                                                    }
                                                    Label {
                                                        text: qsTr("%1 · 累计上榜 %2 次").arg(rankingEntry.modelData.platform_name || "").arg(rankingEntry.modelData.count === null ? "—" : rankingEntry.modelData.count)
                                                        color: Theme.secondaryText
                                                        font: Theme.font(11, Theme.regularWeight)
                                                    }
                                                }
                                                Label {
                                                    text: rankingEntry.modelData.top_ranking > 0 ? qsTr("最高第 %1 名").arg(rankingEntry.modelData.top_ranking) : rankingEntry.modelData.top_ranking === 0 ? qsTr("暂无排名") : "—"
                                                    color: Theme.accentText
                                                    font: Theme.font(12, Theme.semiboldWeight)
                                                }
                                            }
                                        }
                                        Label {
                                            Layout.fillWidth: true
                                            visible: ranking.items.length === 0
                                            text: qsTr("暂无更多榜单详情")
                                            color: Theme.secondaryText
                                            font: Theme.font(12, Theme.regularWeight)
                                        }
                                    }
                                }
                            }
                        }
                    }
                    Label {
                        width: parent.width
                        visible: page.detail.loading
                        text: qsTr("正在加载歌曲详情…")
                        color: Theme.secondaryText
                        horizontalAlignment: Text.AlignHCenter
                        font: Theme.font(12, Theme.regularWeight)
                    }
                    Label {
                        width: parent.width
                        visible: page.detail.errorMessage.length > 0
                        text: page.detail.errorMessage
                        wrapMode: Text.Wrap
                        color: Theme.secondaryText
                        font: Theme.font(12, Theme.regularWeight)
                    }
                    EchoSoftButton {
                        visible: page.detail.errorMessage.length > 0
                        text: qsTr("重试")
                        onClicked: page.detail.reload()
                    }
                    Label {
                        width: parent.width
                        visible: !page.detail.loading && !page.detail.errorMessage.length && !page.detail.rankings.length && !page.detail.qualities.length
                        text: qsTr("暂无更多歌曲详情")
                        color: Theme.secondaryText
                        horizontalAlignment: Text.AlignHCenter
                        font: Theme.font(12, Theme.regularWeight)
                    }
                }
            }
        }
        EchoPageStickyHeader {
            id: header
            container: viewport
            flowPosition: 0
            height: heading.implicitHeight
            SongDetailHeader {
                id: heading
                anchors.fill: parent
                expandedHeight: 196
                coverSize: 170
                heading: page.detail.detail.title || (page.music ? qsTr("歌曲") : qsTr("评论"))
                typeLabel: page.music ? "SONG" : String(page.context.query.type || "COMMENTS").toUpperCase()
                subtitle: page.detail.detail.artist || ""
                scrollTop: viewport.scrollTop
                resourceActive: page.pageActive
                loading: page.detail.loading && !String(page.detail.detail.title || "").length
                playable: page.detail.songs.count > 0 && !page.detail.loading
                showBatchAction: false
                showPlaybackActions: page.music
                coverComponent: Component {
                    EchoCover {
                        source: page.detail.detail.cover_url || ""
                        resourceActive: page.pageActive
                        cornerRadius: Theme.mediaRadius / heading.coverScale
                    }
                }
                detailsComponent: Component {
                    EchoArtistLinks {
                        objectName: "songArtistLink"
                        artists: page.detail.artistLinks
                        font: Theme.font(13, Theme.semiboldWeight)
                        color: Theme.accentText
                        lineHeight: 20
                        requireCompleteName: true
                        onArtistRequested: id => page.router.navigate("artist-detail", {}, id)
                    }
                }
                secondaryDetailsComponent: Component {
                    Flow {
                        visible: page.music
                        spacing: 8
                        RowLayout {
                            spacing: 8
                            EchoSoftButton {
                                objectName: "songAlbumLink"
                                text: page.detail.detail.album || qsTr("单曲")
                                plain: true
                                leftPadding: 0
                                rightPadding: 0
                                implicitHeight: 18.7
                                font: Theme.font(11, Theme.semiboldWeight)
                                foreground: enabled ? Theme.accentText : Theme.secondaryText
                                enabled: page.detail.albumNavigationId.length > 0
                                onClicked: page.router.navigate("album-detail", {}, page.detail.albumNavigationId)
                            }
                            EchoTextTagRow {
                                text: "•"
                                font: Theme.font(11, Theme.semiboldWeight)
                                lineHeight: 18.7
                                color: Theme.secondaryText
                            }
                        }
                        Repeater {
                            model: [page.detail.detail.language || qsTr("未知"), qsTr("%1 收藏").arg(page.countText(page.detail.detail.favorite_count)), qsTr("%1 评论").arg(page.countText(page.detail.detail.comment_count))]
                            delegate: EchoTextTagRow {
                                id: metadataItem
                                required property string modelData
                                required property int index
                                text: modelData
                                font: Theme.font(11, Theme.semiboldWeight)
                                lineHeight: 18.7
                                spacing: 8
                                color: Theme.secondaryText
                                EchoTextTagRow {
                                    visible: metadataItem.index < 2
                                    text: "•"
                                    font: Theme.font(11, Theme.semiboldWeight)
                                    lineHeight: 18.7
                                    color: Theme.secondaryText
                                }
                            }
                        }
                    }
                }
                additionalActions: Component {
                    RowLayout {
                        spacing: 8
                        EchoSoftButton {
                            text: qsTr("添加到")
                            glyph: "list"
                            enabled: page.detail.songs.count > 0 && !page.detail.loading
                            onClicked: page.detail.actions.addSong(page.detail.songId)
                        }
                        EchoSoftButton {
                            text: qsTr("分享")
                            glyph: "share"
                            enabled: page.detail.songs.count > 0
                            onClicked: page.detail.actions.shareSong(page.detail.songId)
                        }
                    }
                }
                onPlayRequested: page.detail.actions.play()
            }
        }
        EchoPageStickyHeader {
            id: tabs
            objectName: "songDetailMainTabs"
            visible: page.music
            container: viewport
            flowPosition: 196
            stickyTop: header.height
            height: 52
            EchoDetailTabs {
                x: 24
                height: parent.height
                labels: [qsTr("详情"), qsTr("评论")]
                labelWeight: Theme.semiboldWeight
                currentIndex: page.commentTab ? 1 : 0
                onPicked: index => page.chooseTab(index)
            }
        }
    }
}
