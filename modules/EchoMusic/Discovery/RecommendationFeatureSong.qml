pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Controls
import EchoMusic.Music
import EchoMusic.Theme

Item {
    id: story
    required property var entry
    required property SongCatalogActions commands
    property bool portrait: false
    property bool resourceActive: true
    readonly property var recommendation: entry.recommendation || ({})
    readonly property var curator: recommendation.curator || ({})
    readonly property string reason: recommendation.detail || recommendation.headline || ""
    readonly property bool favorite: commands.favoriteRevision >= 0 && commands.isFavorite(entry.resourceId)
    readonly property bool current: commands.currentTrackId === entry.resourceId
    readonly property bool pending: current && commands.playbackLoading
    readonly property real artworkWidth: Math.min(190, width * .42)
    readonly property real artworkHeight: Math.min(height, artworkWidth * 4 / 3)
    HoverHandler {
        id: storyHover
        blocking: false
        enabled: story.resourceActive
    }
    RowLayout {
        anchors.fill: parent
        spacing: 16
        Loader {
            Layout.preferredWidth: story.artworkWidth
            Layout.preferredHeight: story.artworkHeight
            Layout.alignment: Qt.AlignTop
            active: !story.portrait
            visible: active
            sourceComponent: artwork
        }
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumWidth: 0
            Layout.maximumHeight: story.portrait ? story.height : story.artworkHeight
            Layout.alignment: Qt.AlignTop
            spacing: 7
            RowLayout {
                Layout.fillWidth: true
                EchoTextLine {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    text: story.entry.title
                    font: Theme.font(story.portrait ? 14 : 17, Theme.semiboldWeight)
                    color: Theme.text
                }
                EchoIconButton {
                    objectName: "recommendationSongFavorite"
                    Layout.preferredWidth: 32
                    Layout.preferredHeight: 32
                    padding: 5
                    glyph: story.favorite ? "heart-filled" : "heart"
                    text: story.favorite ? qsTr("取消喜欢") : qsTr("喜欢")
                    foreground: story.favorite ? Theme.colors.dangerText : Theme.secondaryText
                    onClicked: story.commands.toggleFavorite(story.entry.resourceId)
                }
            }
            EchoTextLine {
                objectName: "recommendationFeatureArtist"
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                text: story.entry.artistText
                color: Theme.secondaryText
                font: Theme.font(12, Theme.regularWeight)
            }
            Loader {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumHeight: 60
                active: story.portrait
                visible: active
                sourceComponent: artwork
            }
            Item {
                Layout.fillHeight: true
                visible: !story.portrait && story.reason.length > 0
            }
            Label {
                objectName: "recommendationFeatureReason"
                Layout.fillWidth: true
                visible: story.reason.length > 0
                text: story.reason
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
                elide: Text.ElideRight
                maximumLineCount: story.portrait ? 1 : 4
                font: Theme.font(story.portrait ? 11 : 13, Theme.regularWeight)
                color: Theme.secondaryText
            }
            Item {
                Layout.fillHeight: true
                visible: !story.portrait
            }
            RowLayout {
                Layout.fillWidth: true
                visible: (story.curator.name || "").length > 0
                spacing: 6
                EchoCover {
                    Layout.preferredWidth: 20
                    Layout.preferredHeight: 20
                    circular: true
                    source: story.curator.avatar_url || ""
                    coverScope: "avatar"
                    placeholderGlyph: "user"
                    resourceActive: story.resourceActive
                }
                EchoTextLine {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    text: (story.curator.name || "") + qsTr(" 推荐")
                    font: Theme.font(10, Theme.regularWeight)
                    color: Theme.secondaryText
                }
            }
        }
    }
    Component {
        id: artwork
        Item {
            EchoCover {
                anchors.fill: parent
                source: story.recommendation.artwork_url || story.entry.coverUrl
                resourceActive: story.resourceActive
            }
            EchoPlaybackButton {
                objectName: "recommendationFeaturePlay"
                anchors.centerIn: parent
                width: 36
                height: 36
                padding: 10
                foreground: "white"
                playing: story.current && story.commands.playing
                loading: story.pending
                enabled: !loading
                visualContentActive: story.resourceActive
                visible: storyHover.hovered || hovered || visualFocus || story.pending
                background: Rectangle {
                    radius: Theme.controlRadius
                    color: "#66000000"
                }
                onClicked: story.commands.playOrToggle(story.entry.resourceId)
            }
        }
    }
}
