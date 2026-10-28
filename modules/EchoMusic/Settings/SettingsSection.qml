pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Controls
import EchoMusic.Theme

ColumnLayout {
    id: section
    property string heading: ""
    property string glyph: ""
    default property alias rows: content.data
    spacing: 16
    RowLayout {
        Layout.fillHeight: false
        Layout.fillWidth: true
        spacing: 12
        EchoIcon {
            objectName: "settingsSectionIcon"
            name: section.glyph
            Layout.preferredWidth: 20
            Layout.preferredHeight: 20
            color: Theme.floatingAccentText
        }
        EchoTextTagRow {
            text: section.heading
            font: Theme.font(15, Theme.semiboldWeight)
            color: Theme.floatingText
        }
    }
    Rectangle {
        Layout.fillWidth: true
        implicitHeight: content.implicitHeight + 40
        radius: Theme.cardRadius
        color: Theme.floating.mutedSurface
        border.color: "transparent"
        ColumnLayout {
            id: content
            x: 20
            y: 20
            width: parent.width - 40
            spacing: 20
        }
    }
}
