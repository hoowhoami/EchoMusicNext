pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import EchoMusic.Theme

EchoActionButton {
    id: header
    required property var adapter
    required property var entry
    objectName: "pluginColumnHeader-" + entry.key
    text: entry.label || ""
    enabled: entry.enabled !== false && !!entry.command
    padding: 0
    background: null
    contentItem: Text {
        text: header.text
        textFormat: Text.PlainText
        elide: Text.ElideRight
        verticalAlignment: Text.AlignVCenter
        color: header.hovered ? Theme.accentText : Theme.secondaryText
        font: Theme.font(12, Theme.boldWeight)
    }
    onClicked: adapter.invokeEntry(entry.token, entry.key, false)
}
