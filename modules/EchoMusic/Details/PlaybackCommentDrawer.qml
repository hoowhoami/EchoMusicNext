pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Controls
import EchoMusic.Theme

EchoDrawer {
    id: drawer
    objectName: "playbackCommentDrawer"
    property bool resourceActive: true
    required property PlaybackDiscussion service
    readonly property CommentsController controller: service.comments
    preferredWidth: 460
    onAboutToHide: service.closeComments()
    contentItem: Item {
        ColumnLayout {
            id: header
            width: parent.width
            height: implicitHeight
            spacing: 0
            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 24
                Layout.topMargin: 16
                Layout.bottomMargin: 14
                Layout.preferredHeight: 32
                Layout.minimumHeight: 32
                Layout.maximumHeight: 32
                Layout.fillHeight: false
                Layout.rightMargin: 24
                spacing: 8
                EchoTextTagRow {
                    Layout.fillWidth: true
                    text: qsTr("评论")
                    font: Theme.font(16, Theme.boldWeight)
                    color: Theme.floatingText
                    Label {
                        visible: Number(drawer.controller.totalText) > 0
                        text: drawer.controller.totalText + qsTr(" 条")
                        color: Theme.floatingSecondaryText
                        font: Theme.font(12, Theme.semiboldWeight)
                    }
                }
                EchoIconButton {
                    objectName: "playbackCommentClose"
                    implicitWidth: 32
                    implicitHeight: 32
                    padding: 9
                    glyph: "x"
                    text: qsTr("关闭评论")
                    tooltipText: ""
                    onClicked: drawer.close()
                }
            }
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: 1
                color: Theme.subtleBorder
            }
        }
        Item {
            id: body
            clip: true
            anchors.top: header.bottom
            anchors.bottom: parent.bottom
            width: parent.width
            CommentsView {
                anchors.fill: parent
                controller: drawer.controller
                compact: true
                showSections: false
                topMargin: 12
                contentInset: 16
                scrollbarHost: body
                uiScale: drawer.uiScale
                resourceActive: drawer.visible && drawer.resourceActive
            }
        }
    }
}
