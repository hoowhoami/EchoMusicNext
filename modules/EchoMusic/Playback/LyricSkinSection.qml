pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Theme
import EchoMusic.Controls

ColumnLayout {
    id: section
    default property alias rows: sectionBody.data
    property string heading
    Layout.fillWidth: true
    spacing: 8
    data: [
        EchoTextTagRow {
            Layout.fillWidth: true
            Layout.leftMargin: 2
            text: section.heading
            font: Theme.font(12, Theme.semiboldWeight)
            color: Theme.floatingSecondaryText
            lineHeight: 18
        },
        Control {
            Layout.fillWidth: true
            implicitHeight: sectionBody.implicitHeight
            background: Rectangle {
                radius: Theme.cardRadius
                color: Theme.floatingCard
            }
            contentItem: ColumnLayout {
                id: sectionBody
                spacing: 0
            }
        }
    ]
}
