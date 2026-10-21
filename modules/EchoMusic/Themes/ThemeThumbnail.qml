pragma ComponentBehavior: Bound
import QtQuick
import EchoMusic.Theme

// Fixed decorative theme miniature. Ratio geometry stays outside layout size
// hints to avoid size feedback through nested layouts.
Rectangle {
    id: preview
    required property bool solid
    required property color seed
    property var paletteOverride: ({})
    readonly property var previewColors: Object.assign({}, Theme.store.previewPalette(Theme.dark, seed, solid), paletteOverride, paletteOverride.secondaryText !== undefined ? {
        secondary: paletteOverride.secondaryText
    } : {})
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
        Rectangle {
            width: 18
            height: 18
            radius: 9
            color: preview.previewColors.secondary
            opacity: 0.17
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
            model: 3
            delegate: Rectangle {
                required property int index
                y: 36 + (sidebar.width - 4) / 1.6 + index * 12
                width: sidebar.width * 0.8
                height: 3
                radius: 2
                color: preview.previewColors.text
                opacity: 0.22
            }
        }
    }
    Rectangle {
        id: main
        x: 12 + (preview.width - 24) * 0.23 + 6
        y: 12
        width: preview.width - x - 12
        height: (preview.height - 30) * 0.78
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
            model: 2
            delegate: Rectangle {
                required property int index
                x: 9
                y: 45 + (main.width - 28) / 3 + index * 10
                width: (main.width - 18) * 0.8
                height: 3
                radius: 2
                color: preview.previewColors.secondary
                opacity: 0.22
            }
        }
    }
    Rectangle {
        x: main.x
        y: main.y + main.height + 6
        width: main.width
        height: preview.height - y - 12
        color: preview.previewColors.player
        radius: 6
        Rectangle {
            x: 6
            y: (parent.height - 16) / 2
            width: 16
            height: 16
            radius: 3
            color: preview.previewColors.accent
            opacity: 0.45
        }
        Rectangle {
            x: 27
            y: parent.height / 2
            width: 20
            height: 3
            color: preview.previewColors.text
            opacity: 0.3
        }
        Text {
            x: 52
            anchors.verticalCenter: parent.verticalCenter
            text: "▶"
            color: preview.previewColors.text
            font: Theme.font(9, Theme.regularWeight)
        }
        Rectangle {
            x: 66
            y: parent.height / 2
            width: parent.width - 72
            height: 2
            radius: 1
            color: preview.previewColors.accent
        }
    }
}
