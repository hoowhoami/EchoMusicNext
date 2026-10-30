pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import EchoMusic.Controls
import EchoMusic.Playback
import EchoMusic.Theme
import EchoMusic.PluginSdk

Item {
    id: root
    required property EchoPluginViewContext page
    property font baseFont: Theme.store.lyricBaseFont
    property color playedColor: Theme.text
    property color unplayedColor: Theme.secondaryText
    property bool followCurrent: true
    readonly property bool live: visible && page.active && Window.window !== null && Window.window.visible && Window.window.visibility !== Window.Minimized
    property real renderTime: page.player.positionMs
    Connections {
        target: root.page.player
        function onClockChanged(): void {
            if (root && root.live)
                root.renderTime = root.page.player.samplePosition();
        }
        function onLyricChanged(): void {
            if (root && root.live && root.followCurrent && !lines.dragging && !lines.flicking)
                lines.positionViewAtIndex(root.page.player.lyricsInfo.currentIndex, ListView.Center);
        }
    }
    onLiveChanged: if (live)
        renderTime = page.player.samplePosition()
    FrameAnimation {
        running: root.live && root.page.player.clock.isAdvancing === true && Theme.store.motionEnabled
        onTriggered: root.renderTime = root.page.player.samplePosition()
    }
    ListView {
        id: lines
        anchors.fill: parent
        clip: true
        reuseItems: true
        EchoViewPoolGuard {
            targetView: lines
        }
        cacheBuffer: Math.max(0, height)
        model: root.live ? root.page.player.lyrics : null
        spacing: 20
        onMovementStarted: root.followCurrent = false
        delegate: Item {
            id: line
            required property int index
            required property string lineText
            required property real startMs
            required property real durationMs
            required property var words
            required property string translation
            required property string romanization
            width: ListView.view.width
            height: nativeLine.textHeight + secondary.implicitHeight + 12
            LyricLine {
                id: nativeLine
                width: parent.width
                height: textHeight
                text: line.lineText
                words: line.words
                startMs: line.startMs
                durationMs: line.durationMs
                timeMs: root.renderTime
                current: root.page.player.lyricsInfo.currentIndex === line.index
                font: root.baseFont
                playedColor: root.playedColor
                unplayedColor: root.unplayedColor
                animateWords: Theme.store.motionEnabled
                wrap: true
            }
            Label {
                id: secondary
                y: nativeLine.textHeight + 6
                width: parent.width
                text: [root.page.player.lyricsInfo.translation ? line.translation : "", root.page.player.lyricsInfo.romanization ? line.romanization : ""].filter(s => s.length > 0).join("\n")
                visible: text.length > 0
                font: Theme.font(Math.max(12, root.baseFont.pixelSize * .6), Theme.regularWeight)
                color: root.unplayedColor
                wrapMode: Text.Wrap
            }
            TapHandler {
                onTapped: if (root.page.player.lyricsInfo.scrollable)
                    root.page.command("seek", {
                        value: line.startMs,
                        revision: root.page.player.revision
                    })
            }
        }
    }
    EchoSoftButton {
        visible: !root.followCurrent
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        text: qsTr("返回当前行")
        onClicked: {
            root.followCurrent = true;
            lines.positionViewAtIndex(root.page.player.lyricsInfo.currentIndex, ListView.Center);
        }
    }
}
