pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Theme
import EchoMusic.Controls

Button {
    id: card
    required property string label
    required property string description
    required property string emblem
    property string badgeGlyph: ""
    property bool secondary: false
    property bool resourceActive: true
    implicitHeight: 72
    padding: 18
    topPadding: 0
    bottomPadding: 0
    hoverEnabled: true
    Accessible.name: label
    background: Rectangle {
        color: Theme.mutedSurface
        radius: Theme.cardRadius
        border.width: card.visualFocus ? 2 : 1
        border.color: card.visualFocus ? Theme.accent : Theme.subtleBorder
    }
    transform: Translate {
        y: card.resourceActive && card.hovered && Theme.store.motionEnabled ? -1 : 0
        Behavior on y {
            NumberAnimation {
                duration: Theme.fastDuration
            }
        }
    }
    contentItem: RowLayout {
        spacing: 14
        Rectangle {
            Layout.preferredWidth: 44
            Layout.preferredHeight: 44
            radius: Theme.mediaRadius
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop {
                    position: 0
                    color: card.secondary ? Theme.secondaryAccent : Theme.accent
                }
                GradientStop {
                    position: 1
                    color: Theme.withAlpha(card.secondary ? Theme.secondaryAccent : Theme.accent, .7)
                }
            }
            Text {
                anchors.centerIn: parent
                text: card.emblem
                font: Theme.font(14, Theme.boldWeight)
                color: "white"
            }
        }
        ColumnLayout {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            spacing: 2
            Text {
                Layout.fillWidth: true
                text: card.label
                textFormat: Text.PlainText
                font: Theme.font(14, Theme.semiboldWeight)
                color: Theme.text
                elide: Text.ElideRight
            }
            Text {
                Layout.fillWidth: true
                text: card.description
                textFormat: Text.PlainText
                font: Theme.font(12, Theme.mediumWeight)
                color: Theme.secondaryText
                elide: Text.ElideRight
            }
        }
        Rectangle {
            Layout.preferredWidth: 30
            Layout.preferredHeight: 30
            radius: Theme.controlRadius
            color: Theme.withAlpha(Theme.accent, .12)
            EchoIcon {
                anchors.centerIn: parent
                width: 14
                height: 14
                name: card.badgeGlyph.length > 0 ? card.badgeGlyph : card.secondary ? "sparkles" : "play"
                color: Theme.accentText
            }
        }
    }
}
