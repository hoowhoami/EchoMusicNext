pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
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
    readonly property var categoryTags: [String(info.category || "")].filter(value => value.length > 0)
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
            placeholderGlyphSize: 32
        }
    }
    detailsComponent: Component {
        Item {
            implicitHeight: metadataRow.implicitHeight
            RowLayout {
                id: metadataRow
                width: Math.min(parent.width, implicitWidth)
                height: implicitHeight
                objectName: "albumArtistRow"
                spacing: header.width < 680 ? 4 : 12
                RowLayout {
                    Layout.fillWidth: true
                    Layout.maximumWidth: implicitWidth
                    spacing: 8
                    EchoCover {
                        objectName: "albumArtistAvatar"
                        Layout.preferredWidth: 20
                        Layout.preferredHeight: 20
                        circular: true
                        source: header.artistList.length > 0 ? header.artistList[0].avatar_url || "" : ""
                        placeholderGlyph: "user"
                        placeholderGlyphSize: 12
                        resourceActive: header.resourceActive
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: header.width < 680 ? 2 : 4
                        Repeater {
                            model: header.artistList
                            delegate: RowLayout {
                                id: artistDelegate
                                required property var modelData
                                required property int index
                                Layout.fillWidth: true
                                spacing: header.width < 680 ? 2 : 4
                                Button {
                                    id: artistButton
                                    objectName: "albumArtistLink"
                                    Layout.fillWidth: true
                                    Layout.minimumWidth: artistCaption.font.pixelSize * 1.5
                                    flat: true
                                    padding: 0
                                    background: null
                                    enabled: header.artistNavigation && artistDelegate.modelData.id !== undefined && artistDelegate.modelData.id !== null && String(artistDelegate.modelData.id).length > 0 && String(artistDelegate.modelData.id) !== "0"
                                    Accessible.name: String(artistDelegate.modelData.name || "")
                                    onClicked: header.router.navigate("artist-detail", {}, String(artistDelegate.modelData.id))
                                    contentItem: EchoTextTagRow {
                                        id: artistCaption
                                        text: String(artistDelegate.modelData.name || "")
                                        font: Theme.font(13, Theme.semiboldWeight)
                                        color: Theme.accentText
                                    }
                                    HoverHandler {
                                        enabled: artistButton.enabled
                                        cursorShape: Qt.PointingHandCursor
                                    }
                                }
                                EchoTextTagRow {
                                    visible: artistDelegate.index < header.artistList.length - 1
                                    text: "/"
                                    font: Theme.font(13, Theme.semiboldWeight)
                                    color: Theme.secondaryText
                                }
                            }
                        }
                    }
                }
                EchoTextTagRow {
                    objectName: "albumPublishLine"
                    visible: header.publishedDate.length > 0
                    text: header.publishedDate + qsTr(" 发布")
                    color: Theme.secondaryText
                    font: Theme.font(11, Theme.semiboldWeight)
                }
                RowLayout {
                    objectName: "albumCategoryLine"
                    spacing: header.width < 680 ? 4 : 8
                    Repeater {
                        model: header.categoryTags
                        delegate: EchoTag {
                            required property string modelData
                            objectName: "albumCategoryTag"
                            text: modelData
                            tone: EchoTag.Accent
                        }
                    }
                    EchoTag {
                        objectName: "albumLanguage"
                        visible: text.length > 0
                        text: String(header.info.language || "")
                        tone: EchoTag.Accent
                    }
                }
            }
        }
    }
    secondaryDetailsComponent: null
    descriptionLeadingComponent: Component {
        EchoTextTagRow {
            objectName: "albumSongCount"
            text: qsTr("%1 首歌曲").arg(header.songCount)
            color: Theme.secondaryText
            font: Theme.font(11, Theme.semiboldWeight)
            lineHeight: 20
        }
    }
}
