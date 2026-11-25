pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import EchoMusic.Controls
import EchoMusic.Music
import EchoMusic.Theme

Item {
    id: vip
    required property var entry
    required property SongCatalogActions commands
    required property real sleeveHeight
    property bool resourceActive: true
    activeFocusOnTab: true
    objectName: "recommendationVipSong"
    readonly property var recommendation: entry.recommendation || ({})
    readonly property var curator: recommendation.curator || ({})
    readonly property string reason: recommendation.detail || recommendation.headline || ""
    readonly property bool favorite: commands.favoriteRevision >= 0 && commands.isFavorite(entry.resourceId)
    readonly property bool current: commands.currentTrackId === entry.resourceId
    readonly property bool pending: current && commands.playbackLoading
    HoverHandler {
        id: vipHover
        blocking: false
        enabled: vip.resourceActive
    }
    ColumnLayout {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        spacing: 8
        Item {
            id: sleeve
            objectName: "recommendationVipSleeve"
            Layout.fillWidth: true
            Layout.minimumHeight: vip.sleeveHeight
            Layout.preferredHeight: vip.sleeveHeight
            Layout.maximumHeight: vip.sleeveHeight
            Rectangle {
                objectName: "recommendationVipSurface"
                anchors.fill: parent
                radius: Theme.cardRadius
                color: Theme.mix(Theme.mix(Theme.main, Theme.text, Theme.dark ? .06 : .045), Theme.accent, Theme.dark ? .12 : .07)
            }
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 16
                spacing: 4
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 4
                    EchoTextLine {
                        objectName: "recommendationVipTitle"
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        text: vip.entry.title.replace(/\s+/g, " ").trim()
                        font: Theme.font(14, Theme.mediumWeight)
                        color: Theme.text
                    }
                    EchoIconButton {
                        objectName: "recommendationSongFavorite"
                        Layout.preferredWidth: 32
                        Layout.preferredHeight: 32
                        padding: 5
                        glyph: vip.favorite ? "heart-filled" : "heart"
                        text: vip.favorite ? qsTr("取消喜欢") : qsTr("喜欢")
                        foreground: vip.favorite ? Theme.colors.danger : Theme.secondaryText
                        onClicked: vip.commands.toggleFavorite(vip.entry.resourceId)
                    }
                }
                EchoTextLine {
                    objectName: "recommendationVipArtist"
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    text: vip.entry.artistText.replace(/\s+/g, " ").trim()
                    font: Theme.font(12, Theme.regularWeight)
                    color: Theme.secondaryText
                }
                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.topMargin: 8
                    Item {
                        id: album
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.top: parent.top
                        width: Math.max(0, Math.min(parent.width - 8, parent.height))
                        height: width
                        // A paper edge gives the album a sleeve silhouette. Only
                        // the front artwork owns an image/decoded texture.
                        Rectangle {
                            x: 7
                            y: 5
                            width: album.width
                            height: Math.max(0, album.height - 10)
                            radius: 2
                            color: Theme.withAlpha(Theme.accent, Theme.dark ? .14 : .08)
                        }
                        EchoCover {
                            objectName: "recommendationVipArtwork"
                            anchors.fill: parent
                            cornerRadius: 2
                            source: vip.recommendation.artwork_url || vip.entry.coverUrl
                            resourceActive: vip.resourceActive
                        }
                        EchoPlaybackButton {
                            objectName: "recommendationFeaturePlay"
                            anchors.centerIn: parent
                            width: 36
                            height: 36
                            padding: 10
                            foreground: "white"
                            playing: vip.current && vip.commands.playing
                            loading: vip.pending
                            enabled: !loading
                            visualContentActive: vip.resourceActive
                            visible: vip.resourceActive && (vipHover.hovered || vip.activeFocus || hovered || visualFocus || vip.pending)
                            background: Rectangle {
                                radius: Theme.controlRadius
                                color: "#66000000"
                            }
                            onClicked: vip.commands.playOrToggle(vip.entry.resourceId)
                        }
                    }
                }
            }
        }
        ColumnLayout {
            objectName: "recommendationVipCaption"
            Layout.fillWidth: true
            spacing: 4
            EchoTextLine {
                objectName: "recommendationVipReason"
                Layout.minimumHeight: lineHeight
                Layout.preferredHeight: lineHeight
                Layout.maximumHeight: lineHeight
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                text: vip.reason.replace(/\s+/g, " ").trim()
                font: Theme.font(12, Theme.regularWeight)
                color: Theme.text
            }
            EchoTextLine {
                objectName: "recommendationVipCurator"
                Layout.minimumHeight: lineHeight
                Layout.preferredHeight: lineHeight
                Layout.maximumHeight: lineHeight
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                text: (vip.curator.name || "").length > 0 ? vip.curator.name.replace(/\s+/g, " ").trim() + qsTr(" 推荐") : ""
                font: Theme.font(10, Theme.regularWeight)
                color: Theme.secondaryText
            }
        }
    }
}
