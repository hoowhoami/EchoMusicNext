pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Controls
import EchoMusic.Theme

EchoActionButton {
    id: card
    required property var room
    objectName: "listenRoomCard"
    property bool mine: false
    readonly property string currentArtist: (room.current_song?.song?.artists || []).map(artist => artist.name || "").filter(name => name.length > 0).join(" / ")
    property bool resourcesActive: true
    signal inspect(string roomId)
    padding: 0
    topPadding: 0
    bottomPadding: 0
    leftPadding: 0
    rightPadding: 0
    hoverEnabled: true
    implicitHeight: cardContent.implicitHeight
    topInset: 0
    bottomInset: 0
    leftInset: 0
    rightInset: 0
    function singleLine(value): string {
        return String(value).replace(/\s+/g, " ").trim();
    }
    background: Rectangle {
        radius: Theme.cardRadius
        color: Theme.card
        border.width: 1
        border.color: card.hovered ? Theme.mix(Theme.border, Theme.accent, 0.32) : Theme.border
    }
    contentItem: Item {
        id: cardContent
        objectName: "listenRoomCardContent"
        implicitHeight: 96 + 13 + cardBody.implicitHeight + 14
        clip: true
        Rectangle {
            objectName: "listenRoomCardVisual"
            width: parent.width
            height: 96
            topLeftRadius: Theme.cardRadius
            topRightRadius: Theme.cardRadius
            color: "#151821"
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop {
                    position: 0
                    color: Theme.mix("#14151c", Theme.accent, 0.24)
                }
                GradientStop {
                    position: 1
                    color: "#151821"
                }
            }
            ListenSurface {
                anchors.fill: parent
                kind: 1
                radius: Theme.cardRadius
            }
            ListenLiveBadge {
                x: 13
                y: 13
            }
            Label {
                anchors.right: parent.right
                anchors.rightMargin: 14
                y: 14
                text: card.mine ? qsTr("我的房间") : qsTr("众乐房")
                font: Theme.font(10, 700)
                color: "#deffffff"
            }
            Row {
                x: 22
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 20
                height: 42
                spacing: 5
                Repeater {
                    model: [14, 32, 22, 40, 18]
                    Rectangle {
                        id: wave
                        required property int modelData
                        required property int index
                        width: 4
                        height: modelData
                        y: (42 - height) / 2
                        radius: 2
                        color: Theme.mix(Theme.accent, "white", 0.82)
                        SequentialAnimation on scale {
                            running: card.resourcesActive && Theme.store.motionEnabled
                            loops: Animation.Infinite
                            NumberAnimation {
                                to: 0.55
                                duration: 550 + wave.index * 45
                                easing.type: Easing.InOutSine
                            }
                            NumberAnimation {
                                to: 1
                                duration: 550 + wave.index * 45
                                easing.type: Easing.InOutSine
                            }
                        }
                    }
                }
            }
            EchoCover {
                width: 30
                height: 30
                circular: true
                placeholderGlyph: "user"
                anchors.right: parent.right
                anchors.rightMargin: 15
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 14
                source: card.room.owner?.avatar_url || ""
                resourceActive: card.resourcesActive
            }
        }
        ColumnLayout {
            id: cardBody
            objectName: "listenRoomCardBody"
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.leftMargin: 14
            anchors.rightMargin: 14
            anchors.topMargin: 109
            height: implicitHeight
            spacing: 0
            RowLayout {
                Layout.fillWidth: true
                spacing: 10
                Label {
                    objectName: "listenRoomCardTitle"
                    Layout.fillWidth: true
                    text: card.singleLine(card.room.name || qsTr("一起听房间"))
                    textFormat: Text.PlainText
                    color: Theme.text
                    font: Theme.font(13, 850)
                    lineHeightMode: Text.FixedHeight
                    lineHeight: 19.5
                    elide: Text.ElideRight
                    maximumLineCount: 1
                }
                EchoButtonContent {
                    glyph: "users"
                    glyphSize: 13
                    spacing: 4
                    text: card.room.member_count === null ? "—" : String(card.room.member_count ?? "—")
                    color: Theme.secondaryText
                    font: Theme.font(10, 800)
                }
            }
            Label {
                Layout.fillWidth: true
                Layout.topMargin: 3
                text: qsTr("%1 的房间").arg(card.singleLine(card.room.owner?.name || qsTr("房主")))
                textFormat: Text.PlainText
                color: Theme.secondaryText
                font: Theme.font(11, 650)
                lineHeightMode: Text.FixedHeight
                lineHeight: 16.5
                elide: Text.ElideRight
                maximumLineCount: 1
            }
            EchoButtonContent {
                Layout.fillWidth: true
                Layout.topMargin: 10
                glyph: "music"
                glyphSize: 14
                spacing: 7
                text: card.singleLine(card.room.current_song?.song?.title || qsTr("一起分享好音乐")) + (card.currentArtist ? " · " + card.singleLine(card.currentArtist) : "")
                color: Theme.accentText
                font: Theme.font(9, 750)
                horizontalAlignment: Qt.AlignLeft
            }
            Item {
                // EchoMusic reserves its tag row even when the room has no tags.
                Layout.fillWidth: true
                Layout.topMargin: 10
                implicitHeight: 20
            }
        }
    }
    onClicked: inspect(String(room.id || ""))
}
