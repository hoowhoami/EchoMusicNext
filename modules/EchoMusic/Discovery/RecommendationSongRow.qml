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
    readonly property string reason: recommendation.detail || recommendation.headline || ""
    readonly property bool current: commands.currentTrackId === entry.resourceId
    readonly property bool pending: current && commands.playbackLoading
    readonly property bool favorite: commands.favoriteRevision >= 0 && commands.isFavorite(entry.resourceId)
    implicitHeight: 82
    padding: 6
    hoverEnabled: true
    Accessible.name: entry.title + " " + entry.artistText + " " + reason
    background: Rectangle {
        radius: Theme.controlRadius
        color: row.hovered || row.visualFocus ? Theme.rowHover : "transparent"
    }
    contentItem: RowLayout {
        spacing: 10
        Item {
            objectName: "recommendationSongArtwork"
            Layout.preferredWidth: 54
            Layout.preferredHeight: 54
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
            spacing: 3
            opacity: row.entry.catalogPlayable === false ? .45 : 1
            EchoTextLine {
                objectName: "recommendationSongTitle"
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                text: row.entry.title
                font: Theme.font(13, Theme.semiboldWeight)
                color: row.current ? Theme.accentText : Theme.text
            }
            EchoTextTagRow {
                objectName: "recommendationSongArtist"
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                text: row.entry.artistText
                font: Theme.font(12, Theme.regularWeight)
                color: Theme.secondaryText
                spacing: 4
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
                objectName: "recommendationSongReason"
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                visible: row.reason.length > 0
                text: row.curator.name ? row.curator.name + qsTr(" 推荐 · ") + row.reason : row.reason
                font: Theme.font(10, Theme.regularWeight)
                color: Theme.secondaryText
            }
        }
        EchoIconButton {
            objectName: "recommendationSongFavorite"
            Layout.preferredWidth: 32
            Layout.preferredHeight: 32
            padding: 5
            glyph: row.favorite ? "heart-filled" : "heart"
            text: row.favorite ? qsTr("取消喜欢") : qsTr("喜欢")
            foreground: row.favorite ? Theme.colors.danger : Theme.secondaryText
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
