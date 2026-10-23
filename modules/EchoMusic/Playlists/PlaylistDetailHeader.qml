pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import EchoMusic.Controls
import EchoMusic.Theme
import EchoMusic.Music

SongDetailHeader {
    id: header
    required property PlaylistDetailContext detailContext
    signal editRequested
    property string shareFallbackId: ""
    readonly property var info: detailContext.detail
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
                objectName: "playlistEditButton"
                visible: header.detailContext.editListId > 0
                text: qsTr("编辑歌单")
                glyph: "pencil"
                onClicked: header.editRequested()
            }
            EchoSoftButton {
                objectName: "detailShareButton"
                visible: !!header.detailContext.actions.sharing
                text: qsTr("分享")
                glyph: "share"
                onClicked: header.detailContext.actions.sharing.sharePlaylist(header.info, header.shareFallbackId)
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
                objectName: "playlistEditCollapsed"
                visible: header.detailContext.editListId > 0
                text: qsTr("编辑歌单")
                glyph: "pencil"
                onClicked: header.editRequested()
            }
            EchoIconButton {
                objectName: "detailShareCollapsed"
                visible: !!header.detailContext.actions.sharing
                text: qsTr("分享")
                glyph: "share"
                onClicked: header.detailContext.actions.sharing.sharePlaylist(header.info, header.shareFallbackId)
            }
        }
    }
    heading: info.title || qsTr("歌单")
    typeLabel: "PLAYLIST"
    description: info.description || ""
    playable: detailContext.songs.count > 0
    loading: detailContext.loading && !info.global_id
    coverComponent: Component {
        EchoCover {
            source: header.info.cover_url || ""
            resourceActive: header.resourceActive
            cornerRadius: Theme.mediaRadius / header.coverScale
            placeholderGlyph: "playlist-add"
        }
    }
    detailsComponent: Component {
        RowLayout {
            objectName: "playlistCreatorRow"
            spacing: 12
            RowLayout {
                spacing: 8
                EchoCover {
                    Layout.preferredWidth: 20
                    Layout.preferredHeight: 20
                    circular: true
                    source: header.info.creator_avatar_url || ""
                    placeholderGlyph: "user"
                    placeholderGlyphSize: 12
                    resourceActive: header.resourceActive
                }
                EchoTextTagRow {
                    objectName: "playlistCreatorName"
                    text: header.info.creator_name || ""
                    font: Theme.font(13, Theme.semiboldWeight)
                    color: Theme.accentText
                }
            }
            EchoTextTagRow {
                objectName: "playlistPublishedDate"
                Layout.fillWidth: true
                text: header.info.published_at ? header.info.published_at + qsTr(" 发布") : header.info.created_at_seconds ? Qt.formatDate(new Date(header.info.created_at_seconds * 1000), "yyyy-MM-dd") + qsTr(" 创建") : ""
                font: Theme.font(11, Theme.semiboldWeight)
                color: Theme.secondaryText
            }
        }
    }
    secondaryDetailsComponent: Component {
        Flow {
            objectName: "playlistMetadataTags"
            spacing: 8
            EchoButtonContent {
                objectName: "playlistSongCount"
                width: implicitWidth
                height: implicitHeight
                // Flow aligns item tops; keep each tag's full line box in the shared content.
                lineHeight: 18
                glyph: "music"
                glyphSize: 12
                spacing: 4
                text: String(header.detailContext.total)
                font: Theme.font(11, Theme.semiboldWeight)
                color: Theme.secondaryText
            }
            Repeater {
                model: String(header.info.tags || "").split(",").filter(tag => tag.trim().length > 0)
                delegate: EchoTag {
                    required property string modelData
                    objectName: "playlistMetadataTag"
                    text: modelData
                    tone: EchoTag.Accent
                }
            }
        }
    }
}
