pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Theme
import EchoMusic.Controls

ItemDelegate {
    id: card
    EchoActionCursor {
        enabled: card.navigationAllowed
    }
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
    property bool navigationAllowed: navigationAvailable && resourceId.length > 0
    signal openRequested(string id)
    // The media has its own lift gutter; hit geometry and captions never move.
    padding: 0
    topPadding: 4
    implicitHeight: topPadding + width / (video ? 1.78 : 1) + 8 + (artist ? 40 : 58)
    hoverEnabled: true
    Accessible.name: title
    onClicked: {
        if (navigationAllowed)
            openRequested(resourceId);
    }
    background: Item {
        Loader {
            anchors.fill: parent
            active: card.visualFocus && card.resourceActive
            sourceComponent: EchoDashedFrame {
                objectName: "contentCardFocusFrame"
                solid: true
                strokeColor: Theme.accent
                radius: Theme.cardRadius
                thickness: 2
            }
        }
    }
    contentItem: ColumnLayout {
        spacing: 8
        Item {
            id: media
            objectName: "contentCardMedia"
            transform: Translate {
                y: card.resourceActive && card.hovered ? -4 : 0
                Behavior on y {
                    enabled: motion.allowed
                    NumberAnimation {
                        id: lift
                        objectName: "contentArtworkLift"
                        duration: Theme.normalDuration
                        easing.type: Easing.BezierSpline
                        easing.bezierCurve: Theme.easeStandard
                    }
                }
            }
            EchoMotionScope {
                id: motion
                target: card
                active: card.resourceActive
                animations: [lift]
            }
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
            Loader {
                anchors.fill: parent
                active: card.video && card.resourceActive
                sourceComponent: Item {
                    objectName: "contentCardVideoDetails"
                    Rectangle {
                        objectName: "contentCardPlayIndicator"
                        anchors.centerIn: parent
                        width: 36
                        height: 36
                        radius: 18
                        color: "#e6ffffff"
                        visible: card.hovered || card.visualFocus
                        EchoIcon {
                            anchors.centerIn: parent
                            width: 14
                            height: 14
                            name: "play"
                            color: "black"
                            resourceActive: card.resourceActive
                        }
                    }
                    Rectangle {
                        visible: card.durationText.length > 0
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
                            textFormat: Text.PlainText
                        }
                    }
                }
            }
        }
        ColumnLayout {
            Layout.fillWidth: true
            objectName: "contentCardCaption"
            Layout.preferredHeight: card.artist ? 40 : 58
            Layout.minimumHeight: Layout.preferredHeight
            Layout.maximumHeight: Layout.preferredHeight
            spacing: 4
            Text {
                Layout.fillWidth: true
                objectName: "contentCardTitle"
                Layout.preferredHeight: card.artist ? 18 : 36
                Layout.minimumHeight: Layout.preferredHeight
                Layout.maximumHeight: Layout.preferredHeight
                text: card.title
                color: Theme.text
                font: Theme.font(13, Theme.semiboldWeight)
                textFormat: Text.PlainText
                lineHeightMode: Text.FixedHeight
                lineHeight: 18
                wrapMode: card.artist ? Text.NoWrap : Text.Wrap
                maximumLineCount: card.artist ? 1 : 2
                horizontalAlignment: card.artist ? Text.AlignHCenter : Text.AlignLeft
                elide: Text.ElideRight
            }
            Text {
                Layout.fillWidth: true
                objectName: "contentCardSecondary"
                Layout.preferredHeight: 18
                text: card.video || !card.extraText.length ? card.artistText : card.extraText
                horizontalAlignment: card.artist ? Text.AlignHCenter : Text.AlignLeft
                textFormat: Text.PlainText
                color: Theme.secondaryText
                font: Theme.font(12, Theme.regularWeight)
                lineHeightMode: Text.FixedHeight
                lineHeight: 18
                elide: Text.ElideRight
            }
        }
    }
}
