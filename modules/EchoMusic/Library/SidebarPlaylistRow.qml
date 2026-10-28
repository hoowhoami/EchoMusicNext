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
    readonly property bool canEdit: kind === "owned" && !defaultKind
    signal openRequested(double cloudId)
    signal editRequested(double cloudId)
    signal removeRequested(double cloudId, string title, bool owned)
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
            objectName: "sidebarPlaylistCover"

            Layout.preferredWidth: row.collapsed ? 32 : 28
            Layout.preferredHeight: row.collapsed ? 32 : 28
            source: row.coverUrl
            resourceActive: row.resourceActive
        }
        EchoTextLine {
            id: titleLabel
            objectName: "sidebarPlaylistTitle"
            visible: !row.collapsed
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            Layout.alignment: Qt.AlignVCenter
            Layout.rightMargin: (editAction.visible ? 32 : 0) + (removeAction.visible ? 32 : 0)
            text: row.title
            color: row.selected ? Theme.accentText : Theme.text
            font: Theme.font(14, row.selected ? Theme.semiboldWeight : Theme.regularWeight)
            fillTextWidth: true
            stableElisionBaseline: true
        }
    }
    EchoIconButton {
        id: editAction
        objectName: "sidebarPlaylistEdit"
        visible: !row.collapsed && row.canEdit && (row.hovered || row.visualFocus || hovered || removeAction.hovered)
        enabled: !row.busy
        anchors.right: parent.right
        anchors.rightMargin: 42
        anchors.verticalCenter: parent.verticalCenter
        width: 28
        height: 28
        padding: 7
        glyph: "pencil"
        text: qsTr("编辑歌单")
        onClicked: row.editRequested(row.cloudListId)
    }
    EchoIconButton {
        id: removeAction
        objectName: "sidebarPlaylistRemove"
        visible: !row.collapsed && !row.defaultKind && (row.hovered || row.visualFocus || hovered || editAction.hovered)
        enabled: !row.busy
        anchors.right: parent.right
        anchors.rightMargin: 10
        anchors.verticalCenter: parent.verticalCenter
        width: 28
        height: 28
        padding: 7
        glyph: "trash"
        text: row.kind === "owned" ? qsTr("删除歌单") : qsTr("取消收藏")
        onClicked: row.removeRequested(row.cloudListId, row.title, row.kind === "owned")
    }
    EchoToolTip {
        parent: row
        // The row stays hovered over child actions; their own hints take priority.
        requestedVisible: row.hovered && !editAction.hovered && !removeAction.hovered && (row.collapsed || titleLabel.isTruncated())
        text: row.title
    }
}
