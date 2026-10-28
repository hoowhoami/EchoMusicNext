pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Theme
import EchoMusic.Controls

Control {
    id: row
    property string heading
    property string hint
    property Component action
    Layout.fillWidth: true
    implicitHeight: Math.max(52, contentItem.implicitHeight + 24)
    padding: 12
    Rectangle {
        width: parent.width
        height: 1
        color: Theme.floating.strongBorder
        visible: row.y > 0
    }
    contentItem: RowLayout {
        spacing: 16
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: false
            Layout.minimumWidth: 0
            spacing: 4
            EchoTextTagRow {
                Layout.fillWidth: true
                text: row.heading
                font: Theme.font(13, Theme.mediumWeight)
                color: Theme.floatingText
                lineHeight: 20
                wrap: true
            }
            EchoTextTagRow {
                visible: row.hint.length > 0
                Layout.fillWidth: true
                text: row.hint
                font: Theme.font(11, Theme.regularWeight)
                color: Theme.floatingSecondaryText
                lineHeight: 16.5
                wrap: true
            }
        }
        Loader {
            Layout.fillHeight: false
            sourceComponent: row.action
        }
    }
}
