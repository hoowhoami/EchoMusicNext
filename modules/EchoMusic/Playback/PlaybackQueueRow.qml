pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Theme
import EchoMusic.Controls

ItemDelegate {
    id: row
    required property int index
    required property string title
    required property string artistText
    required property string coverUrl
    required property bool isCurrent
    property bool viewActive: false
    property bool playing: false
    property bool resourceActive: true
    property bool actionsEnabled: true
    readonly property bool active: isCurrent && viewActive
    signal playRequested(int index)
    signal removeRequested(int index)
    width: parent ? parent.width : 0
    height: 56
    hoverEnabled: true
    leftPadding: 10
    rightPadding: 12
    topPadding: 0
    bottomPadding: 0
    onDoubleClicked: if (actionsEnabled)
        playRequested(index)
    background: Rectangle {
        radius: Theme.controlRadius
        color: row.active || row.hovered ? Theme.floating.hover : "transparent"
    }
    contentItem: RowLayout {
        spacing: 10
        Item {
            Layout.preferredWidth: 52
            Layout.fillHeight: true
            Label {
                anchors.centerIn: parent
                visible: !row.hovered && !row.active
                text: row.index + 1
                font: Theme.font(12, Theme.regularWeight)
                color: Theme.floating.secondaryText
            }
            EchoIconButton {
                colors: Theme.floating
                anchors.centerIn: parent
                visible: row.hovered || row.active
                glyph: row.active && row.playing ? "player-pause" : "play"
                text: row.active && row.playing ? qsTr("暂停") : qsTr("播放")
                foreground: row.active ? Theme.floating.accentText : Theme.floating.text
                implicitWidth: 28
                implicitHeight: 28
                padding: 6
                background: Item {}
                onClicked: row.playRequested(row.index)
            }
        }
        EchoCover {
            colors: Theme.floating
            Layout.preferredWidth: 46
            Layout.preferredHeight: 46
            source: row.coverUrl
            resourceActive: row.resourceActive
        }
        ColumnLayout {
            Layout.fillHeight: false
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            spacing: 2
            Label {
                Layout.fillWidth: true
                text: row.title
                font: Theme.font(13, Theme.semiboldWeight)
                color: row.active ? Theme.floating.accentText : Theme.floating.text
                elide: Text.ElideRight
            }
            Label {
                Layout.fillWidth: true
                text: row.artistText
                font: Theme.font(12, Theme.regularWeight)
                color: Theme.floating.secondaryText
                elide: Text.ElideRight
            }
        }
        Item {
            Layout.preferredWidth: 28
            Layout.fillHeight: true
            EchoIconButton {
                colors: Theme.floating
                anchors.centerIn: parent
                visible: row.actionsEnabled && row.hovered && row.viewActive
                objectName: "queueRemoveButton"
                glyph: "x"
                text: qsTr("移出队列")
                implicitWidth: 24
                implicitHeight: 24
                padding: 5
                onClicked: row.removeRequested(row.index)
            }
        }
    }
}
