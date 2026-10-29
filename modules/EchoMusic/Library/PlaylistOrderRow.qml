pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Theme

ItemDelegate {
    id: row
    required property int index
    required property string title
    required property string subtitle
    property bool selected: false
    property bool lifted: false
    property bool interactive: true
    signal selectedRequested(int index)
    width: parent ? parent.width : 0
    height: 52
    leftPadding: 14
    rightPadding: 14
    topPadding: 0
    bottomPadding: 0
    hoverEnabled: true
    enabled: interactive
    Accessible.name: title
    onClicked: selectedRequested(index)
    background: Rectangle {
        anchors.fill: parent
        anchors.margins: 2
        radius: Theme.controlRadius
        color: row.lifted ? "transparent" : row.selected ? Theme.withAlpha(Theme.floating.accent, 0.07) : row.hovered ? Theme.floating.hover : "transparent"
        border.width: 1
        border.color: row.selected && !row.lifted ? Theme.withAlpha(Theme.floating.accent, 0.18) : "transparent"
    }
    contentItem: RowLayout {
        spacing: 10
        Grid {
            Layout.preferredWidth: 10
            Layout.preferredHeight: 14
            columns: 2
            spacing: 3
            Repeater {
                model: 6
                delegate: Rectangle {
                    width: 2
                    height: 2
                    radius: 1
                    color: row.selected || row.lifted ? Theme.floating.accentText : Theme.floatingSecondaryText
                }
            }
        }
        Label {
            objectName: "playlistOrderIndex"
            Layout.alignment: Qt.AlignBaseline
            text: row.index + 1
            Layout.preferredWidth: 34
            horizontalAlignment: Text.AlignRight
            color: Theme.floatingSecondaryText
            font: Theme.font(12, Theme.regularWeight)
        }
        Label {
            objectName: "playlistOrderTitle"
            Layout.alignment: Qt.AlignBaseline
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            text: row.title
            textFormat: Text.PlainText
            maximumLineCount: 1
            wrapMode: Text.NoWrap
            elide: Text.ElideRight
            color: Theme.floatingText
            font: Theme.font(13, Theme.mediumWeight)
        }
        Label {
            objectName: "playlistOrderSubtitle"
            Layout.alignment: Qt.AlignBaseline
            Layout.fillWidth: false
            Layout.minimumWidth: 0
            // The row width belongs to the virtual view/drag loader, so this
            // bound never depends on the content layout's computed width.
            Layout.maximumWidth: row.width * 0.4
            visible: text.length > 0
            text: row.subtitle
            textFormat: Text.PlainText
            maximumLineCount: 1
            wrapMode: Text.NoWrap
            elide: Text.ElideRight
            horizontalAlignment: Text.AlignRight
            color: Theme.floatingSecondaryText
            font: Theme.font(11, Theme.regularWeight)
        }
    }
}
