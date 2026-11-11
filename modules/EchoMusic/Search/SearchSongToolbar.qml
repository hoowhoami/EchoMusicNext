pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Shapes
import EchoMusic.Theme
import EchoMusic.Controls
import EchoMusic.Music
import EchoMusic.Search

Rectangle {
    id: toolbar
    required property SearchModel service
    required property SongListGeometry geometry
    property bool showAlbum: true
    implicitHeight: 140
    // The sticky toolbar inherits the main panel background.
    // EchoPageScrollContainer clips list content below its separate header.
    color: "transparent"
    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        RowLayout {
            Layout.fillHeight: false
            Layout.fillWidth: true
            Layout.preferredHeight: 44
            Layout.minimumHeight: 44
            Layout.maximumHeight: 44
            Layout.bottomMargin: 0
            spacing: 10
            Rectangle {
                id: badgeSurface
                Layout.preferredWidth: 32
                Layout.preferredHeight: 32
                Layout.bottomMargin: 8
                radius: Theme.controlRadius
                color: "transparent"
                // CSS 135deg: upper-left to lower-right, including the original
                // 92% accent / 8% white start rather than a horizontal gradient.
                Shape {
                    anchors.fill: parent
                    ShapePath {
                        strokeWidth: -1
                        fillGradient: LinearGradient {
                            x1: 0
                            y1: 0
                            x2: badgeSurface.width
                            y2: badgeSurface.height
                            GradientStop {
                                position: 0
                                color: Qt.rgba(Theme.accent.r * 0.92 + 0.08, Theme.accent.g * 0.92 + 0.08, Theme.accent.b * 0.92 + 0.08, 1)
                            }
                            GradientStop {
                                position: 1
                                color: Theme.secondaryAccent
                            }
                        }
                        startX: Theme.controlRadius
                        startY: 0
                        PathLine {
                            x: badgeSurface.width - Theme.controlRadius
                            y: 0
                        }
                        PathArc {
                            x: badgeSurface.width
                            y: Theme.controlRadius
                            radiusX: Theme.controlRadius
                            radiusY: Theme.controlRadius
                        }
                        PathLine {
                            x: badgeSurface.width
                            y: badgeSurface.height - Theme.controlRadius
                        }
                        PathArc {
                            x: badgeSurface.width - Theme.controlRadius
                            y: badgeSurface.height
                            radiusX: Theme.controlRadius
                            radiusY: Theme.controlRadius
                        }
                        PathLine {
                            x: Theme.controlRadius
                            y: badgeSurface.height
                        }
                        PathArc {
                            x: 0
                            y: badgeSurface.height - Theme.controlRadius
                            radiusX: Theme.controlRadius
                            radiusY: Theme.controlRadius
                        }
                        PathLine {
                            x: 0
                            y: Theme.controlRadius
                        }
                        PathArc {
                            x: Theme.controlRadius
                            y: 0
                            radiusX: Theme.controlRadius
                            radiusY: Theme.controlRadius
                        }
                    }
                }
                // Use the Tabler star glyph for this action.
                EchoIcon {
                    anchors.centerIn: parent
                    width: 16
                    height: 16
                    name: "star"
                    color: "black"
                }
            }
            EchoTextTagRow {
                Layout.bottomMargin: 8
                text: toolbar.service.kind === SearchModel.Lyric ? qsTr("搜索歌词") : qsTr("搜索单曲")
                color: Theme.text
                font: Theme.font(15, Theme.semiboldWeight)
            }
            Item {
                Layout.fillWidth: true
            }
            // Playback resolves through the shared Qt authority. Batch selection
            // remains separate from replacing the current playback queue.
            EchoSoftButton {
                Layout.bottomMargin: 8
                text: qsTr("播放")
                glyph: "play"
                accent: true
                enabled: toolbar.service.results.count > 0
                onClicked: toolbar.service.playAll()
            }
            EchoSoftButton {
                Layout.bottomMargin: 8
                text: qsTr("批量")
                glyph: "list"
                enabled: false
            }
        }
        RowLayout {
            id: songTabRow
            objectName: "searchSongTabRow"
            Layout.fillHeight: false
            Layout.fillWidth: true
            Layout.preferredHeight: 52
            Layout.minimumHeight: 52
            Layout.maximumHeight: 52
            spacing: 8
            Item {
                objectName: "searchSongTabSlot"
                implicitWidth: songLabel.implicitWidth
                Layout.minimumWidth: implicitWidth
                Layout.preferredWidth: implicitWidth
                Layout.maximumWidth: implicitWidth
                Layout.preferredHeight: 36
                Layout.fillHeight: false
                EchoTextTagRow {
                    id: songLabel
                    objectName: "searchSongCountRow"
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 6
                    text: qsTr("歌曲")
                    spacing: 4
                    wrapTags: false
                    color: Theme.text
                    font: Theme.font(14, Theme.semiboldWeight)
                    EchoBadge {
                        id: badge
                        objectName: "searchSongCountBadge"
                        tone: EchoBadge.Neutral
                        count: toolbar.service.results.query.trim().length ? toolbar.service.results.count + " / " + toolbar.service.count : toolbar.service.count
                    }
                }
                Rectangle {
                    objectName: "searchSongTabIndicator"
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: 2
                    radius: 1
                    color: Theme.withAlpha(Theme.accent, 0.7)
                }
            }
            Item {
                Layout.fillWidth: true
            }
            EchoInputBackground {
                Layout.preferredWidth: 180
                Layout.preferredHeight: 36
                radius: Theme.controlRadius
                focused: filter.activeFocus
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 10
                    anchors.rightMargin: 6
                    spacing: 6
                    EchoIcon {
                        name: "search"
                        Layout.preferredWidth: 14
                        Layout.preferredHeight: 14
                        color: Theme.iconMain
                    }
                    EchoTextField {
                        id: filter
                        objectName: "searchResultFilter"
                        EchoInputFocus.boundary: parent.parent
                        Layout.fillWidth: true
                        padding: 0
                        leftPadding: 0
                        rightPadding: 0
                        font: Theme.font(12, Theme.regularWeight)
                        color: Theme.text
                        placeholderTextColor: Theme.secondaryText
                        placeholderText: qsTr("筛选当前结果")
                        background: null
                        selectionColor: Theme.accent
                        Accessible.name: qsTr("筛选当前搜索结果")
                        text: toolbar.service.results.query
                        onTextEdited: toolbar.service.results.query = text
                    }
                    EchoIconButton {
                        glyph: "x"
                        text: qsTr("清除筛选")
                        visible: filter.text.length > 0
                        Layout.preferredWidth: 20
                        Layout.preferredHeight: 20
                        padding: 5
                        focusPolicy: Qt.NoFocus
                        onClicked: {
                            toolbar.service.results.query = "";
                            filter.forceActiveFocus();
                        }
                    }
                }
            }
            SongLocateButton {
                enabled: false
            }
        }
        SongListHeader {
            geometry: toolbar.geometry
            Layout.fillWidth: true
            Layout.preferredHeight: 44
            Layout.minimumHeight: 44
            Layout.maximumHeight: 44
            lyricColumn: toolbar.service.kind === SearchModel.Lyric
            showAlbum: toolbar.showAlbum
            sortField: toolbar.service.results.sortField
            sortDirection: toolbar.service.results.sortDirection
            onSortPicked: field => toolbar.service.results.toggleSort(field)
        }
    }
    // Separate the song-count/filter row from the sortable column header,
    // independently of the short active underline and the header's own rule.
    EchoDivider {
        objectName: "searchSongTabDivider"
        anchors.left: parent.left
        anchors.right: parent.right
        y: songTabRow.y + songTabRow.height - height
        lineColor: Theme.subtleBorder
    }
}
