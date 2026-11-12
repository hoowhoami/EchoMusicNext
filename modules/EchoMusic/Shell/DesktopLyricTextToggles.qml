pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Controls
import EchoMusic.Theme
import EchoMusic.Playback

Rectangle {
    id: group
    required property PlaybackService playback
    property bool vertical: false
    readonly property int count: Number(playback.lyrics.hasTranslation) + Number(playback.lyrics.hasRomanization)
    implicitWidth: vertical ? 30 : count * 32
    implicitHeight: vertical ? count * 30 : 32
    radius: Theme.controlRadius
    color: "#1affffff"
    GridLayout {
        anchors.fill: parent
        columns: group.vertical ? 1 : 2
        columnSpacing: 0
        rowSpacing: 0
        Repeater {
            model: ["translation", "romanization"]
            delegate: ToolButton {
                id: toggle
                EchoActionCursor {}
                required property string modelData
                readonly property bool translation: modelData === "translation"
                readonly property bool first: translation || !group.playback.lyrics.hasTranslation
                readonly property bool last: !translation || !group.playback.lyrics.hasRomanization
                objectName: "desktopLyricText-" + modelData
                visible: translation ? group.playback.lyrics.hasTranslation : group.playback.lyrics.hasRomanization
                implicitWidth: group.vertical ? 30 : 32
                implicitHeight: group.vertical ? 30 : 32
                hoverEnabled: true
                checked: translation ? group.playback.lyricText.wantTranslation : group.playback.lyricText.wantRomanization
                text: translation ? qsTr("译") : qsTr("音")
                Accessible.name: translation ? qsTr("翻译") : qsTr("音译")
                padding: 0
                contentItem: EchoButtonContent {
                    text: toggle.text
                    color: toggle.checked ? "#31cfa1" : "white"
                    font: Theme.font(12, 700)
                }
                background: Rectangle {
                    color: toggle.hovered ? "#4dffffff" : "transparent"
                    topLeftRadius: toggle.first ? Theme.controlRadius : 0
                    topRightRadius: (group.vertical ? toggle.first : toggle.last) ? Theme.controlRadius : 0
                    bottomLeftRadius: (group.vertical ? toggle.last : toggle.first) ? Theme.controlRadius : 0
                    bottomRightRadius: toggle.last ? Theme.controlRadius : 0
                    border.width: toggle.visualFocus ? 1 : 0
                    border.color: "white"
                }
                Rectangle {
                    visible: !toggle.first
                    width: group.vertical ? parent.width : 1
                    height: group.vertical ? 1 : parent.height
                    color: "#1fffffff"
                }
                onClicked: {
                    if (translation)
                        group.playback.lyricText.wantTranslation = !group.playback.lyricText.wantTranslation;
                    else
                        group.playback.lyricText.wantRomanization = !group.playback.lyricText.wantRomanization;
                }
            }
        }
    }
}
