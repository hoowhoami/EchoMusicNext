pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import EchoMusic.Controls
import EchoMusic.Theme
import EchoMusic.Music

// Album header using the shared sliver geometry and album-specific metadata rows.
SongDetailHeader {
    id: header
    required property AlbumDetailContext detailContext
    property var router: null
    property string shareId: ""
    readonly property var info: detailContext.detail
    readonly property var artistList: info.artists !== undefined && info.artists !== null ? info.artists : []
    readonly property int songCount: detailContext.songs.totalCount
    readonly property string publishedDate: String(info.published_at || "").split(/[ T]/)[0]
    readonly property string publisher: String(info.publisher || "").trim()
    readonly property string publicationText: [publishedDate, publisher].filter(value => value.length > 0).join(" · ")
    readonly property var categoryTags: [String(info.category || "")].filter(value => value.length > 0)
    EchoTextInkMetrics {
        id: publicationDateMetrics
        font: Theme.font(11, Theme.semiboldWeight)
        text: header.publishedDate
    }
    // Artist links require an ID and an available target route.
    // Track registry changes because plugins can register pages.
    property bool artistNavigation: false
    function refreshArtistNavigation(): void {
        header.artistNavigation = !!(header.router && header.router.available("artist-detail"));
    }
    Component.onCompleted: refreshArtistNavigation()
    Connections {
        target: header.router
        ignoreUnknownSignals: true
        function onRegistryChanged(): void {
            header.refreshArtistNavigation();
        }
    }
    additionalActions: Component {
        RowLayout {
            spacing: 8
            EchoSoftButton {
                objectName: "detailCollectButton"
                visible: header.detailContext.collectionAvailable
                text: header.detailContext.collected ? qsTr("已收藏") : qsTr("收藏")
                glyph: header.detailContext.collected ? "heart-filled" : "heart"
                accent: header.detailContext.collected
                enabled: !header.detailContext.collectionBusy
                onClicked: header.detailContext.toggleCollection()
            }

            EchoSoftButton {
                objectName: "detailShareButton"
                visible: !!header.detailContext.actions.sharing
                text: qsTr("分享")
                glyph: "share"
                onClicked: header.detailContext.actions.sharing.shareAlbum(header.shareId, String(header.info.title || ""))
            }
        }
    }
    collapsedActions: Component {
        RowLayout {
            spacing: 8
            EchoIconButton {
                objectName: "detailCollectCollapsed"
                visible: header.detailContext.collectionAvailable
                text: header.detailContext.collected ? qsTr("取消收藏") : qsTr("收藏")
                tooltipText: text
                glyph: header.detailContext.collected ? "heart-filled" : "heart"
                foreground: header.detailContext.collected ? Theme.accentText : Theme.iconMain
                enabled: !header.detailContext.collectionBusy
                onClicked: header.detailContext.toggleCollection()
            }

            EchoIconButton {
                objectName: "detailShareCollapsed"
                visible: !!header.detailContext.actions.sharing
                text: qsTr("分享")
                glyph: "share"
                onClicked: header.detailContext.actions.sharing.shareAlbum(header.shareId, String(header.info.title || ""))
            }
        }
    }
    heading: header.info.title || qsTr("专辑")
    typeLabel: "ALBUM"
    description: header.info.description || ""
    playable: header.detailContext.songs.count > 0
    loading: header.detailContext.detailLoading && !header.info.id
    coverComponent: Component {
        EchoCover {
            objectName: "albumDetailCover"
            source: header.info.cover_url || ""
            resourceActive: header.resourceActive
            cornerRadius: Theme.mediaRadius / header.coverScale
            placeholderGlyph: "vinyl"
        }
    }
    uniformMetadataRows: true
    detailsComponent: Component {
        DetailMetadataRow {
            authors: header.artistList
            avatarSource: header.artistList.length > 0 ? header.artistList[0].avatar_url || "" : ""
            secondaryText: header.publicationText
            secondaryMinimumWidth: Math.ceil(publicationDateMetrics.naturalWidth)
            labels: header.categoryTags.concat(String(header.info.language || "").length > 0 ? [String(header.info.language)] : [])
            authorLinks: header.artistNavigation
            compact: header.width < 680
            resourceActive: header.resourceActive
            rowObjectName: "albumArtistRow"
            avatarObjectName: "albumArtistAvatar"
            authorObjectName: "albumArtistLink"
            secondaryObjectName: "albumPublishLine"
            labelsObjectName: "albumCategoryLine"
            labelObjectName: "albumCategoryTag"
            labelObjectNames: header.categoryTags.length > 0 ? ["albumCategoryTag", "albumLanguage"] : ["albumLanguage"]
            onAuthorActivated: id => header.router.navigate("artist-detail", {}, id)
        }
    }
    secondaryDetailsComponent: null
    descriptionLeadingComponent: Component {
        EchoTextTagRow {
            objectName: "albumSongCount"
            alignFontBaseline: true
            text: qsTr("%1 首歌曲").arg(header.songCount)
            color: Theme.secondaryText
            font: Theme.font(11, Theme.semiboldWeight)
            lineHeight: 20
        }
    }
}
