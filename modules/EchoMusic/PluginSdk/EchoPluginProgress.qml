pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Controls
import EchoMusic.Theme
import EchoMusic.PluginSdk

RowLayout {
    id: root
    required property EchoPluginViewContext page
    property ThemeColors colors: Theme.colors
    spacing: 12
    function time(ms: real): string {
        const s = Math.max(0, Math.floor(ms / 1000));
        return String(Math.floor(s / 60)).padStart(2, "0") + ":" + String(s % 60).padStart(2, "0");
    }
    Label {
        text: root.time(slider.pressed ? slider.value : root.page.player.positionMs)
        color: root.colors.secondaryText
        font: Theme.font(11, Theme.mediumWeight)
        Layout.preferredWidth: 42
        horizontalAlignment: Text.AlignRight
    }
    EchoSlider {
        id: slider
        objectName: "pluginSeekSlider"
        property string dragRevision
        onPressedChanged: if (pressed)
            dragRevision = root.page.player.revision
        Layout.fillWidth: true
        colors: root.colors
        progress: true
        enabled: root.page.active && root.page.player.hasTrack && !root.page.player.loading
        from: 0
        to: Math.max(1, root.page.player.durationMs)
        value: root.page.player.positionMs
        onMoved: root.page.command("seek", {
            value: value,
            revision: slider.dragRevision
        })
    }
    Label {
        text: root.time(root.page.player.durationMs)
        color: root.colors.secondaryText
        font: Theme.font(11, Theme.mediumWeight)
        Layout.preferredWidth: 42
    }
}
