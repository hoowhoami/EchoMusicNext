pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Controls
import EchoMusic.Theme
import EchoMusic.Music

SongDetailHeader {
    id: header
    required property PlaylistDetailContext detailContext
    signal editRequested
    signal removeRequested
    property bool removalAvailable: false
    property bool removalBusy: false
    property string shareFallbackId: ""
    readonly property var info: detailContext.detail
    // Five labeled actions need room beside the expanded artwork and metadata.
    compactExpandedActions: width < 680
    additionalActions: Component {
        RowLayout {
            spacing: 8
            EchoSoftButton {
                objectName: "detailCollectButton"
                display: header.compactExpandedActions ? AbstractButton.IconOnly : AbstractButton.TextBesideIcon
                visible: header.detailContext.collectionAvailable
                text: header.detailContext.collected ? qsTr("已收藏") : qsTr("收藏")
                glyph: header.detailContext.collected ? "heart-filled" : "heart"
                accent: header.detailContext.collected
                enabled: !header.detailContext.collectionBusy
                onClicked: header.detailContext.toggleCollection()
            }

            EchoSoftButton {
                objectName: "playlistEditButton"
                display: header.compactExpandedActions ? AbstractButton.IconOnly : AbstractButton.TextBesideIcon
                visible: header.detailContext.editListId > 0
                text: qsTr("编辑")
                glyph: "pencil"
                onClicked: header.editRequested()
            }
            EchoSoftButton {
                objectName: "detailShareButton"
                display: header.compactExpandedActions ? AbstractButton.IconOnly : AbstractButton.TextBesideIcon
                visible: !!header.detailContext.actions.sharing
                text: qsTr("分享")
                glyph: "share"
                onClicked: header.detailContext.actions.sharing.sharePlaylist(header.info, header.shareFallbackId)
            }
            EchoSoftButton {
                objectName: "playlistRemoveButton"
                display: header.compactExpandedActions ? AbstractButton.IconOnly : AbstractButton.TextBesideIcon
                visible: header.removalAvailable
                enabled: !header.removalBusy
                text: qsTr("删除")
                glyph: "trash"
                danger: true
                onClicked: header.removeRequested()
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
                text: qsTr("编辑")
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
            EchoIconButton {
                objectName: "playlistRemoveCollapsed"
                visible: header.removalAvailable
                enabled: !header.removalBusy
                text: qsTr("删除歌单")
                glyph: "trash"
                foreground: Theme.colors.dangerText
                onClicked: header.removeRequested()
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
    uniformMetadataRows: true
    detailsComponent: Component {
        DetailMetadataRow {
            authors: [
                {
                    name: header.info.creator_name || ""
                }
            ]
            avatarSource: header.info.creator_avatar_url || ""
            secondaryText: header.info.published_at ? String(header.info.published_at).split(/[ T]/)[0] + qsTr(" 发布") : header.info.created_at_seconds ? Qt.formatDate(new Date(header.info.created_at_seconds * 1000), "yyyy-MM-dd") + qsTr(" 创建") : ""
            labels: String(header.info.tags || "").split(",").filter(tag => tag.trim().length > 0)
            compact: header.width < 680
            resourceActive: header.resourceActive
            rowObjectName: "playlistCreatorRow"
            avatarObjectName: "playlistCreatorAvatar"
            authorObjectName: "playlistCreatorName"
            secondaryObjectName: "playlistPublishedDate"
            labelsObjectName: "playlistMetadataTags"
            labelObjectName: "playlistMetadataTag"
        }
    }
    secondaryDetailsComponent: null
    descriptionLeadingComponent: Component {
        RowLayout {
            // The count's fixed line box starts at the group's origin; do not
            // feed an arranged child position back into baseline alignment.
            baselineOffset: countLabel.baselineOffset
            spacing: 4
            EchoTextTagRow {
                id: countLabel
                objectName: "playlistSongCount"
                alignFontBaseline: true
                lineHeight: 20
                text: qsTr("%1 首歌曲").arg(header.detailContext.total)
                font: Theme.font(11, Theme.semiboldWeight)
                color: Theme.secondaryText
            }
            Item {
                visible: header.detailContext.filteredInvalidCount > 0
                implicitWidth: 18
                implicitHeight: countLabel.implicitHeight
                // The count shares its font baseline with the introduction;
                // its ink center can differ from the surrounding line box.
                EchoIconButton {
                    objectName: "playlistFilterInfo"
                    y: countLabel.firstLineInkCenter - height / 2
                    width: 18
                    height: 18
                    visualContentActive: header.resourceActive && visible
                    glyph: "info-circle"
                    hoverBackground: false
                    text: qsTr("查看无效歌曲过滤说明")
                    tooltipText: qsTr("当前列表已过滤 %1 首无效歌曲\n通常是因为歌曲信息缺失无法参与播放").arg(header.detailContext.filteredInvalidCount)
                    padding: 2
                    foreground: hovered || visualFocus ? "#d97706" : "#f59e0b"
                }
            }
        }
    }
}
