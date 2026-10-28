pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Theme
import EchoMusic.Controls

Button {
    id: card
    required property string entryKey
    required property string title
    required property bool selected
    property url preview
    signal chosen
    objectName: entryKey === "cover" ? "coverLyricSkinCard" : entryKey === "portrait" ? "portraitLyricSkinCard" : "pluginLyricSkinCard"
    implicitHeight: (width - 22) * 9 / 16 + 7 + 19.5 + 22
    padding: 11
    hoverEnabled: true
    Accessible.name: card.title
    Accessible.checked: card.selected
    onClicked: card.chosen()
    background: Rectangle {
        radius: Theme.cardRadius
        color: card.hovered ? Theme.floating.hover : Theme.floatingCard
        border.width: 1
        border.color: card.selected ? Theme.accent : Theme.floatingBorder
    }
    contentItem: ColumnLayout {
        spacing: 7
        Rectangle {
            objectName: "coverSkinPreview"
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: Theme.controlRadius
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop {
                    position: 0
                    color: "#1b1f27"
                }
                GradientStop {
                    position: 1
                    color: "#101216"
                }
            }
            Rectangle {
                visible: card && card.entryKey !== "portrait"
                x: 12
                y: (parent.height - height) / 2
                width: 34
                height: width
                radius: Theme.mediaRadius
                gradient: Gradient {
                    GradientStop {
                        position: 0
                        color: "#5b8def"
                    }
                    GradientStop {
                        position: .55
                        color: "#8b5cf6"
                    }
                    GradientStop {
                        position: 1
                        color: "#ec4899"
                    }
                }
            }
            Column {
                x: card && card.entryKey === "portrait" ? 16 : 56
                y: card && card.entryKey === "portrait" ? parent.height - height - 12 : (parent.height - height) / 2
                width: parent.width - (card && card.entryKey === "portrait" ? 32 : 68)
                spacing: 5
                Repeater {
                    model: [.8, .55, .65]
                    Rectangle {
                        required property real modelData
                        width: parent ? parent.width * modelData : 0
                        anchors.horizontalCenter: card && card.entryKey === "portrait" ? parent.horizontalCenter : undefined
                        height: 4
                        radius: Theme.microRadius
                        color: Theme.withAlpha("white", modelData === .55 ? .4 : .22)
                    }
                }
            }
            EchoCover {
                anchors.fill: parent
                visible: card.preview.toString().length > 0
                source: card.preview
                resourceActive: card.visible
                cornerRadius: Theme.controlRadius
            }
            EchoSelectionBadge {
                objectName: "coverSkinSelectionBadge"
                z: 2
                visible: card.selected && !card.hovered
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 8
            }
            Rectangle {
                z: 2
                visible: card.hovered
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 8
                width: 20
                height: 20
                radius: Theme.detailRadius
                color: Theme.elevated
                Rectangle {
                    anchors.fill: parent
                    radius: parent.radius
                    color: Theme.selected
                }
                EchoIcon {
                    anchors.centerIn: parent
                    width: 14
                    height: 14
                    name: "settings"
                    color: Theme.accentText
                }
            }
        }
        EchoTextTagRow {
            Layout.fillWidth: true
            text: card.title
            font: Theme.font(13, Theme.semiboldWeight)
            lineHeight: 19.5
            color: Theme.floatingText
        }
    }
}
