pragma ComponentBehavior: Bound
import QtQuick
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
    readonly property bool commentTab: context.name === "song-comments" || !music
    objectName: "songDetailPage"
    function countText(value: var): string {
        if (value === undefined || value === null || !Number.isFinite(Number(value)))
            return "—";
        const number = Number(value);
        return number < 10000 ? String(number) : (number / 10000).toFixed(number >= 100000 ? 0 : 1).replace(/\.0$/, "") + "w";
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
        stickyHeaders: [header]
        CommentsView {
            id: comments
            anchors.fill: parent
            controller: page.detail.comments
            pageContainer: viewport
            topMargin: 216
            showComments: page.commentTab
            showSections: page.music
            onSectionPicked: index => page.router.replaceQuery(Object.assign({}, page.context.query, {
                    commentTab: ["all", "classify", "hotword"][index]
                }))
            resourceActive: page.pageActive
            uiScale: page.settings.uiScale
            detailContent: Component {
                SongDetailContent {
                    width: comments.listView.width
                    detail: page.detail
                    router: page.router
                    resourceActive: page.pageActive
                }
            }
        }
        EchoPageStickyHeader {
            id: header
            objectName: "songPageHeader"
            container: viewport
            flowPosition: 0
            height: heading.implicitHeight
            SongDetailHeader {
                id: heading
                anchors.fill: parent
                expandedHeight: 196
                coverSize: 170
                heading: page.detail.detail.title || (page.music ? qsTr("歌曲") : qsTr("评论"))
                typeLabel: page.commentTab ? "COMMENT" : "SONG"
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
                    RowLayout {
                        spacing: 8
                        EchoArtistLinks {
                            objectName: "songArtistLink"
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            // RowLayout rounds allocations; reserve enough room
                            // for a complete name before enabling its link.
                            Layout.preferredWidth: Math.ceil(implicitWidth)
                            Layout.maximumWidth: Math.ceil(implicitWidth)
                            artists: page.detail.artistLinks
                            font: Theme.font(13, Theme.semiboldWeight)
                            color: Theme.accentText
                            lineHeight: 20
                            requireCompleteName: true
                            onArtistRequested: id => page.router.navigate("artist-detail", {}, id)
                        }
                        EchoTextTagRow {
                            visible: page.music
                            text: "•"
                            font: Theme.font(13, Theme.semiboldWeight)
                            lineHeight: 20
                            color: Theme.secondaryText
                        }
                        EchoSoftButton {
                            objectName: "songAlbumLink"
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            Layout.maximumWidth: implicitWidth
                            visible: page.music
                            text: page.detail.detail.album || qsTr("单曲")
                            plain: true
                            leftPadding: 0
                            rightPadding: 0
                            implicitHeight: 20
                            font: Theme.font(13, Theme.semiboldWeight)
                            foreground: enabled ? Theme.accentText : Theme.secondaryText
                            enabled: page.detail.albumNavigationId.length > 0
                            onClicked: page.router.navigate("album-detail", {}, page.detail.albumNavigationId)
                        }
                        Item {
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                        }
                    }
                }
                secondaryDetailsComponent: Component {
                    Flow {
                        objectName: "songHeaderMetadata"
                        visible: page.music
                        spacing: 8
                        Repeater {
                            id: headerCounts
                            model: [qsTr("%1 收藏").arg(page.countText(page.detail.detail.favorite_count)), qsTr("%1 评论").arg(page.countText(page.detail.detail.comment_count))]
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
                                    visible: metadataItem.index < headerCounts.count - 1
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
    }
}
