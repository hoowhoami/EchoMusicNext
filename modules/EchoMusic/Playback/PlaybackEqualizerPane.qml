pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Controls
import EchoMusic.Theme
import EchoMusic.Playback

ColumnLayout {
    id: pane
    required property PlaybackAudioControls audio
    readonly property bool locked: audio.providerId.length > 0
    readonly property var frequencies: ["31", "62", "125", "250", "500", "1k", "2k", "4k", "8k", "16k"]
    spacing: 0
    Label {
        visible: pane.locked
        Layout.fillWidth: true
        Layout.leftMargin: 16
        Layout.rightMargin: 16
        Layout.bottomMargin: 8
        text: qsTr("当前由音效引擎自动处理")
        color: Theme.floating.accentText
        font: Theme.font(12, Theme.mediumWeight)
    }
    ColumnLayout {
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.leftMargin: 16
        Layout.rightMargin: 16
        Layout.topMargin: 8
        Layout.bottomMargin: 16
        spacing: 12
        Item {
            id: bands
            Layout.fillWidth: true
            Layout.fillHeight: true
            opacity: pane.locked ? 0.42 : 1
            Repeater {
                model: 10
                delegate: ColumnLayout {
                    id: band
                    required property int index
                    x: index * (bands.width - width) / 9
                    width: 28
                    height: bands.height
                    spacing: 8
                    EchoSlider {
                        objectName: "equalizerBand-" + band.index
                        colors: Theme.floating
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        enabled: !pane.locked
                        orientation: Qt.Vertical
                        from: -12
                        to: 12
                        stepSize: .1
                        value: pane.audio.gains[band.index]
                        Accessible.name: pane.frequencies[band.index] + qsTr(" 均衡增益")
                        Accessible.description: value.toFixed(1) + " dB"
                        onMoved: pane.audio.setEqualizerBand(band.index, value)
                    }
                    Label {
                        Layout.alignment: Qt.AlignHCenter
                        text: pane.frequencies[band.index]
                        font: Theme.font(10, Theme.boldWeight)
                        color: Theme.floating.secondaryText
                    }
                }
            }
        }
        EchoDivider {
            lineColor: Theme.withAlpha(Theme.floating.text, .05)
            Layout.fillWidth: true
        }
        Grid {
            id: presets
            Layout.fillWidth: true
            columns: 3
            spacing: 8
            Repeater {
                model: pane.audio.equalizerPresets
                delegate: PlaybackEffectPresetButton {
                    required property var modelData
                    required property int index
                    width: (presets.width - 16) / 3
                    text: modelData.name
                    enabled: !pane.locked
                    accent: JSON.stringify(pane.audio.gains) === JSON.stringify(modelData.gains)
                    onClicked: pane.audio.applyEqualizerPreset(index)
                }
            }
        }
    }
}
