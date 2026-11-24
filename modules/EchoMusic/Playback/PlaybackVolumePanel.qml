pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import EchoMusic.Theme
import EchoMusic.Controls
import EchoMusic.Playback

// All local volume surfaces share this presentation; PlaybackService owns gain,
// casting limits and mute restoration. Opening the explanation is view state.
Item {
    id: panel
    objectName: "playbackVolumePanel"
    required property PlaybackService playback
    property ThemeColors colors: Theme.floating
    property bool explanationExpanded: false
    readonly property real maximumVolume: playback.cast.remote ? 100 : 150
    readonly property bool boosted: maximumVolume > 100 && playback.volume > 100
    readonly property color gainColor: Qt.rgba(colors.warning.r * .72 + colors.text.r * .28, colors.warning.g * .72 + colors.text.g * .28, colors.warning.b * .72 + colors.text.b * .28, 1)
    implicitWidth: 280
    implicitHeight: layout.implicitHeight
    ColumnLayout {
        id: layout
        width: panel.width
        spacing: 0
        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            EchoTextLine {
                text: qsTr("音量")
                font: Theme.font(13, Theme.semiboldWeight)
                color: panel.colors.text
            }
            EchoIconButton {
                objectName: "playbackVolumeMute"
                colors: panel.colors
                implicitWidth: 28
                implicitHeight: 28
                padding: 6
                hoverBackground: false
                glyph: panel.playback.volume === 0 ? "volume-off" : "volume-2"
                text: panel.playback.volume === 0 ? qsTr("取消静音") : qsTr("静音")
                onClicked: panel.playback.toggleMute()
            }
            Item {
                Layout.fillWidth: true
            }
            RowLayout {
                spacing: 3
                EchoTextTagRow {
                    objectName: "playbackVolumeValue"
                    Layout.alignment: Qt.AlignBaseline
                    text: Math.round(panel.playback.volume).toString()
                    font: Theme.font(26, Theme.regularWeight)
                    color: panel.colors.text
                    alignFontBaseline: true
                    lineHeight: 34
                }
                EchoTextTagRow {
                    Layout.alignment: Qt.AlignBaseline
                    text: "%"
                    font: Theme.font(11, Theme.regularWeight)
                    color: panel.colors.secondaryText
                    alignFontBaseline: true
                    lineHeight: 17
                }
            }
        }
        EchoSlider {
            id: slider
            objectName: "playbackVolumeSlider"
            colors: panel.colors
            Layout.fillWidth: true
            Layout.topMargin: 14
            Layout.preferredHeight: 24
            padding: 0
            from: 0
            to: panel.maximumVolume
            stepSize: 1
            value: panel.playback.volume
            onMoved: panel.playback.setVolume(value)
            Accessible.name: qsTr("音量")
            Accessible.description: panel.boosted ? qsTr("已启用软件增益，可能影响音质") : qsTr("100% 为原始增益参考点")
            background: Item {
                id: track
                x: slider.leftPadding + slider.handle.width / 2
                y: slider.topPadding + (slider.availableHeight - height) / 2
                width: Math.max(0, slider.availableWidth - slider.handle.width)
                height: 4
                readonly property real reference: 100 / panel.maximumVolume
                readonly property real normalWidth: Math.min(slider.position, reference) * width
                readonly property real boostWidth: Math.max(0, slider.position - reference) * width
                Rectangle {
                    anchors.fill: parent
                    radius: 2
                    color: Theme.withAlpha(panel.colors.text, .12)
                }
                Rectangle {
                    visible: panel.maximumVolume > 100
                    x: slider.mirrored ? 0 : track.reference * track.width
                    width: track.width * (1 - track.reference)
                    height: parent.height
                    radius: 2
                    color: Theme.withAlpha(panel.gainColor, .14)
                }
                Rectangle {
                    x: slider.mirrored ? track.width - width : 0
                    width: track.normalWidth
                    height: parent.height
                    radius: 2
                    color: panel.colors.accent
                }
                Rectangle {
                    objectName: "playbackVolumeGainTrack"
                    visible: panel.boosted
                    x: slider.mirrored ? track.width * (1 - track.reference) - width : track.reference * track.width
                    width: track.boostWidth
                    height: parent.height
                    radius: 2
                    color: panel.gainColor
                }
                EchoDivider {
                    visible: panel.maximumVolume > 100
                    x: (slider.mirrored ? 1 - track.reference : track.reference) * track.width
                    y: -2
                    height: 8
                    orientation: Qt.Vertical
                    lineColor: panel.colors.secondaryText
                }
            }
        }
        Item {
            Layout.fillWidth: true
            implicitHeight: 24
            EchoTextLine {
                x: slider.handle.width / 2
                text: "0"
                color: panel.colors.secondaryText
                font: Theme.font(11, Theme.regularWeight)
            }
            EchoActionButton {
                id: referenceAction
                objectName: "playbackVolumeReference"
                visible: panel.maximumVolume > 100
                x: slider.handle.width / 2 + (parent.width - slider.handle.width) * (slider.mirrored ? 1 - 100 / panel.maximumVolume : 100 / panel.maximumVolume) - width / 2
                width: 38
                height: 22
                padding: 0
                text: "100"
                Accessible.name: qsTr("设为 100% 音量")
                onClicked: panel.playback.setVolume(100)
                contentItem: EchoTextLine {
                    fillTextWidth: true
                    text: "100"
                    font: Theme.font(11, Theme.regularWeight)
                    color: referenceAction.hovered || referenceAction.visualFocus ? panel.colors.accentText : panel.colors.secondaryText
                    horizontalAlignment: Text.AlignHCenter
                }
                background: Item {
                    EchoDashedFrame {
                        anchors.fill: parent
                        visible: referenceAction.visualFocus
                        solid: true
                        strokeColor: panel.colors.accent
                        radius: Theme.detailRadius
                    }
                }
            }
            EchoTextLine {
                anchors.right: parent.right
                anchors.rightMargin: slider.handle.width / 2
                text: panel.maximumVolume.toString()
                color: panel.colors.secondaryText
                font: Theme.font(11, Theme.regularWeight)
            }
        }
        EchoDivider {
            Layout.fillWidth: true
            Layout.topMargin: 4
            lineColor: panel.colors.surfaceOutline
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 38
            spacing: 6
            Rectangle {
                Layout.preferredWidth: 4
                Layout.preferredHeight: 4
                radius: 2
                color: panel.boosted ? panel.gainColor : panel.colors.secondaryText
            }
            EchoTextLine {
                objectName: "playbackVolumeStatus"
                Layout.fillWidth: true
                fillTextWidth: true
                text: panel.boosted ? qsTr("增益可能影响音质") : panel.playback.cast.remote ? qsTr("投屏设备音量") : qsTr("标准音量")
                font: Theme.font(11, Theme.regularWeight)
                color: panel.colors.secondaryText
            }
            EchoActionButton {
                id: restore
                objectName: "playbackVolumeRestore"
                visible: panel.boosted
                padding: 0
                implicitWidth: contentItem.implicitWidth + 8
                implicitHeight: 28
                text: qsTr("调回 100%")
                Accessible.name: qsTr("将音量调回 100%，关闭额外增益")
                onClicked: panel.playback.setVolume(100)
                contentItem: EchoTextLine {
                    fillTextWidth: true
                    text: restore.text
                    color: panel.colors.accentText
                    font: Theme.font(11, Theme.semiboldWeight)
                    horizontalAlignment: Text.AlignHCenter
                }
                background: Item {
                    EchoDashedFrame {
                        anchors.fill: parent
                        visible: restore.visualFocus
                        solid: true
                        strokeColor: panel.colors.accent
                        radius: Theme.detailRadius
                    }
                }
            }
            EchoIconButton {
                objectName: "playbackVolumeInfo"
                colors: panel.colors
                implicitWidth: 28
                implicitHeight: 28
                padding: 7
                glyph: "info-circle"
                text: panel.explanationExpanded ? qsTr("收起音量说明") : qsTr("音量与软件增益说明")
                onClicked: panel.explanationExpanded = !panel.explanationExpanded
                Accessible.description: panel.explanationExpanded ? qsTr("已展开") : qsTr("已收起")
            }
        }
        Loader {
            Layout.fillWidth: true
            active: panel.explanationExpanded
            visible: active
            sourceComponent: ColumnLayout {
                spacing: 10
                EchoDivider {
                    Layout.fillWidth: true
                    lineColor: panel.colors.surfaceOutline
                }
                EchoTextLine {
                    objectName: "playbackVolumeExplanation"
                    Layout.fillWidth: true
                    fillTextWidth: true
                    wrap: true
                    font: Theme.font(11, Theme.regularWeight)
                    color: panel.colors.secondaryText
                    text: panel.playback.cast.remote ? qsTr("当前控制投屏设备的音量，最高为 100%。") : qsTr("100% 为原始增益。更高音量通过软件放大，可能影响音质；出现破音时请调低。")
                    lineHeight: 18
                }
                EchoTextLine {
                    objectName: "playbackVolumeGainDetails"
                    Layout.fillWidth: true
                    fillTextWidth: true
                    font: Theme.font(11, Theme.regularWeight)
                    color: panel.colors.secondaryText
                    text: panel.boosted ? qsTr("当前额外增益 +%1 dB").arg((20 * Math.log10(panel.playback.volume / 100)).toFixed(1)) : panel.playback.cast.remote ? qsTr("最高音量 100%") : qsTr("原始增益参考 100%")
                    lineHeight: 18
                }
            }
        }
    }
}
