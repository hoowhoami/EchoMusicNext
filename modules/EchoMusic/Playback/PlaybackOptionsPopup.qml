pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Theme
import EchoMusic.Controls
import EchoMusic.Playback

EchoAnchoredPopup {
    id: popup
    onAboutToShow: if (kind === "quality")
        playback.ensureQualities()
    objectName: "playbackOptionsPopup"
    required property PlaybackService playback
    property string kind: "volume"
    preferredWidth: kind === "volume" ? 48 : kind === "speed" ? 360 : kind === "play-mode" ? 184 : 208
    // Fit the bounded option list to available window space; scroll when constrained.
    maximumHeight: kind === "quality" ? implicitHeight : 420
    preferAbove: true
    alignEnd: false
    showArrow: false
    sideOffset: 8
    padding: kind === "volume" ? 12 : 6
    topPadding: kind === "speed" ? 14 : padding
    bottomPadding: kind === "speed" ? 12 : padding
    leftPadding: kind === "speed" ? 16 : padding
    rightPadding: kind === "speed" ? 16 : padding
    implicitHeight: contents.implicitHeight + topPadding + bottomPadding
    onOpened: scroll.contentY = 0
    contentItem: Flickable {
        id: scroll
        objectName: "playbackOptionsViewport"
        clip: true
        contentWidth: width
        contentHeight: contents.implicitHeight
        boundsBehavior: Flickable.StopAtBounds
        flickableDirection: Flickable.VerticalFlick
        ScrollBar.vertical: EchoPopupScrollBar {
            surface: popup
            view: scroll
            resourceActive: popup.visible
        }
        ColumnLayout {
            id: contents
            width: scroll.width
            spacing: popup.kind === "speed" ? 12 : popup.kind === "volume" ? 6 : 2
            ColumnLayout {
                visible: popup.kind === "volume"
                Layout.alignment: Qt.AlignHCenter
                spacing: 6
                EchoSlider {
                    colors: Theme.floating
                    objectName: "playbackVolumeSlider"
                    Layout.preferredWidth: 24
                    Layout.preferredHeight: 98
                    padding: 0
                    orientation: Qt.Vertical
                    from: 0
                    to: 100
                    value: popup.playback.volume
                    onMoved: popup.playback.setVolume(value)
                    Accessible.name: qsTr("音量")
                }
                Label {
                    Layout.alignment: Qt.AlignHCenter
                    text: Math.round(popup.playback.volume)
                    font: Theme.font(10, Theme.boldWeight)
                    color: Theme.floating.secondaryText
                }
                EchoIconButton {
                    colors: Theme.floating
                    Layout.preferredWidth: 24
                    Layout.preferredHeight: 24
                    // The mute target is 24px with a 16px icon.
                    objectName: "playbackVolumeMute"
                    padding: 4
                    hoverBackground: false
                    glyph: popup.playback.volume === 0 ? "volume-3" : popup.playback.volume < 50 ? "volume-2" : "volume"
                    text: popup.playback.volume === 0 ? qsTr("取消静音") : qsTr("静音")
                    onClicked: popup.playback.toggleMute()
                }
            }
            ColumnLayout {
                visible: popup.kind === "speed"
                enabled: !popup.playback.cast.remoteMedia
                Layout.fillWidth: true
                spacing: 12
                RowLayout {
                    Layout.fillHeight: false
                    spacing: 5
                    Layout.fillWidth: true
                    Label {
                        text: qsTr("倍速播放")
                        font: Theme.font(11, Theme.boldWeight)
                        color: Theme.floating.secondaryText
                        Layout.fillWidth: true
                    }
                    Button {
                        id: reset
                        implicitWidth: 44
                        implicitHeight: 22
                        hoverEnabled: true
                        text: hovered || visualFocus ? qsTr("重置") : popup.playback.speed.toFixed(2) + "x"
                        font: Theme.font(11, Theme.semiboldWeight)
                        background: Item {}
                        contentItem: Label {
                            text: reset.text
                            font: reset.font
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                            color: reset.hovered ? Theme.floating.accentText : Theme.floating.secondaryText
                        }
                        onClicked: popup.playback.setSpeed(1)
                        Accessible.name: qsTr("重置播放倍速")
                    }
                }
                RowLayout {
                    Layout.fillHeight: false
                    Layout.fillWidth: true
                    spacing: 8
                    Label {
                        text: "0.1"
                        color: Theme.floating.secondaryText
                        font: Theme.font(10, Theme.semiboldWeight)
                    }
                    EchoSlider {
                        colors: Theme.floating
                        Layout.fillWidth: true
                        from: .1
                        to: 5
                        stepSize: .1
                        value: popup.playback.speed
                        onMoved: popup.playback.setSpeed(value)
                        Accessible.name: qsTr("播放倍速")
                    }
                    Label {
                        text: "5x"
                        color: Theme.floating.secondaryText
                        font: Theme.font(10, Theme.semiboldWeight)
                    }
                }
                GridLayout {
                    Layout.fillWidth: true
                    columns: popup.width < 340 ? 4 : 7
                    columnSpacing: 6
                    rowSpacing: 6
                    Repeater {
                        model: [0.5, 0.75, 1, 1.25, 1.5, 2, 3]
                        EchoSoftButton {
                            colors: Theme.floating
                            required property real modelData
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            implicitHeight: 28
                            leftPadding: 4
                            rightPadding: 4
                            font: Theme.font(11, Theme.semiboldWeight)
                            text: (Math.floor(modelData) === modelData ? modelData.toFixed(1) : modelData) + "x"
                            accent: Math.abs(popup.playback.speed - modelData) < .01
                            onClicked: popup.playback.setSpeed(modelData)
                        }
                    }
                }
            }
            ColumnLayout {
                visible: popup.kind === "quality"
                Layout.fillWidth: true
                Layout.leftMargin: 10
                Layout.rightMargin: 10
                Layout.topMargin: 6
                Layout.bottomMargin: 10
                spacing: 4
                Label {
                    text: qsTr("音质选择")
                    font: Theme.font(12, Theme.boldWeight)
                    color: Theme.floating.text
                }
                Label {
                    objectName: "playbackSourceStatus"
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    text: !popup.playback.hasTrack ? qsTr("暂无歌曲") : popup.playback.loading ? qsTr("正在切换音质…") : popup.playback.sourceNotice || qsTr("播放音质")
                    font: Theme.font(11, Theme.regularWeight)
                    color: Theme.floating.secondaryText
                }
            }
            Label {
                visible: popup.kind === "quality" && popup.playback.mvFallbackFailureReason.length > 0
                Layout.fillWidth: true
                Layout.leftMargin: 10
                Layout.rightMargin: 10
                text: qsTr("MV 音频兜底失败：%1").arg(popup.playback.mvFallbackFailureReason)
                wrapMode: Text.Wrap
                font: Theme.font(11, Theme.regularWeight)
                color: Theme.floating.secondaryText
            }
            Repeater {
                model: popup.kind === "quality" ? popup.playback.qualities : popup.kind === "play-mode" ? [
                    {
                        value: PlaybackService.Sequential,
                        label: qsTr("顺序播放"),
                        icon: "repeat-off"
                    },
                    {
                        value: PlaybackService.ListLoop,
                        label: qsTr("列表循环"),
                        icon: "repeat"
                    },
                    {
                        value: PlaybackService.Shuffle,
                        label: qsTr("随机播放"),
                        icon: "arrows-shuffle"
                    },
                    {
                        value: PlaybackService.SingleLoop,
                        label: qsTr("单曲循环"),
                        icon: "repeat-once"
                    }
                ] : []
                delegate: ColumnLayout {
                    id: optionRow
                    objectName: "playbackOptionRow-" + modelData.value
                    required property var modelData
                    Layout.fillWidth: true
                    spacing: 0
                    Button {
                        id: option
                        readonly property var modelData: optionRow.modelData
                        objectName: "playbackOption-" + modelData.value
                        readonly property bool selected: popup.kind === "quality" ? popup.playback.effectiveQuality === modelData.value : popup.playback.playMode === modelData.value
                        Layout.fillWidth: true
                        implicitHeight: popup.kind === "quality" ? 52 : 36
                        padding: 0
                        leftPadding: 10
                        rightPadding: 10
                        hoverEnabled: true
                        enabled: popup.kind === "quality" ? !!modelData.enabled && !popup.playback.loading : true
                        opacity: enabled ? 1 : .5
                        background: Rectangle {
                            radius: Theme.controlRadius
                            color: option.selected ? Theme.floating.mutedSurface : option.hovered ? Theme.floating.hover : "transparent"
                        }
                        contentItem: RowLayout {
                            spacing: popup.kind === "quality" ? 8 : 10
                            EchoIcon {
                                visible: popup.kind === "play-mode"
                                Layout.fillWidth: false
                                Layout.fillHeight: false
                                // kind changes before Repeater replaces its rows;
                                // the old quality row deliberately has no icon.
                                name: option.modelData.icon || ""
                                color: option.selected ? Theme.floating.accentText : Theme.floating.iconMain
                                Layout.preferredWidth: 20
                                Layout.maximumWidth: 20
                                Layout.preferredHeight: 20
                            }
                            ColumnLayout {
                                Layout.fillHeight: false
                                objectName: "playbackOptionLabels"
                                Layout.fillWidth: true
                                Layout.minimumWidth: 0
                                Layout.alignment: Qt.AlignVCenter
                                spacing: 0
                                EchoTextTagRow {
                                    objectName: "playbackOptionTitle"
                                    Layout.fillWidth: true
                                    Layout.minimumWidth: 0
                                    text: option.modelData.label
                                    font: Theme.font(popup.kind === "quality" ? 13 : 12, popup.kind === "quality" ? Theme.semiboldWeight : Theme.regularWeight)
                                    lineHeight: popup.kind === "quality" ? 20 : 18
                                    color: option.selected ? Theme.floating.accentText : Theme.floating.text
                                }
                                EchoTextTagRow {
                                    objectName: "playbackOptionDetail"
                                    Layout.fillWidth: true
                                    Layout.minimumWidth: 0
                                    visible: popup.kind === "quality"
                                    text: option.modelData.detail || " "
                                    font: Theme.font(11, Theme.regularWeight)
                                    lineHeight: 16
                                    color: Theme.floating.secondaryText
                                }
                            }
                            EchoTag {
                                objectName: "playbackOptionTag"
                                // Only the label column grows. Explicit hints prevent Qt size-policy stretch.
                                Layout.fillWidth: false
                                Layout.fillHeight: false
                                Layout.minimumWidth: Math.ceil(implicitWidth)
                                Layout.maximumWidth: Math.ceil(implicitWidth)
                                Layout.alignment: Qt.AlignVCenter
                                colors: Theme.floating
                                visible: popup.kind === "quality"
                                text: option.modelData.tag || ""
                                tone: EchoTag.Custom
                                customTone: option.modelData.color || Theme.accent
                            }
                            EchoIcon {
                                objectName: "playbackOptionCheck"
                                Layout.fillWidth: false
                                Layout.fillHeight: false
                                name: "check"
                                opacity: option.selected ? 1 : 0
                                Layout.preferredWidth: 14
                                Layout.minimumWidth: 14
                                Layout.maximumWidth: 14
                                Layout.preferredHeight: 14
                                Layout.minimumHeight: 14
                                Layout.maximumHeight: 14
                                color: Theme.floating.accentText
                            }
                        }
                        onClicked: {
                            if (popup.kind === "quality")
                                popup.playback.setQuality(modelData.value);
                            else {
                                popup.playback.setPlayMode(modelData.value);
                                popup.close();
                            }
                        }
                        Accessible.name: modelData.label
                    }
                }
            }
        }
    }
}
