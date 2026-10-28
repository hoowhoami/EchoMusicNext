pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import EchoMusic.Theme
import EchoMusic.Controls

LyricSkinSettingRow {
    id: colorRow
    property color swatchColor
    property string actionObjectName
    property string resetObjectName
    signal openPicker
    signal resetColor
    action: Component {
        RowLayout {
            spacing: 8
            EchoSoftButton {
                objectName: colorRow.resetObjectName
                text: qsTr("恢复默认")
                colors: Theme.floating
                onClicked: colorRow.resetColor()
            }
            EchoSoftButton {
                objectName: colorRow.actionObjectName
                text: qsTr("更改")
                colors: Theme.floating
                rightPadding: 36
                onClicked: colorRow.openPicker()
                Rectangle {
                    anchors.right: parent.right
                    anchors.rightMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    width: 24
                    height: 24
                    radius: Theme.controlRadius
                    color: colorRow.swatchColor
                    border.width: 1
                    border.color: Theme.floating.strongBorder
                }
            }
        }
    }
}
