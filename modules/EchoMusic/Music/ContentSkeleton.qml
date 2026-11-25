pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import EchoMusic.Controls
import EchoMusic.Theme

// Bounded cover and text placeholders; loading never duplicates the catalog model.
Rectangle {
    id: skeleton
    property bool compact: false
    property bool video: false
    property bool artist: false
    property bool resourceActive: true
    readonly property real coverSize: Math.max(0, width)
    implicitHeight: compact ? 62 : coverSize / (video ? 1.78 : 1) + (artist ? 52 : 70)
    color: compact ? Theme.withAlpha(Theme.text, 0.03) : "transparent"
    radius: Theme.cardRadius
    RowLayout {
        anchors.fill: parent
        anchors.margins: 8
        visible: skeleton.compact
        spacing: 12
        EchoSkeleton {
            Layout.preferredWidth: 40
            Layout.preferredHeight: 40
            radius: 8
            animate: skeleton.resourceActive
        }
        Item {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            Layout.preferredHeight: 32
            Column {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                spacing: 8
                EchoSkeleton {
                    variant: EchoSkeleton.Text
                    width: parent.width * .68
                    height: 13
                    animate: skeleton.resourceActive
                }
                EchoSkeleton {
                    variant: EchoSkeleton.Text
                    width: parent.width * .42
                    height: 11
                    animate: skeleton.resourceActive
                }
            }
        }
    }
    ColumnLayout {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.topMargin: 4
        visible: !skeleton.compact
        spacing: 8
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: skeleton.coverSize / (skeleton.video ? 1.78 : 1)
            EchoSkeleton {
                anchors.centerIn: parent
                width: skeleton.artist ? Math.min(parent.width * .82, 180) : parent.width
                height: skeleton.artist ? width : parent.height
                radius: skeleton.artist ? width / 2 : skeleton.video ? 8 : 14
                animate: skeleton.resourceActive
            }
        }
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: skeleton.artist ? 40 : 58
            Column {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.leftMargin: 2
                anchors.rightMargin: 2
                anchors.verticalCenter: parent.verticalCenter
                spacing: 7
                EchoSkeleton {
                    variant: EchoSkeleton.Text
                    width: parent.width * .82
                    height: 13
                    animate: skeleton.resourceActive
                }
                EchoSkeleton {
                    variant: EchoSkeleton.Text
                    width: parent.width * .54
                    height: 11
                    animate: skeleton.resourceActive
                }
            }
        }
    }
}
