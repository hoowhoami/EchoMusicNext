pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Controls
import EchoMusic.Theme

// Virtualized singer and user card. Singers navigate to their artist page;
// followed users have no navigation target.
ItemDelegate {
    id: card
    EchoActionCursor {}
    objectName: "favoriteUserCard"
    required property string nickname
    required property string avatarUrl
    required property string metaText
    property bool singer: false
    property string targetId: ""
    property bool resourceActive: true
    property bool navigationAvailable: false
    signal openRequested(string id)
    hoverEnabled: true
    padding: 12
    Accessible.name: nickname
    onClicked: {
        if (singer && navigationAvailable && targetId.length > 0)
            openRequested(targetId);
    }
    transform: Translate {
        y: card.resourceActive && card.hovered && Theme.normalDuration > 0 ? -1 : 0
        Behavior on y {
            enabled: card.resourceActive
            NumberAnimation {
                duration: Theme.normalDuration
                easing.type: Easing.BezierSpline
                easing.bezierCurve: Theme.easeStandard
            }
        }
    }
    background: Rectangle {
        radius: Theme.cardRadius
        color: Theme.mutedSurface
        border.width: card.visualFocus ? 2 : 0
        border.color: Theme.accent
    }
    contentItem: ColumnLayout {
        spacing: 6
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: width
            EchoCover {
                anchors.centerIn: parent
                width: Math.min(parent.width * .82, 180)
                height: width
                source: card.avatarUrl
                circular: true
                resourceActive: card.resourceActive
            }
        }
        ColumnLayout {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            Layout.preferredHeight: 38
            spacing: 3
            Text {
                Layout.fillWidth: true
                text: card.nickname
                color: Theme.text
                font: Theme.font(13, Theme.semiboldWeight)
                lineHeightMode: Text.FixedHeight
                lineHeight: font.pixelSize * 1.15
                elide: Text.ElideRight
            }
            Text {
                Layout.fillWidth: true
                visible: card.metaText.length > 0
                text: card.metaText
                color: Theme.secondaryText
                font: Theme.font(11, Theme.semiboldWeight)
                lineHeightMode: Text.FixedHeight
                lineHeight: 16.5
                elide: Text.ElideRight
            }
        }
        Item {
            Layout.fillHeight: true
        }
    }
}
