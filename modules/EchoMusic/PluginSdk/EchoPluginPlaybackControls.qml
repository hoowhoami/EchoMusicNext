pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import EchoMusic.Controls
import EchoMusic.Theme
import EchoMusic.PluginSdk

RowLayout {
    id: root
    required property EchoPluginViewContext page
    property ThemeColors colors: Theme.colors
    spacing: 6
    EchoIconButton {
        glyph: "player-skip-back"
        text: qsTr("上一首")
        colors: root.colors
        enabled: root.page.active && root.page.player.hasTrack
        implicitWidth: 38
        implicitHeight: 38
        onClicked: root.page.command("previous")
    }
    EchoIconButton {
        objectName: "pluginPlayToggle"
        animatePlayPause: true
        glyph: root.page.player.playing ? "pause" : "play"
        text: root.page.player.playing ? qsTr("暂停") : qsTr("播放")
        colors: root.colors
        enabled: root.page.active && root.page.player.hasTrack
        implicitWidth: 38
        implicitHeight: 38
        onClicked: root.page.command("toggle")
    }
    EchoIconButton {
        glyph: "player-skip-forward"
        text: qsTr("下一首")
        colors: root.colors
        enabled: root.page.active && root.page.player.hasTrack
        implicitWidth: 38
        implicitHeight: 38
        onClicked: root.page.command("next")
    }
}
