pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import EchoMusic.Theme

Slider {
    id: control
    EchoUiPart {
        id: formPart
        target: control
        role: "control.slider"
        key: control.objectName
        properties: ["foreground", "background", "accent", "borderColor", "borderWidth", "radius"]
    }
    property ThemeColors colors: Theme.colors
    property bool showValue: false
    // Progress handles appear on hover, drag or keyboard focus.
    // Other sliders keep their handles visible.
    property bool progress: false
    property bool busy: false
    // Optional bounded model with normalized start/end roles; no business state.
    property var markers: null
    property real markerOpacity: Theme.dark ? 0.65 : 0.8
    readonly property bool invertedRange: from > to
    property string valueSuffix: ""
    readonly property string valueLabel: Number(value.toFixed(2)) + valueSuffix
    implicitWidth: horizontal ? 200 : 24
    implicitHeight: horizontal ? (showValue ? 44 : 24) : 120
    topPadding: showValue && horizontal ? 20 : 0
    // Basic.Slider defaults both vertical paddings to 6. Reset the matching
    // edge too, so the track/handle share the row's actual vertical center.
    bottomPadding: 0
    hoverEnabled: true
    Rectangle {
        visible: control.showValue && control.horizontal
        anchors.right: parent.right
        y: 0
        width: valueText.implicitWidth + 12
        height: valueText.implicitHeight + 4
        radius: Theme.detailRadius
        color: control.colors.mutedSurface
        Text {
            id: valueText
            anchors.centerIn: parent
            text: control.valueLabel
            color: control.colors.secondaryText
            font: Theme.font(11, Theme.semiboldWeight)
        }
    }
    background: Rectangle {
        id: track
        x: control.leftPadding + (control.horizontal ? 0 : control.availableWidth / 2 - 2)
        y: control.topPadding + (control.horizontal ? control.availableHeight / 2 - 2 : 0)
        width: control.horizontal ? control.availableWidth : 4
        height: control.horizontal ? 4 : control.availableHeight
        radius: formPart.value("radius", 2)
        color: formPart.value("background", Theme.withAlpha(control.colors.text, control.hovered ? 0.24 : 0.18))
        // Markers sit behind the played range and extend 1px above and below it.
        Repeater {
            model: control.progress && control.horizontal ? control.markers : null
            delegate: Item {
                id: mark
                required property int index
                required property real start
                required property real end
                width: track.width
                height: track.height
                Rectangle {
                    objectName: "progressClimaxStart-" + mark.index
                    x: mark.start * parent.width - 1
                    y: parent.height / 2 - 3
                    width: 2
                    height: 6
                    radius: 1
                    color: Theme.withAlpha(control.colors.accent, control.markerOpacity)
                }
                Rectangle {
                    objectName: "progressClimaxEnd-" + mark.index
                    visible: mark.end > mark.start
                    x: mark.end * parent.width - 1
                    y: parent.height / 2 - 3
                    width: 2
                    height: 6
                    radius: 1
                    color: Theme.withAlpha(control.colors.accent, control.markerOpacity)
                }
            }
        }
        Rectangle {
            width: control.horizontal ? (control.invertedRange ? 1 - control.visualPosition : control.visualPosition) * parent.width : parent.width
            height: control.horizontal ? parent.height : (control.invertedRange ? 1 - control.position : control.position) * parent.height
            x: control.horizontal && control.invertedRange ? parent.width - width : 0
            y: control.horizontal || control.invertedRange ? 0 : parent.height - height
            radius: 2
            color: formPart.value("accent", control.colors.accent)
            Loader {
                anchors.fill: parent
                active: control.progress && control.busy && control.horizontal
                sourceComponent: EchoProgressBusyOverlay {
                    colors: control.colors
                    busy: true
                }
            }
        }
    }
    handle: Rectangle {
        opacity: !control.progress || control.hovered || control.pressed || control.visualFocus ? 1 : 0
        Behavior on opacity {
            NumberAnimation {
                duration: Theme.fastDuration
            }
        }
        x: control.leftPadding + (control.horizontal ? control.visualPosition * (control.availableWidth - width) : control.availableWidth / 2 - width / 2)
        y: control.topPadding + (control.horizontal ? control.availableHeight / 2 - height / 2 : (1 - control.position) * (control.availableHeight - height))
        implicitWidth: 12
        implicitHeight: 12
        radius: 6
        color: formPart.value("foreground", "white")
        border.width: formPart.value("borderWidth", control.visualFocus ? 1 : 0.5)
        border.color: formPart.value("borderColor", Theme.withAlpha(control.colors.text, 0.24))
    }
}
