pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import EchoMusic.Controls
import EchoMusic.Theme

GridLayout {
    id: rankings
    required property var stats
    property bool resourceActive: true
    property bool animate: true
    property bool interactive: true
    signal playRequested(string id)
    readonly property bool stacked: width < 700 * Theme.store.globalFontSizeScale / 100
    readonly property int pendingImages: {
        let count = 0;
        for (let i = 0; i < songs.count; ++i) {
            const tile = songs.itemAt(i) as SongTile;
            if (tile && tile.coverStatus === Image.Loading)
                ++count;
        }
        return count;
    }
    objectName: "historyRankingGrid"
    columns: stacked ? 1 : 2
    columnSpacing: 40
    rowSpacing: 32
    HistoryStatsPanel {
        id: artistPanel
        objectName: "historyArtistCard"
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        Layout.preferredWidth: 1
        Layout.alignment: Qt.AlignTop
        flat: true
        heading: qsTr("偏爱的声音")
        description: qsTr("从歌手的声音，读懂你的偏好")
        resourceActive: rankings.resourceActive
        animate: rankings.animate
        order: 2
        Repeater {
            model: rankings.stats.topArtists || []
            delegate: ColumnLayout {
                id: artist
                required property var modelData
                required property int index
                objectName: "historyTopArtistRow"
                Layout.fillWidth: true
                spacing: 10
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10
                    EchoTextTagRow {
                        Layout.preferredWidth: 22
                        text: String(artist.index + 1).padStart(2, "0")
                        color: artist.index === 0 ? Theme.accentText : Theme.secondaryText
                        font: Theme.font(11, Theme.mediumWeight)
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        spacing: 3
                        EchoTextTagRow {
                            Layout.fillWidth: true
                            text: artist.modelData.name
                            font: Theme.font(artist.index === 0 ? 24 : 15, artist.index === 0 ? Theme.boldWeight : Theme.mediumWeight)
                            lineHeight: font.pixelSize * 1.4
                            color: artist.index === 0 ? Theme.accentText : Theme.text
                        }
                        EchoTextTagRow {
                            visible: artist.index === 0
                            text: qsTr("%1 首歌，陪你听了 %2 次").arg(artist.modelData.songs).arg(artist.modelData.plays)
                            font: Theme.font(10, Theme.regularWeight)
                            color: Theme.secondaryText
                        }
                    }
                    EchoTextTagRow {
                        objectName: "historyArtistCount"
                        visible: artist.index !== 0
                        text: qsTr("%1 次").arg(artist.modelData.plays)
                        font: Theme.font(10, Theme.mediumWeight)
                        color: Theme.secondaryText
                    }
                }
                EchoDivider {
                    Layout.fillWidth: true
                    lineColor: Theme.subtleBorder
                }
            }
        }
    }
    HistoryStatsPanel {
        id: songsPanel
        objectName: "historyTopSongsCard"
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        Layout.preferredWidth: 1.7
        Layout.alignment: Qt.AlignTop
        flat: true
        heading: qsTr("你的私人重播册")
        description: qsTr("那些一次次回到耳边的歌")
        resourceActive: rankings.resourceActive
        animate: rankings.animate
        order: 3
        GridLayout {
            id: gallery
            Layout.fillWidth: true
            columns: width >= 330 * Theme.store.globalFontSizeScale / 100 ? 3 : 2
            columnSpacing: 16
            rowSpacing: 20
            Repeater {
                id: songs
                model: rankings.stats.topSongs || []
                delegate: SongTile {}
            }
        }
    }
    component SongTile: EchoActionButton {
        id: song
        required property var modelData
        required property int index
        readonly property int coverStatus: cover.status
        objectName: "historyTopSongRow"
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        Layout.preferredWidth: 1
        padding: 10
        enabled: rankings.interactive && String(modelData.id || "").length > 0
        text: qsTr("播放 %1").arg(modelData.title || qsTr("未知歌曲"))
        Accessible.name: text
        onClicked: rankings.playRequested(String(modelData.id))
        background: Rectangle {
            radius: Theme.cardRadius
            color: song.hovered ? Theme.rowHover : Theme.hotCard
        }
        contentItem: ColumnLayout {
            spacing: 7
            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: width
                EchoCover {
                    id: cover
                    anchors.fill: parent
                    source: song.modelData.cover || ""
                    cornerRadius: Theme.mediaRadius
                    resourceActive: rankings.resourceActive
                    retainWhileLoading: !rankings.animate
                }
                Rectangle {
                    x: 6
                    y: 6
                    width: rank.implicitWidth + 12
                    height: 22
                    radius: 4
                    color: "#bb17191d"
                    EchoTextTagRow {
                        id: rank
                        anchors.centerIn: parent
                        text: String(song.index + 1).padStart(2, "0")
                        font: Theme.font(10, Theme.semiboldWeight)
                        color: "white"
                    }
                }
                Rectangle {
                    anchors.centerIn: parent
                    width: 32
                    height: 32
                    radius: 16
                    visible: song.hovered || song.visualFocus
                    color: "#bb17191d"
                    EchoIcon {
                        anchors.centerIn: parent
                        width: 16
                        height: 16
                        name: "play"
                        color: "white"
                        resourceActive: rankings.resourceActive && parent.visible
                    }
                }
            }
            EchoTextTagRow {
                Layout.fillWidth: true
                text: song.modelData.title || qsTr("未知歌曲")
                font: Theme.font(12, Theme.semiboldWeight)
            }
            EchoTextTagRow {
                Layout.fillWidth: true
                text: song.modelData.artist || qsTr("未知歌手")
                color: Theme.secondaryText
                font: Theme.font(10, Theme.regularWeight)
            }
            EchoTextTagRow {
                objectName: "historySongCountValue"
                text: qsTr("%1 次播放").arg(song.modelData.plays)
                font: Theme.font(10, Theme.mediumWeight)
                color: Theme.accentText
            }
        }
    }
}
