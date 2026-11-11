pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtMultimedia
import QtQuick.Effects
import EchoMusic.Shell
import EchoMusic.Controls
import EchoMusic.Theme

Rectangle {
    id: surface
    required property MvDetailContext detail
    required property ShellSettings settings
    property bool presented: true
    property bool fullScreen: false
    property bool outputActive: true
    readonly property alias videoSink: video.videoSink
    signal fullscreenRequested
    function timeText(ms: real): string {
        const seconds = Math.floor(ms / 1000);
        return Math.floor(seconds / 60) + ":" + String(seconds % 60).padStart(2, "0");
    }
    layer.enabled: !fullScreen && outputActive && presented && visible
    layer.effect: MultiEffect {
        maskEnabled: true
        maskSource: roundedMask
        autoPaddingEnabled: false
    }
    Item {
        id: roundedMask
        anchors.fill: parent
        visible: false
        layer.enabled: surface.layer.enabled
        Rectangle {
            anchors.fill: parent
            color: "white"
            radius: Theme.cardRadius
        }
    }
    onOutputActiveChanged: if (outputActive)
        detail.setVideoSink(video.videoSink)

    objectName: "mvPlayerBox"
    color: "black"
    radius: surface.fullScreen ? 0 : Theme.cardRadius
    clip: true
    VideoOutput {
        id: video
        objectName: "mvVideoOutput"
        anchors.fill: parent
        fillMode: VideoOutput.PreserveAspectFit
        Component.onCompleted: if (surface.outputActive)
            surface.detail.setVideoSink(videoSink)
    }
    Connections {
        target: surface.detail
        function onPlayerChanged(): void {
            if (surface.outputActive)
                surface.detail.setVideoSink(video.videoSink);
        }
    }
    Image {
        anchors.fill: parent
        visible: !surface.detail.player
        source: surface.detail.detail.cover_url || ""
        fillMode: Image.PreserveAspectFit
        asynchronous: true
        sourceSize.width: 1120
    }
    EchoActionCursor {}
    TapHandler {
        onTapped: surface.detail.toggle()
    }
    Loader {
        anchors.fill: parent
        active: surface.outputActive && surface.presented && surface.settings.mvBarrageEnabled && surface.Window.visibility !== Window.Minimized
        sourceComponent: BarrageLayer {
            controller: surface.detail.barrage
            config: surface.settings.mvBarrageConfig
            playing: surface.detail.playing
        }
    }
    ColumnLayout {
        anchors.centerIn: parent
        width: Math.max(0, parent.width - 48)
        visible: surface.detail.loading || !surface.detail.player
        spacing: 12
        EchoSpinner {
            Layout.alignment: Qt.AlignHCenter
            visible: surface.detail.loading
            resourceActive: surface.presented
        }
        Label {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            text: surface.detail.loading ? (surface.detail.sourceHash.length ? qsTr("正在切换片源…") : qsTr("正在加载 MV …")) : surface.detail.errorMessage || qsTr("暂无可播放的视频")
            font: Theme.font(13, Theme.regularWeight)
            color: "#d6ffffff"
            wrapMode: Text.Wrap
        }
        EchoSoftButton {
            Layout.alignment: Qt.AlignHCenter
            visible: !surface.detail.loading && surface.detail.errorMessage.length > 0
            text: qsTr("重试")
            onClicked: surface.detail.reload()
        }
    }
    Rectangle {
        objectName: "mvControlBar"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 48
        color: "#b3000000"
        visible: !!surface.detail.player
        RowLayout {
            objectName: "mvControlRow"
            anchors.fill: parent
            anchors.leftMargin: 8
            anchors.rightMargin: 12
            spacing: 8
            EchoIconButton {
                objectName: "mvPlayPause"
                glyph: surface.detail.playing ? "pause" : "play"
                text: surface.detail.playing ? qsTr("暂停") : qsTr("播放")
                foreground: "white"
                onClicked: surface.detail.toggle()
            }
            EchoTextTagRow {
                objectName: "mvPositionText"
                Layout.fillHeight: true
                text: surface.timeText(surface.detail.position)
                color: "white"
                font: Theme.font(11, Theme.regularWeight)
            }
            EchoSlider {
                objectName: "mvSeek"
                Layout.fillWidth: true
                from: 0
                to: Math.max(1, surface.detail.duration)
                value: surface.detail.position
                progress: true
                Accessible.name: qsTr("视频进度")
                onMoved: surface.detail.seek(value)
            }
            EchoTextTagRow {
                objectName: "mvDurationText"
                Layout.fillHeight: true
                text: surface.timeText(surface.detail.duration)
                color: "white"
                font: Theme.font(11, Theme.regularWeight)
            }
            EchoIconButton {
                objectName: "mvMute"
                glyph: surface.detail.volume === 0 ? "volume-off" : "volume"
                text: qsTr("静音")
                foreground: "white"
                onClicked: surface.detail.volume = surface.detail.volume === 0 ? .5 : 0
            }
            EchoSlider {
                objectName: "mvVolume"
                Layout.preferredWidth: 80
                from: 0
                to: 1
                value: surface.detail.volume
                Accessible.name: qsTr("视频音量")
                onMoved: surface.detail.volume = value
            }
            EchoIconButton {
                objectName: "mvFullscreen"
                glyph: surface.fullScreen ? "arrows-minimize" : "arrows-maximize"
                text: surface.fullScreen ? qsTr("退出全屏") : qsTr("全屏")
                foreground: "white"
                onClicked: surface.fullscreenRequested()
            }
        }
    }
}
