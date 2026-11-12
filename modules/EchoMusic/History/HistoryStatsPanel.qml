pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import EchoMusic.Controls
import EchoMusic.Theme

// A bounded statistics section; charts and rankings share the same typography.
Rectangle {
    id: panel
    default property alias content: body.data
    property string heading
    property string description
    property string detail
    property bool resourceActive: true
    property bool animate: true
    property int order: 0
    property bool flat: false
    readonly property real inset: flat ? 0 : 20
    implicitHeight: column.implicitHeight + inset * 2
    radius: Theme.dialogRadius
    color: flat ? "transparent" : Theme.mutedSurface
    border.width: flat ? 0 : 1
    border.color: Theme.subtleBorder
    data: [
        ColumnLayout {
            id: column
            x: panel.inset
            y: panel.inset
            width: Math.max(0, panel.width - panel.inset * 2)
            spacing: 18
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 5
                RowLayout {
                    Layout.fillWidth: true
                    EchoTextTagRow {
                        Layout.fillWidth: true
                        text: panel.heading
                        font: Theme.font(16, Theme.boldWeight)
                    }
                    EchoTextTagRow {
                        visible: panel.detail.length > 0
                        text: panel.detail
                        color: Theme.accentText
                        font: Theme.font(11, Theme.semiboldWeight)
                    }
                }
                EchoTextTagRow {
                    Layout.fillWidth: true
                    visible: panel.description.length > 0
                    text: panel.description
                    wrap: true
                    color: Theme.secondaryText
                    font: Theme.font(11, Theme.regularWeight)
                }
            }
            ColumnLayout {
                id: body
                Layout.fillWidth: true
                spacing: 12
            }
        },
        EchoEnter {
            target: panel
            active: panel.resourceActive
            duration: panel.animate ? Theme.normalDuration : 0
            order: panel.order
        }
    ]
}
