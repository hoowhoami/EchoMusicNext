pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import EchoMusic.Theme

// Both columns use the same native ink/line-box contract. In particular, a
// wrapped paragraph must not revert to Label's unrelated ascent alignment.
RowLayout {
    id: section
    objectName: "legalSection"
    required property string heading
    required property string text
    spacing: 8
    EchoTextLine {
        objectName: "legalSectionTitle"
        Layout.alignment: Qt.AlignTop
        Layout.fillHeight: false
        text: section.heading
        font: Theme.font(13, Theme.semiboldWeight)
        lineHeight: 24
        color: Theme.floatingText
    }
    EchoTextLine {
        objectName: "legalSectionContent"
        Layout.alignment: Qt.AlignTop
        Layout.fillWidth: true
        Layout.fillHeight: false
        Layout.minimumWidth: 0
        fillTextWidth: true
        text: section.text
        font: Theme.font(13, Theme.regularWeight)
        lineHeight: 24
        wrap: true
        color: Theme.floatingSecondaryText
    }
}
