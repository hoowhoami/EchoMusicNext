pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import EchoMusic.Controls
import EchoMusic.Theme

// An editorial portrait led by the listener's actual records and artwork.
ColumnLayout {
    id: overview
    required property var stats
    property bool resourceActive: true
    property bool animate: true
    property bool interactive: true
    signal playRequested(string id)
    readonly property bool narrow: width < 760 * Theme.store.globalFontSizeScale / 100
    readonly property bool compact: width < 470 * Theme.store.globalFontSizeScale / 100
    readonly property var favorite: (stats.topSongs || [])[0] || ({})
    readonly property var album: stats.topAlbum || ({})
    readonly property int pendingImages: (songCover.status === Image.Loading ? 1 : 0) + (albumCover.status === Image.Loading ? 1 : 0)
    spacing: 22
    RowLayout {
        Layout.fillWidth: true
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 4
            EchoTextTagRow {
                text: "LISTENING JOURNAL"
                font: Theme.spacedFont(10, Theme.semiboldWeight, 2)
                color: Theme.accentText
            }
            EchoTextTagRow {
                Layout.fillWidth: true
                text: qsTr("音乐里的你")
                font: Theme.font(30, Theme.boldWeight)
                lineHeight: font.pixelSize * 1.3
            }
        }
        ColumnLayout {
            Layout.alignment: Qt.AlignBottom
            spacing: 3
            EchoTextTagRow {
                objectName: "historyTotalPlays"
                Layout.alignment: Qt.AlignRight
                text: qsTr("%1 次播放").arg(overview.stats.totalPlaysLabel || "0")
                font: Theme.font(overview.compact ? 13 : 19, Theme.semiboldWeight)
            }
            EchoTextTagRow {
                visible: !overview.compact
                text: overview.stats.dateRangeLabel || ""
                color: Theme.secondaryText
                font: Theme.font(10, Theme.regularWeight)
            }
        }
    }
    GridLayout {
        id: portrait
        Layout.fillWidth: true
        visible: Number(overview.stats.totalSongs || 0) > 0
        columns: overview.narrow ? 1 : 2
        columnSpacing: 18
        rowSpacing: 18
        Rectangle {
            id: songStory
            objectName: "historyStatsHero"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumWidth: 0
            Layout.preferredWidth: 1.65
            implicitHeight: songBody.implicitHeight + 48
            radius: Theme.dialogRadius
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop {
                    position: 0
                    color: Theme.mix(Theme.mutedSurface, Theme.accent, .18)
                }
                GradientStop {
                    position: 1
                    color: Theme.mix(Theme.mutedSurface, Theme.accent, .035)
                }
            }
            ColumnLayout {
                id: songBody
                x: 24
                y: 24
                width: Math.max(0, songStory.width - 48)
                spacing: 20
                RowLayout {
                    Layout.fillWidth: true
                    EchoTextTagRow {
                        Layout.fillWidth: true
                        text: qsTr("01 / 你的单曲循环")
                        color: Theme.accentText
                        font: Theme.spacedFont(11, Theme.semiboldWeight, .5)
                    }
                    EchoIcon {
                        Layout.preferredWidth: 18
                        Layout.preferredHeight: 18
                        name: "repeat"
                        color: Theme.accentText
                        resourceActive: overview.resourceActive
                    }
                }
                GridLayout {
                    Layout.fillWidth: true
                    columns: overview.compact ? 1 : 2
                    columnSpacing: 22
                    rowSpacing: 18
                    EchoCover {
                        id: songCover
                        objectName: "historyFavoriteArtwork"
                        Layout.preferredWidth: overview.compact ? 132 : Math.min(160, songStory.width * .29)
                        Layout.preferredHeight: Layout.preferredWidth
                        Layout.alignment: Qt.AlignLeft
                        source: overview.favorite.cover || ""
                        cornerRadius: Theme.mediaRadius
                        resourceActive: overview.resourceActive
                        retainWhileLoading: !overview.animate
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        spacing: 8
                        EchoTextTagRow {
                            Layout.fillWidth: true
                            text: overview.favorite.title || qsTr("未知歌曲")
                            wrap: true
                            font: Theme.font(24, Theme.boldWeight)
                            lineHeight: font.pixelSize * 1.3
                        }
                        EchoTextTagRow {
                            Layout.fillWidth: true
                            text: overview.favorite.artist || qsTr("未知歌手")
                            color: Theme.secondaryText
                            font: Theme.font(12, Theme.mediumWeight)
                        }
                        EchoTextTagRow {
                            Layout.fillWidth: true
                            text: qsTr("已经重逢 %1 次").arg(overview.favorite.plays || 0)
                            color: Theme.accentText
                            font: Theme.font(12, Theme.semiboldWeight)
                        }
                        EchoSoftButton {
                            objectName: "historyFavoritePlay"
                            visible: overview.interactive
                            Layout.topMargin: 6
                            text: qsTr("再听一次")
                            glyph: "play"
                            accent: true
                            enabled: String(overview.favorite.id || "").length > 0
                            onClicked: overview.playRequested(String(overview.favorite.id))
                        }
                    }
                }
                EchoTextTagRow {
                    Layout.fillWidth: true
                    text: qsTr("有些旋律，一响起就舍不得切走。")
                    font: Theme.font(11, Theme.regularWeight)
                    color: Theme.secondaryText
                    wrap: true
                }
            }
            EchoEnter {
                target: songStory
                active: overview.resourceActive
                duration: overview.animate ? Theme.normalDuration : 0
            }
        }
        Rectangle {
            id: albumStory
            objectName: "historyAlbumStory"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumWidth: 0
            Layout.preferredWidth: 1
            implicitHeight: albumBody.implicitHeight + 40
            radius: Theme.dialogRadius
            color: Theme.mix(Theme.mutedSurface, "#c69b69", Theme.dark ? .09 : .10)
            ColumnLayout {
                id: albumBody
                x: 20
                y: 20
                width: Math.max(0, albumStory.width - 40)
                spacing: 12
                EchoTextTagRow {
                    text: qsTr("反复翻开的专辑")
                    color: Theme.secondaryText
                    font: Theme.font(11, Theme.mediumWeight)
                }
                Item {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 136
                    Rectangle {
                        width: 112
                        height: width
                        x: (parent.width - width) / 2 + 34
                        y: 8
                        radius: width / 2
                        color: Theme.dark ? "#202126" : "#30313a"
                        Repeater {
                            model: 4
                            delegate: Rectangle {
                                required property int index
                                anchors.centerIn: parent
                                width: 98 - index * 16
                                height: width
                                radius: width / 2
                                color: "transparent"
                                border.width: 1
                                border.color: "#45464c"
                            }
                        }
                        Rectangle {
                            anchors.centerIn: parent
                            width: 30
                            height: width
                            radius: width / 2
                            color: Theme.accent
                        }
                    }
                    EchoCover {
                        id: albumCover
                        objectName: "historyAlbumArtwork"
                        x: (parent.width - width) / 2 - 16
                        y: 9
                        width: 116
                        height: width
                        rotation: -8
                        source: overview.album.cover || ""
                        cornerRadius: 3
                        placeholderGlyph: "vinyl"
                        resourceActive: overview.resourceActive
                        retainWhileLoading: !overview.animate
                    }
                }
                EchoTextTagRow {
                    Layout.fillWidth: true
                    text: overview.album.name || qsTr("未知专辑")
                    font: Theme.font(18, Theme.boldWeight)
                    wrap: true
                }
                EchoTextTagRow {
                    objectName: "historyAlbumCount"
                    Layout.fillWidth: true
                    text: qsTr("%1 首歌，%2 次回访").arg(overview.album.songs || 0).arg(overview.album.plays || 0)
                    font: Theme.font(11, Theme.mediumWeight)
                    color: Theme.secondaryText
                }
            }
            EchoEnter {
                target: albumStory
                active: overview.resourceActive
                duration: overview.animate ? Theme.normalDuration : 0
                order: 1
            }
        }
    }
    GridLayout {
        id: metrics
        objectName: "historyStatGrid"
        Layout.fillWidth: true
        columns: width >= 640 * Theme.store.globalFontSizeScale / 100 ? 4 : width >= 300 * Theme.store.globalFontSizeScale / 100 ? 2 : 1
        columnSpacing: 20
        rowSpacing: 18
        Repeater {
            model: overview.stats.statCards || []
            delegate: ColumnLayout {
                id: metric
                required property var modelData
                objectName: "historyStatCard"
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                Layout.preferredWidth: 1
                spacing: 5
                EchoTextTagRow {
                    Layout.fillWidth: true
                    text: metric.modelData.value + (metric.modelData.unit ? " " + metric.modelData.unit : "")
                    font: Theme.font(26, Theme.semiboldWeight)
                    lineHeight: font.pixelSize * 1.3
                }
                EchoTextTagRow {
                    Layout.fillWidth: true
                    text: metric.modelData.label
                    color: Theme.secondaryText
                    font: Theme.font(11, Theme.regularWeight)
                }
            }
        }
    }
    EchoDivider {
        Layout.fillWidth: true
        lineColor: Theme.subtleBorder
    }
}
