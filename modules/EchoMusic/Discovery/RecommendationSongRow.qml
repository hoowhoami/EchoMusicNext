pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Controls
import EchoMusic.Music
import EchoMusic.Theme

ItemDelegate {
    id: row
    required property var entry
    required property SongCatalogActions commands
    property bool resourceActive: true
    property bool customPlayback: false
    signal playRequested
    readonly property var recommendation: entry.recommendation || ({})
    readonly property var curator: recommendation.curator || ({})
    readonly property string reason: String(recommendation.detail || recommendation.headline || "").replace(/\s+/g, " ").trim()
    readonly property bool current: commands.currentTrackId === entry.resourceId
    readonly property bool pending: current && commands.playbackLoading
    readonly property bool favorite: commands.favoriteRevision >= 0 && commands.isFavorite(entry.resourceId)
    readonly property real textHeight: songTitle.implicitHeight + songArtist.implicitHeight + (songReason.visible ? songReason.implicitHeight : 0)
    readonly property real artworkSide: Math.max(54, textHeight)
    implicitHeight: Math.max(82, artworkSide + 12)
    padding: 6
    hoverEnabled: true
    Accessible.name: entry.title + " " + entry.artistText + " " + reason
    background: Rectangle {
        radius: Theme.controlRadius
        color: row.hovered || row.visualFocus ? Theme.rowHover : "transparent"
    }
    clip: true
    contentItem: RowLayout {
        spacing: 10
        Item {
            objectName: "recommendationSongArtwork"
            Layout.preferredWidth: row.artworkSide
            Layout.preferredHeight: row.artworkSide
            EchoCover {
                anchors.fill: parent
                source: row.entry.coverUrl
                resourceActive: row.resourceActive
            }
            EchoPlaybackButton {
                anchors.fill: parent
                padding: 18
                opacity: row.hovered || row.visualFocus || row.pending ? 1 : 0
                visible: opacity > 0
                playing: row.current && row.commands.playing
                loading: row.pending
                enabled: !loading
                visualContentActive: row.resourceActive
                foreground: "white"
                background: Rectangle {
                    radius: Theme.mediaRadius
                    color: "#66000000"
                }
                onClicked: row.customPlayback ? row.playRequested() : row.commands.playOrToggle(row.entry.resourceId)
            }
        }
        ColumnLayout {
            objectName: "recommendationSongInfo"
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            Layout.alignment: Qt.AlignVCenter
            Layout.preferredHeight: songReason.visible ? row.artworkSide : implicitHeight
            Layout.minimumHeight: Layout.preferredHeight
            Layout.maximumHeight: Layout.preferredHeight
            spacing: songReason.visible ? Math.max(0, (row.artworkSide - row.textHeight) / 2) : 3
            opacity: row.entry.catalogPlayable === false ? .45 : 1
            EchoTextTagRow {
                id: songTitle
                objectName: "recommendationSongTitle"
                Layout.preferredHeight: implicitHeight
                Layout.minimumHeight: Layout.preferredHeight
                Layout.maximumHeight: Layout.preferredHeight
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                text: String(row.entry.title || "").replace(/\s+/g, " ").trim()
                font: Theme.font(13, Theme.semiboldWeight)
                color: row.current ? Theme.accentText : Theme.text
                spacing: 4
                wrapTags: false
                clip: true
                Repeater {
                    model: row.entry.privilegeTags || []
                    delegate: EchoTag {
                        objectName: "recommendationSongTag"
                        required property var modelData
                        text: modelData.label
                        tone: EchoTag.Custom
                        customTone: modelData.color
                    }
                }
                EchoTag {
                    objectName: "recommendationSongTag"
                    visible: (row.entry.qualityLabel || "").length > 0
                    text: row.entry.qualityLabel || ""
                    tone: EchoTag.Custom
                    customTone: "#06B6D4"
                }
            }
            EchoTextLine {
                id: songArtist
                objectName: "recommendationSongArtist"
                Layout.preferredHeight: lineHeight
                Layout.minimumHeight: Layout.preferredHeight
                Layout.maximumHeight: Layout.preferredHeight
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                text: String(row.entry.artistText || "").replace(/\s+/g, " ").trim()
                font: Theme.font(12, Theme.regularWeight)
                color: Theme.secondaryText
            }
            EchoTextLine {
                id: songReason
                objectName: "recommendationSongReason"
                Layout.preferredHeight: lineHeight
                Layout.minimumHeight: Layout.preferredHeight
                Layout.maximumHeight: Layout.preferredHeight
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                visible: row.reason.length > 0
                text: row.curator.name ? String(row.curator.name).replace(/\s+/g, " ").trim() + qsTr(" 推荐 · ") + row.reason : row.reason
                font: Theme.font(10, Theme.regularWeight)
                color: Theme.secondaryText
            }
        }
        EchoIconButton {
            objectName: "recommendationSongFavorite"
            Layout.preferredWidth: 32
            Layout.preferredHeight: 32
            padding: 5
            hoverBackground: false
            scaleFeedback: false
            glyph: row.favorite ? "heart-filled" : "heart-light"
            text: row.favorite ? qsTr("取消收藏") : qsTr("收藏")
            foreground: hovered ? "#dc2626" : Theme.colors.danger
            onClicked: row.commands.toggleFavorite(row.entry.resourceId)
        }
    }
    TapHandler {
        onDoubleTapped: if (!row.pending) {
            if (row.customPlayback)
                row.playRequested();
            else
                row.commands.play(row.entry.resourceId, false);
        }
    }
}
