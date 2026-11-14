pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import EchoMusic.Controls
import EchoMusic.Theme
import EchoMusic.Playback

Flickable {
    id: pane
    required property PlaybackService playback
    required property Popup surface
    contentWidth: width
    contentHeight: body.height + 20
    clip: true
    boundsBehavior: Flickable.StopAtBounds
    Column {
        id: body
        x: 8
        y: 8
        width: parent.width - 16
        spacing: 12
        Grid {
            columns: 3
            spacing: 8
            Repeater {
                model: [
                    {
                        id: "none",
                        label: qsTr("原声")
                    },
                    {
                        id: "piano",
                        label: qsTr("钢琴")
                    },
                    {
                        id: "vocal",
                        label: qsTr("人声")
                    },
                    {
                        id: "accompaniment",
                        label: qsTr("伴奏")
                    },
                    {
                        id: "subwoofer",
                        label: qsTr("骨笛")
                    },
                    {
                        id: "ancient",
                        label: qsTr("尤克里里")
                    },
                    {
                        id: "surnay",
                        label: qsTr("唢呐")
                    },
                    {
                        id: "dj",
                        label: "DJ"
                    }
                ]
                delegate: PlaybackEffectPresetButton {
                    required property var modelData
                    objectName: "songEffect-" + modelData.id
                    width: (body.width - 16) / 3
                    text: modelData.label
                    accent: pane.playback.appliedSongEffect === modelData.id
                    enabled: pane.playback.hasTrack && !pane.playback.loading
                    onClicked: pane.playback.setSongEffect(modelData.id)
                }
            }
        }
        Rectangle {
            visible: !pane.playback.loading && pane.playback.songEffectError.length > 0
            width: parent.width
            height: feedback.implicitHeight + 20
            radius: Theme.controlRadius
            color: Theme.floating.mutedSurface
            EchoDashedFrame {
                anchors.fill: parent
                solid: true
                strokeColor: Theme.floating.subtleBorder
                radius: Theme.controlRadius
            }
            Column {
                id: feedback
                x: 12
                y: 10
                width: parent.width - 24
                spacing: 3
                Label {
                    text: qsTr("所选音效暂未生效")
                    color: Theme.floating.text
                    font: Theme.font(12, Theme.mediumWeight)
                }
                Label {
                    width: parent.width
                    text: pane.playback.songEffectError
                    wrapMode: Text.Wrap
                    font: Theme.font(12, Theme.regularWeight)
                    color: Theme.floating.secondaryText
                }
            }
        }
    }
    ScrollBar.vertical: EchoPopupScrollBar {
        surface: pane.surface
        view: pane
        resourceActive: pane.surface.visible && pane.visible
    }
}
