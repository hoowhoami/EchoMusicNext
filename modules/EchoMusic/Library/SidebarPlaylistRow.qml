pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Controls
import EchoMusic.Theme

ItemDelegate {
    id: row
    objectName: "sidebarPlaylistRow"
    required property string title
    required property string coverUrl
    required property double cloudListId
    required property string globalId
    required property string creatorGlobalId
    required property string kind
    required property var defaultKind
    property bool collapsed: false
    property bool selected: false
    property bool resourceActive: true
    property bool busy: false
    signal openRequested(double cloudId)
    property bool playbackBusy: false
    signal playRequested(double cloudId)
    height: collapsed ? 42 : 40
    padding: collapsed ? 5 : 14
    topPadding: collapsed ? 5 : 6
    bottomPadding: collapsed ? 5 : 6
    hoverEnabled: true
    Accessible.name: title
    onClicked: openRequested(cloudListId)
    background: Rectangle {
        radius: row.collapsed ? Theme.controlRadius : Theme.cardRadius
        color: row.selected ? Theme.selected : row.hovered || row.visualFocus ? Theme.rowHover : "transparent"
    }
    contentItem: RowLayout {
        spacing: 12
        EchoCover {
            id: playlistCover
            objectName: "sidebarPlaylistCover"

            Layout.preferredWidth: row.collapsed ? 32 : 28
            Layout.preferredHeight: row.collapsed ? 32 : 28
            source: row.coverUrl
            placeholderOpacity: playAction.visible ? 0 : 0.3
            resourceActive: row.resourceActive
        }
        EchoTextLine {
            id: titleLabel
            objectName: "sidebarPlaylistTitle"
            visible: !row.collapsed
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            Layout.alignment: Qt.AlignVCenter
            text: row.title
            color: row.selected ? Theme.accentText : Theme.text
            font: Theme.font(14, row.selected ? Theme.semiboldWeight : Theme.regularWeight)
            fillTextWidth: true
            stableElisionBaseline: true
        }
    }
    EchoPlaybackButton {
        id: playAction
        objectName: "sidebarPlaylistPlay"
        parent: playlistCover
        visible: !row.collapsed && (row.hovered || row.visualFocus || hovered || visualFocus || row.playbackBusy)
        enabled: !row.busy && !row.playbackBusy
        visualContentActive: row.resourceActive
        loading: row.playbackBusy
        anchors.fill: parent
        padding: 7
        foreground: hoverHighlighted || visualFocus ? Theme.accentText : "white"
        text: row.playbackBusy ? qsTr("正在读取歌单…") : qsTr("播放歌单")
        background: Rectangle {
            radius: playlistCover.cornerRadius
            color: Qt.rgba(0, 0, 0, playAction.down ? .6 : .45)
        }
        onClicked: row.playRequested(row.cloudListId)
    }
    // A disabled child button otherwise passes its click to the playlist row.
    MouseArea {
        parent: playlistCover
        anchors.fill: playAction
        enabled: playAction.visible && !playAction.enabled
        acceptedButtons: Qt.LeftButton
    }
    EchoToolTip {
        parent: row
        // The row stays hovered over child actions; their own hints take priority.
        requestedVisible: row.hovered && !playAction.hovered && (row.collapsed || titleLabel.isTruncated())
        text: row.title
    }
}
