pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Controls
import EchoMusic.Theme

Item {
    id: heading
    required property string title
    property Component leadingControl: null
    property Component trailingControl: null
    // Header height includes a 28px action, 8px bottom padding and 1px border.
    implicitHeight: 37
    RowLayout {
        x: 8
        width: parent.width - 12
        height: 28
        spacing: 8
        Loader {
            visible: active
            active: heading.leadingControl !== null
            sourceComponent: heading.leadingControl
        }
        Label {
            text: heading.title
            color: Theme.floatingText
            font: Theme.font(13, Theme.semiboldWeight)
            Layout.fillWidth: true
        }
        Loader {
            visible: active
            active: heading.trailingControl !== null
            sourceComponent: heading.trailingControl
        }
    }
    // A bare 1-DIP Rectangle rasterizes by pixel-center sampling (no MSAA, no
    // AA material), so at 125% its 1.25 device pixels cover one or two whole
    // rows depending on the fractional position - the line visibly flips
    // between 1px and 2px as layout shifts. EchoDivider parks the line on a
    // whole device row and rounds its thickness, keeping it exactly 1px.
    EchoDivider {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        lineColor: Theme.floating.subtleBorder
    }
}
