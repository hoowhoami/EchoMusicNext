pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import EchoMusic.Controls
import EchoMusic.Theme
import EchoMusic.Music

// Artist header with a round avatar and artist statistics.
// The loading cover uses the same circular shape.
SongDetailHeader {
    id: header
    required property ArtistDetailContext detailContext
    property string shareId: ""
    readonly property var info: detailContext.detail
    readonly property int songCount: detailContext.songs.totalCount
    readonly property int albumCount: Number(info.album_count || 0)
    readonly property int mvCount: Number(info.mv_count || 0)
    readonly property int fansCount: Number(info.fans || 0)
    readonly property string countsLine: {
        const parts = [qsTr("%1 首歌曲").arg(header.songCount)];
        if (header.albumCount > 0)
            parts.push(qsTr("%1 张专辑").arg(header.albumCount));
        if (header.mvCount > 0)
            parts.push(qsTr("%1 MV").arg(header.mvCount));
        return parts.join(" • ");
    }
    readonly property string fansText: header.fansCount >= 10000 ? (header.fansCount / 10000).toFixed(1).replace(/\.0$/, "") + qsTr("万") : String(header.fansCount)
    readonly property string fansLine: {
        const parts = [];
        if (header.fansCount > 0)
            parts.push(qsTr("%1 粉丝").arg(header.fansText));
        if (String(header.info.birthday || "").length > 0)
            parts.push("🎂 " + header.info.birthday);
        return parts.join("  ");
    }
    additionalActions: Component {
        RowLayout {
            visible: header.detailContext.interactionAvailable || !!header.detailContext.actions.sharing
            spacing: 8
            EchoSoftButton {
                objectName: "artistFollowButton"
                visible: header.detailContext.interactionAvailable
                text: header.detailContext.followPending ? (header.detailContext.followed ? qsTr("取消中...") : qsTr("关注中...")) : header.detailContext.followed ? qsTr("已关注") : qsTr("关注")
                glyph: header.detailContext.followed ? "heart-filled" : "heart"
                accent: header.detailContext.followed
                enabled: !header.detailContext.followBusy
                onClicked: header.detailContext.toggleFollow()
            }
            EchoSoftButton {
                objectName: "artistBlockButton"
                visible: header.detailContext.interactionAvailable
                text: header.detailContext.blockBusy ? (header.detailContext.blocked ? qsTr("取消中...") : qsTr("屏蔽中...")) : header.detailContext.blocked ? qsTr("取消屏蔽") : qsTr("屏蔽歌手")
                glyph: "eye-off"
                accent: header.detailContext.blocked
                enabled: !header.detailContext.blockBusy
                onClicked: header.detailContext.toggleBlock()
            }
            EchoSoftButton {
                objectName: "detailShareButton"
                visible: !!header.detailContext.actions.sharing
                text: qsTr("分享")
                glyph: "share"
                onClicked: header.detailContext.actions.sharing.shareArtist(header.shareId, String(header.info.name || ""))
            }
        }
    }
    collapsedActions: Component {
        RowLayout {
            visible: header.detailContext.interactionAvailable || !!header.detailContext.actions.sharing
            spacing: 8
            EchoIconButton {
                objectName: "artistFollowCollapsed"
                visible: header.detailContext.interactionAvailable
                text: header.detailContext.followed ? qsTr("取消关注") : qsTr("关注")
                tooltipText: text
                glyph: header.detailContext.followed ? "heart-filled" : "heart"
                foreground: header.detailContext.followed ? Theme.accentText : Theme.iconMain
                enabled: !header.detailContext.followBusy
                onClicked: header.detailContext.toggleFollow()
            }
            EchoIconButton {
                objectName: "artistBlockCollapsed"
                visible: header.detailContext.interactionAvailable
                text: header.detailContext.blockBusy ? (header.detailContext.blocked ? qsTr("取消中...") : qsTr("屏蔽中...")) : header.detailContext.blocked ? qsTr("取消屏蔽") : qsTr("屏蔽歌手")
                tooltipText: text
                glyph: "eye-off"
                foreground: header.detailContext.blocked ? Theme.accentText : Theme.iconMain
                enabled: !header.detailContext.blockBusy
                onClicked: header.detailContext.toggleBlock()
            }
            EchoIconButton {
                objectName: "detailShareCollapsed"
                visible: !!header.detailContext.actions.sharing
                text: qsTr("分享")
                glyph: "share"
                onClicked: header.detailContext.actions.sharing.shareArtist(header.shareId, String(header.info.name || ""))
            }
        }
    }
    heading: header.info.name || qsTr("歌手")
    typeLabel: "ARTIST"
    coverRound: true
    description: header.info.description || ""
    playable: header.detailContext.songs.count > 0
    loading: header.detailContext.detailLoading && !header.info.id
    coverComponent: Component {
        EchoCover {
            objectName: "artistDetailCover"
            source: header.info.avatar_url || ""
            circular: true
            resourceActive: header.resourceActive
            placeholderGlyph: "user"
            placeholderGlyphSize: 32
        }
    }
    uniformMetadataRows: true
    detailsComponent: Component {
        DetailMetadataRow {
            resourceActive: header.resourceActive
            compact: header.width < 680
            secondaryText: header.fansLine
            secondaryObjectName: "artistFansLine"
            rowObjectName: "artistFactsRow"
            leadingText: header.countsLine
            leadingObjectName: "artistCountsLine"
        }
    }
    secondaryDetailsComponent: null
}
