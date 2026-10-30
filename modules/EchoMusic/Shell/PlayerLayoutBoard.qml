pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Theme
import EchoMusic.Shell

Rectangle {
    id: board
    objectName: "playerLayoutBoard"
    required property ShellActionModel actions
    property bool lyricStyle: false
    property bool resourceActive: true
    property bool narrow: false
    PlayerLayoutReorder {
        id: reorderState
        actions: board.actions
        hostItem: board
        resourceActive: board.resourceActive && board.visible && board.Window.window !== null && board.Window.window.visible && board.Window.window.visibility !== Window.Minimized
    }
    readonly property var regions: [
        {
            zone: ShellActionModel.Left,
            label: qsTr("歌曲信息")
        },
        {
            zone: ShellActionModel.Center,
            label: qsTr("播放控制")
        },
        {
            zone: ShellActionModel.Right,
            label: qsTr("功能按钮")
        }
    ]
    implicitHeight: contents.implicitHeight + 20
    radius: Theme.cardRadius
    color: Theme.floating.mutedSurface
    ColumnLayout {
        id: contents
        x: 10
        y: 10
        width: board.width - 20
        spacing: 9
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: preview.implicitHeight + (board.narrow ? 16 : 18)
            radius: Theme.cardRadius
            color: Theme.withAlpha(Theme.elevated, 0.58)
            GridLayout {
                id: preview
                x: board.narrow ? 8 : 12
                y: board.narrow ? 8 : 9
                width: parent.width - x * 2
                columns: board.narrow ? 1 : 3
                columnSpacing: 12
                rowSpacing: board.narrow ? 8 : 7
                readonly property real sideWidth: Math.max(0, (width - 24) / 3.4)
                Repeater {
                    model: board.regions
                    delegate: Item {
                        id: illustration
                        required property int index
                        visible: !board.narrow
                        Layout.row: 0
                        Layout.column: index
                        Layout.preferredWidth: index === 1 ? preview.sideWidth * 1.4 : preview.sideWidth
                        Layout.preferredHeight: 42
                        opacity: 0.72
                        // This is the original editor's miniature layout diagram.
                        Row {
                            visible: illustration.index === 0
                            anchors.fill: parent
                            anchors.leftMargin: 8
                            anchors.rightMargin: 8
                            spacing: 9
                            Rectangle {
                                width: 34
                                height: 34
                                y: 4
                                radius: Theme.mediaRadius
                                color: Theme.withAlpha(Theme.floatingSecondaryText, 0.18)
                            }
                            Column {
                                width: parent.width - 43
                                y: 13
                                spacing: 6
                                Rectangle {
                                    width: parent.width * 0.76
                                    height: 5
                                    radius: 2.5
                                    color: Theme.withAlpha(Theme.floatingSecondaryText, 0.16)
                                }
                                Rectangle {
                                    width: parent.width * 0.48
                                    height: 5
                                    radius: 2.5
                                    color: Theme.withAlpha(Theme.floatingSecondaryText, 0.16)
                                }
                            }
                        }
                        Column {
                            visible: illustration.index === 1
                            x: 18
                            width: parent.width - 36
                            y: 2
                            spacing: 9
                            Row {
                                anchors.horizontalCenter: parent.horizontalCenter
                                spacing: 16
                                Rectangle {
                                    width: 16
                                    height: 16
                                    y: 5
                                    radius: 8
                                    color: Theme.withAlpha(Theme.floatingSecondaryText, 0.17)
                                }
                                Rectangle {
                                    width: 28
                                    height: 28
                                    radius: 14
                                    color: Theme.withAlpha(Theme.accent, 0.16)
                                }
                                Rectangle {
                                    width: 16
                                    height: 16
                                    y: 5
                                    radius: 8
                                    color: Theme.withAlpha(Theme.floatingSecondaryText, 0.17)
                                }
                            }
                            Rectangle {
                                width: parent.width
                                height: 4
                                radius: 2
                                color: Theme.withAlpha(Theme.floatingSecondaryText, 0.15)
                                Rectangle {
                                    width: parent.width * 0.38
                                    height: 4
                                    radius: 2
                                    color: Theme.withAlpha(Theme.accent, 0.28)
                                }
                            }
                        }
                        Row {
                            visible: illustration.index === 2
                            anchors.right: parent.right
                            anchors.rightMargin: 8
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 9
                            Repeater {
                                model: 3
                                delegate: Rectangle {
                                    width: 20
                                    height: 20
                                    radius: 10
                                    color: Theme.withAlpha(Theme.floatingSecondaryText, 0.15)
                                }
                            }
                        }
                    }
                }
                Repeater {
                    model: board.regions
                    delegate: PlayerLayoutZone {
                        lyricStyle: board.lyricStyle
                        required property int index
                        required property var modelData
                        Layout.row: board.narrow ? index : 1
                        Layout.column: board.narrow ? 0 : index
                        Layout.fillWidth: true
                        Layout.preferredWidth: board.narrow ? preview.width : index === 1 ? preview.sideWidth * 1.4 : preview.sideWidth
                        actions: board.actions
                        zone: modelData.zone
                        label: modelData.label
                        dragLayer: board
                        reorder: reorderState
                        resourceActive: board.resourceActive
                    }
                }
            }
        }
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: more.implicitHeight + 32
            color: Theme.withAlpha(Theme.store.floatingTone, 0.05 * 0.54)
            radius: Theme.cardRadius
            Label {
                x: 13
                y: 8
                text: qsTr("更多")
                color: Theme.floatingSecondaryText
                font: Theme.font(11, Theme.extraBoldWeight)
            }
            PlayerLayoutZone {
                id: more
                lyricStyle: board.lyricStyle
                x: 10
                y: 24
                width: parent.width - 20
                height: implicitHeight
                actions: board.actions
                zone: ShellActionModel.More
                label: qsTr("更多菜单按钮")
                showLabel: false
                dragLayer: board
                reorder: reorderState
                resourceActive: board.resourceActive
            }
        }
    }
}
