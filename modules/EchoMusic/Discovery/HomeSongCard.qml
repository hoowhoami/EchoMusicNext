pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Theme
import EchoMusic.Controls

ItemDelegate {
    id: card
    objectName: "homeSongCard"
    required property var entry
    required property DiscoveryService service
    property bool resourceActive: true
    readonly property bool currentSong: service.playback.trackId === entry.resourceId
    readonly property bool pendingPlay: currentSong && service.playback.loading
    implicitHeight: 62
    padding: 8
    hoverEnabled: true
    Accessible.name: entry.title + " " + entry.artistText
    background: Rectangle {
        radius: Theme.controlRadius
        color: card.hovered || card.visualFocus ? Theme.rowHover : Theme.hotCard
    }
    contentItem: RowLayout {
        spacing: 10
        Item {
            Layout.preferredWidth: 46
            Layout.preferredHeight: 46
            EchoCover {
                anchors.fill: parent
                source: card.entry.coverUrl
                resourceActive: card.resourceActive
            }
            EchoPlaybackButton {
                objectName: "homeSongPlay"
                anchors.fill: parent
                padding: 16
                playing: card.currentSong && card.service.playback.playing
                loading: card.pendingPlay
                enabled: !loading
                visualContentActive: card.resourceActive
                foreground: card.currentSong || hoverHighlighted || visualFocus ? Theme.accentText : "white"
                hoverBackground: false
                visible: card.hovered || card.visualFocus || hovered || card.pendingPlay || card.currentSong && card.service.playback.playing
                background: Rectangle {
                    radius: Theme.mediaRadius
                    color: "#66000000"
                }
                onClicked: {
                    if (card.pendingPlay)
                        return;
                    if (card.currentSong) {
                        if (card.service.playback.playing)
                            card.service.playback.pause();
                        else
                            card.service.playback.play();
                    } else
                        card.service.play(card.service.styleSongs, card.entry.resourceId, false);
                }
            }
        }
        ColumnLayout {
            opacity: card.entry.catalogPlayable === false ? .45 : 1
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            spacing: 2
            EchoTextTagRow {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                text: card.entry.title
                font: Theme.font(13, Theme.semiboldWeight)
                color: card.currentSong ? Theme.accentText : Theme.text
                spacing: 6
                Repeater {
                    model: card.entry.privilegeTags || []
                    delegate: EchoTag {
                        required property var modelData
                        text: modelData.label
                        tone: EchoTag.Custom
                        customTone: modelData.color
                    }
                }
                EchoTag {
                    visible: (card.entry.qualityLabel || "").length > 0
                    text: (card.entry.qualityLabel || "")
                    tone: EchoTag.Custom
                    customTone: "#06B6D4"
                }
            }
            EchoTextTagRow {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                text: card.entry.artistText
                color: card.currentSong ? Theme.accentText : Theme.secondaryText
                font: Theme.font(12, Theme.mediumWeight)
            }
        }
    }
    TapHandler {
        onDoubleTapped: if (!card.pendingPlay)
            card.service.play(card.service.styleSongs, card.entry.resourceId, false)
    }
}
