pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Theme

// Shared checkbox surface. Qt owns tristate, keyboard and accessibility semantics.
CheckBox {
    id: control
    EchoUiPart {
        id: formPart
        target: control
        role: "control.checkbox"
        key: control.objectName
        properties: ["foreground", "background", "accent", "borderColor", "borderWidth", "radius"]
    }
    property ThemeColors colors: Theme.colors
    // Indicator-only control: do not inherit CheckBox's expanding text policy.
    Layout.fillWidth: false
    Layout.fillHeight: false
    implicitWidth: 18
    implicitHeight: 18
    padding: 0
    hoverEnabled: true
    opacity: enabled ? 1 : 0.5
    contentItem: null
    indicator: Rectangle {
        anchors.centerIn: parent
        width: 18
        height: 18
        radius: formPart.value("radius", Theme.detailRadius)
        readonly property bool active: control.checkState !== Qt.Unchecked
        color: formPart.value("background", active ? Theme.withAlpha(formPart.value("accent", control.colors.accent), control.down ? 0.22 : control.hovered ? 0.18 : 0.12) : control.down ? control.colors.neutralPressed : control.hovered ? control.colors.hover : control.colors.mutedSurface)
        border.width: formPart.value("borderWidth", 1)
        border.color: formPart.value("borderColor", active ? Theme.mix(control.colors.strongBorder, formPart.value("accent", control.colors.accent), 0.35) : control.hovered ? Theme.withAlpha(control.colors.tone, 0.24) : control.colors.strongBorder)
        EchoIcon {
            anchors.centerIn: parent
            width: 14
            height: 14
            visible: control.checkState !== Qt.Unchecked
            name: control.checkState === Qt.PartiallyChecked ? "minus" : "check"
            color: formPart.value("foreground", control.colors.accentText)
        }
        Rectangle {
            anchors.fill: parent
            anchors.margins: -3
            visible: control.visualFocus
            color: "transparent"
            border.color: formPart.value("borderColor", formPart.value("accent", control.colors.accent))
            border.width: formPart.value("borderWidth", 1)
            radius: Theme.detailRadius + 3
        }
    }
    background: null
}
