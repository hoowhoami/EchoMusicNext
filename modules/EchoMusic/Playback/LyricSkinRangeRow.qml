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
    property string valueLabel
    property real value
    property real from
    property real to
    property real stepSize
    signal moved(real value)
    Layout.fillWidth: true
    implicitHeight: contentItem.implicitHeight + 24
    padding: 12
    Rectangle {
        width: parent.width
        height: 1
        color: Theme.floating.strongBorder
        visible: row.y > 0
    }
    contentItem: ColumnLayout {
        spacing: 4
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: false
            spacing: 12
            EchoTextTagRow {
                Layout.fillWidth: true
                text: row.heading
                font: Theme.font(13, Theme.mediumWeight)
                color: Theme.floatingText
                lineHeight: 20
            }
            Control {
                leftPadding: 6
                rightPadding: 6
                topPadding: 2
                bottomPadding: 2
                implicitWidth: contentItem.implicitWidth + 12
                implicitHeight: 22
                background: Rectangle {
                    radius: Theme.detailRadius
                    color: Theme.floating.mutedSurface
                }
                contentItem: EchoTextTagRow {
                    text: row.valueLabel
                    font: Theme.font(11, Theme.semiboldWeight)
                    color: Theme.floatingSecondaryText
                    lineHeight: 18
                }
            }
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
        EchoSlider {
            Layout.fillWidth: true
            Layout.topMargin: 6
            colors: Theme.floating
            from: row.from
            to: row.to
            stepSize: row.stepSize
            value: row.value
            Accessible.name: row.heading
            onMoved: row.moved(value)
        }
    }
}
