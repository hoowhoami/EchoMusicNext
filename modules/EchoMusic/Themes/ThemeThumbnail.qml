pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import EchoMusic.Controls
import EchoMusic.Theme

// Fixed decorative theme miniature. Ratio geometry stays outside layout size
// hints to avoid size feedback through nested layouts.
Rectangle {
    id: preview
    objectName: "themeThumbnail"
    required property bool solid
    required property color seed
    property bool resourceActive: true
    property var paletteOverride: ({})
    readonly property var previewColors: Object.assign({}, Theme.store.previewPalette(Theme.dark, seed, solid), paletteOverride, paletteOverride.secondaryText !== undefined ? {
        secondary: paletteOverride.secondaryText
    } : {})
    component ThumbnailIcon: EchoIcon {
        implicitWidth: 14
        implicitHeight: 14
        color: preview.previewColors.text
        resourceActive: preview.resourceActive && preview.visible
    }
    implicitHeight: width / 1.6
    color: previewColors.shell
    radius: 10
    clip: true
    Item {
        id: sidebar
        x: 16
        y: 19
        width: (preview.width - 24) * 0.23 - 8
        height: preview.height - 38
        readonly property real menuTop: 36 + (width - 4) / 1.6
        Rectangle {
            width: 18
            height: 18
            radius: 9
            color: preview.previewColors.secondary
            opacity: 0.17
        }
        Rectangle {
            x: 23
            y: 4
            width: Math.max(0, sidebar.width - x)
            height: 3
            radius: 1.5
            color: preview.previewColors.text
            opacity: 0.38
        }
        Rectangle {
            x: 23
            y: 11
            width: Math.max(0, sidebar.width - x - 7)
            height: 2
            radius: 1
            color: preview.previewColors.secondary
            opacity: 0.25
        }
        Repeater {
            model: 4
            delegate: Rectangle {
                required property int index
                readonly property real cell: (sidebar.width - 4) / 2
                x: (index % 2) * (cell + 4)
                y: 27 + Math.floor(index / 2) * (cell / 1.6 + 4)
                width: cell
                height: cell / 1.6
                radius: 3
                color: preview.previewColors.secondary
                opacity: 0.13
            }
        }
        Repeater {
            model: ["heart", "cloud", "clock"]
            delegate: Item {
                id: navigationRow
                required property string modelData
                required property int index
                y: sidebar.menuTop + index * 10
                width: sidebar.width
                height: 7
                ThumbnailIcon {
                    name: navigationRow.modelData
                    width: 7
                    height: 7
                    opacity: 0.5
                }
                Rectangle {
                    x: 13
                    y: 2
                    width: Math.max(0, sidebar.width - 19 - navigationRow.index * 4)
                    height: 3
                    radius: 1.5
                    color: preview.previewColors.text
                    opacity: 0.3
                }
            }
        }
        Item {
            id: playlists
            y: sidebar.menuTop + 30
            width: sidebar.width
            height: Math.max(0, footer.y - y - 4)
            visible: height >= 6
            Rectangle {
                y: 2
                width: 14
                height: 2
                radius: 1
                color: preview.previewColors.accent
            }
            Rectangle {
                x: 19
                y: 2
                width: 11
                height: 2
                radius: 1
                color: preview.previewColors.secondary
                opacity: 0.45
            }
            ThumbnailIcon {
                x: playlists.width - width
                width: 6
                height: 6
                name: "plus"
                opacity: 0.55
            }
            Repeater {
                model: Math.max(0, Math.min(3, Math.floor((playlists.height - 6) / 8)))
                delegate: Item {
                    id: playlistRow
                    required property int index
                    y: 8 + index * 8
                    width: playlists.width
                    height: 6
                    ThumbnailIcon {
                        width: 6
                        height: 6
                        name: "music"
                        opacity: 0.4
                    }
                    Rectangle {
                        x: 12
                        y: 1.5
                        width: Math.max(0, playlists.width - 17 - playlistRow.index * 3)
                        height: 2.5
                        radius: 1.25
                        color: preview.previewColors.text
                        opacity: 0.35
                    }
                }
            }
        }
        Rectangle {
            y: footer.y - 5
            width: sidebar.width
            height: 0.5
            color: preview.previewColors.secondary
            opacity: 0.14
        }
        RowLayout {
            id: footer
            y: sidebar.height - height
            width: sidebar.width
            height: 8
            spacing: Math.max(0, (width - 32) / 3)
            Repeater {
                model: ["circle-chevron-left", "layout-sidebar", "settings", "shirt"]
                delegate: ThumbnailIcon {
                    required property string modelData
                    Layout.preferredWidth: 8
                    Layout.preferredHeight: 8
                    name: modelData
                    opacity: 0.7
                }
            }
        }
    }

    Rectangle {
        id: main
        x: 12 + (preview.width - 24) * 0.23 + 6
        y: 12
        width: preview.width - x - 12
        height: preview.height - player.height - 30
        color: preview.previewColors.main
        radius: 6
        clip: true
        Rectangle {
            x: 9
            y: 9
            width: (main.width - 18) * 0.6
            height: 7
            radius: 4
            color: preview.previewColors.secondary
            opacity: 0.13
        }
        Rectangle {
            x: 9
            y: 25
            width: (main.width - 18) * 0.35
            height: 4
            radius: 2
            color: preview.previewColors.text
            opacity: 0.72
        }
        Repeater {
            model: 3
            delegate: Rectangle {
                required property int index
                readonly property real cell: (main.width - 28) / 3
                x: 9 + index * (cell + 5)
                y: 36
                width: cell
                height: cell
                radius: 4
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop {
                        position: 0
                        color: preview.previewColors.accent
                    }
                    GradientStop {
                        position: 1
                        color: preview.previewColors.main
                    }
                }
            }
        }
        Repeater {
            model: 3
            delegate: Item {
                required property int index
                readonly property real cell: (main.width - 28) / 3
                x: 9 + index * (cell + 5)
                y: 41 + cell
                width: cell
                height: 9
                Rectangle {
                    width: parent.width * 0.76
                    height: 3
                    radius: 1.5
                    color: preview.previewColors.text
                    opacity: 0.5
                }
                Rectangle {
                    y: 6
                    width: parent.width * 0.48
                    height: 2
                    radius: 1
                    color: preview.previewColors.secondary
                    opacity: 0.3
                }
            }
        }
    }

    Rectangle {
        id: player
        objectName: "themeThumbnailPlayer"
        x: main.x
        y: main.y + main.height + 6
        width: main.width
        height: width * 64 / 360
        color: preview.previewColors.player
        radius: 6
        // A fixed decorative coordinate space keeps the miniature coherent at
        // every card width. No player, live media or interaction is instantiated.
        Item {
            width: 360
            height: 64
            scale: player.width / width
            transformOrigin: Item.TopLeft
            RowLayout {
                x: 10
                y: 10
                width: 112
                height: 44
                spacing: 9
                Rectangle {
                    Layout.preferredWidth: 44
                    Layout.preferredHeight: 44
                    radius: 7
                    gradient: Gradient {
                        GradientStop {
                            position: 0
                            color: preview.previewColors.accent
                        }
                        GradientStop {
                            position: 1
                            color: Theme.mix(preview.previewColors.accent, preview.previewColors.player, 0.65)
                        }
                    }
                    ThumbnailIcon {
                        anchors.centerIn: parent
                        width: 22
                        height: 22
                        name: "music"
                        color: EchoColorValue.contrast(preview.previewColors.accent, "white") >= 3 ? "white" : "#171718"
                        opacity: 0.8
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 5
                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: 4
                        radius: 2
                        color: preview.previewColors.text
                        opacity: 0.8
                    }
                    Rectangle {
                        Layout.preferredWidth: 34
                        implicitHeight: 3
                        radius: 1.5
                        color: preview.previewColors.secondary
                        opacity: 0.55
                    }
                    RowLayout {
                        spacing: 6
                        ThumbnailIcon {
                            name: "heart"
                            implicitWidth: 11
                            implicitHeight: 11
                            opacity: 0.65
                        }
                        ThumbnailIcon {
                            name: "text-plus"
                            implicitWidth: 11
                            implicitHeight: 11
                            opacity: 0.65
                        }
                    }
                }
            }
            ColumnLayout {
                x: 133
                y: 7
                width: 146
                height: 49
                spacing: 6
                RowLayout {
                    Layout.alignment: Qt.AlignHCenter
                    spacing: 12
                    ThumbnailIcon {
                        name: "repeat"
                        implicitWidth: 11
                        implicitHeight: 11
                        opacity: 0.55
                    }
                    ThumbnailIcon {
                        name: "player-skip-back"
                    }
                    Rectangle {
                        implicitWidth: 25
                        implicitHeight: 25
                        radius: 12.5
                        color: Theme.withAlpha(preview.previewColors.text, 0.08)
                        ThumbnailIcon {
                            anchors.centerIn: parent
                            width: 14
                            height: 14
                            name: "player-play"
                        }
                    }
                    ThumbnailIcon {
                        name: "player-skip-forward"
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 5
                    Text {
                        text: "0:42"
                        font: Theme.font(8, Theme.regularWeight)
                        color: preview.previewColors.secondary
                    }
                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: 3
                        radius: 1.5
                        color: Theme.withAlpha(preview.previewColors.text, 0.12)
                        Rectangle {
                            width: parent.width * 0.38
                            height: parent.height
                            radius: parent.radius
                            color: preview.previewColors.accent
                        }
                    }
                    Text {
                        text: "3:28"
                        font: Theme.font(8, Theme.regularWeight)
                        color: preview.previewColors.secondary
                    }
                }
            }
            RowLayout {
                x: 299
                y: 24
                spacing: 8
                ThumbnailIcon {
                    name: "volume"
                    opacity: 0.75
                }
                ThumbnailIcon {
                    name: "list"
                    opacity: 0.75
                }
                ThumbnailIcon {
                    name: "dots"
                    implicitWidth: 12
                    implicitHeight: 12
                    opacity: 0.75
                }
            }
        }
    }
}
