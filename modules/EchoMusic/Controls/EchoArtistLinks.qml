pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import EchoMusic.Theme

// Each retained domain artist owns one native button. Text-only fallback names
// remain readable without inventing a catalog identity.
Item {
    id: links
    property var artists: []
    property font font: Theme.font(12, Theme.mediumWeight)
    property color color: Theme.secondaryText
    property color hoverColor: Theme.accentText
    property real lineHeight: font.pixelSize * 1.5
    property string textObjectName: ""
    property real separatorSpacing: 4
    property bool linksEnabled: true
    property bool requireCompleteName: false
    readonly property bool truncated: names.implicitWidth > width
    signal artistRequested(string id)
    implicitWidth: names.implicitWidth
    implicitHeight: lineHeight
    clip: true
    TextMetrics {
        id: separatorMetrics
        font: links.font
        text: "/"
    }
    Row {
        id: names
        height: links.height
        spacing: links.separatorSpacing
        Repeater {
            model: links.artists
            delegate: Item {
                id: entry
                required property var modelData
                required property int index
                readonly property bool hasSeparator: index < links.artists.length - 1
                readonly property bool showSeparator: hasSeparator && x + button.implicitWidth + separatorMetrics.advanceWidth + links.separatorSpacing * 2 < links.width
                implicitWidth: button.implicitWidth + (hasSeparator ? separatorMetrics.advanceWidth + links.separatorSpacing : 0)
                height: links.height
                Row {
                    height: entry.height
                    spacing: entry.showSeparator ? links.separatorSpacing : 0
                    AbstractButton {
                        id: button
                        objectName: "artistLink-" + entry.index
                        text: entry.modelData.name
                        enabled: links.linksEnabled && entry.modelData.id.length > 0 && (!links.requireCompleteName || text.trim().length > 0 && width > 0 && width + 0.01 >= implicitWidth)
                        focusPolicy: enabled ? Qt.StrongFocus : Qt.NoFocus
                        hoverEnabled: true
                        padding: 0
                        width: Math.max(0, Math.min(label.implicitWidth, links.width - entry.x - (entry.showSeparator ? separatorMetrics.advanceWidth + links.separatorSpacing * 2 : 0)))
                        height: links.height
                        implicitWidth: label.implicitWidth
                        background: null
                        contentItem: EchoTextLine {
                            id: label
                            textItem.objectName: entry.index === 0 ? links.textObjectName : ""
                            text: button.text
                            font: links.font
                            color: button.hovered || button.visualFocus ? links.hoverColor : links.color
                            lineHeight: links.lineHeight
                        }
                        HoverHandler {
                            cursorShape: Qt.PointingHandCursor
                            enabled: button.enabled
                        }
                        onClicked: links.artistRequested(entry.modelData.id)
                    }
                    Loader {
                        active: entry.showSeparator
                        visible: active
                        width: separatorMetrics.advanceWidth
                        height: links.height
                        sourceComponent: EchoTextLine {
                            lineHeight: links.lineHeight
                            text: "/"
                            color: links.color
                            font: links.font
                        }
                    }
                }
            }
        }
    }
}
