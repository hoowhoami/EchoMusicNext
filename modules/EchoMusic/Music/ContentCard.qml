pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Theme
import EchoMusic.Controls

ItemDelegate {
    id: card
    objectName: "contentDelegate"
    required property string title
    required property string artistText
    required property string extraText
    required property string coverUrl
    required property string durationText
    required property string resourceId
    property bool artist: false
    property bool video: false
    property bool resourceActive: true
    property bool navigationAvailable: false
    signal openRequested(string id)
    // Include the original transparent 1px border in the content inset.
    padding: artist ? 13 : video ? 9 : 11
    hoverEnabled: true
    Accessible.name: title
    onClicked: {
        if (navigationAvailable)
            openRequested(resourceId);
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
        spacing: card.artist ? 6 : 8
        Item {
            id: media
            Layout.fillWidth: true
            Layout.preferredHeight: width / (card.video ? 1.78 : 1)
            EchoCover {
                coverScope: "card"
                anchors.centerIn: parent
                width: card.artist ? Math.min(parent.width * 0.82, 180) : parent.width
                height: card.artist ? width : parent.height
                source: card.coverUrl
                circular: card.artist
                resourceActive: card.resourceActive
            }
            Rectangle {
                anchors.fill: parent
                radius: Theme.controlRadius
                color: "#4d000000"
                opacity: card.resourceActive && card.hovered && card.video ? 1 : 0
                Behavior on opacity {
                    enabled: card.resourceActive
                    NumberAnimation {
                        duration: Theme.normalDuration
                        easing.type: Easing.BezierSpline
                        easing.bezierCurve: Theme.easeStandard
                    }
                }
                Rectangle {
                    anchors.centerIn: parent
                    width: 36
                    height: 36
                    radius: 18
                    color: "#e6ffffff"
                    scale: card.resourceActive && card.hovered ? 1 : 0.75
                    Behavior on scale {
                        enabled: card.resourceActive
                        NumberAnimation {
                            duration: Theme.normalDuration
                            easing.type: Easing.BezierSpline
                            easing.bezierCurve: Theme.easeStandard
                        }
                    }
                    EchoIcon {
                        anchors.centerIn: parent
                        width: 14
                        height: 14
                        name: "play"
                        color: "black"
                        resourceActive: card.resourceActive
                    }
                }
            }
            Rectangle {
                visible: card.video && card.durationText.length > 0
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.margins: 8
                width: time.implicitWidth + 10
                height: 20
                radius: 4
                color: "#99000000"
                Text {
                    id: time
                    anchors.centerIn: parent
                    text: card.durationText
                    color: "white"
                    font: Theme.font(11, Theme.regularWeight)
                }
            }
        }
        ColumnLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 2
            Layout.rightMargin: 2
            Layout.preferredHeight: card.artist ? 38 : 36
            spacing: card.artist ? 3 : 2
            Text {
                Layout.fillWidth: true
                text: card.title
                color: Theme.text
                font: Theme.font(13, Theme.semiboldWeight)
                textFormat: Text.PlainText
                lineHeightMode: Text.FixedHeight
                lineHeight: font.pixelSize * (card.artist ? 1.15 : 1.1)
                elide: Text.ElideRight
            }
            Text {
                Layout.fillWidth: true
                text: card.video ? card.artistText : card.extraText
                textFormat: Text.PlainText
                color: Theme.secondaryText
                font: Theme.font(11, Theme.semiboldWeight)
                lineHeightMode: Text.FixedHeight
                lineHeight: 16.5
                elide: Text.ElideRight
            }
            Item {
                Layout.fillHeight: true
            }
        }
    }
}
