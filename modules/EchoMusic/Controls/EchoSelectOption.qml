pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import EchoMusic.Theme

ItemDelegate {
    id: option
    property bool selected: false
    property bool navigationFocus: false
    property bool wrap: false
    implicitHeight: wrap ? Math.max(36, label.implicitHeight + 16) : 36
    leftPadding: 10
    rightPadding: 32
    topPadding: wrap ? 8 : 0
    bottomPadding: wrap ? 8 : 0
    font: Theme.font(13, Theme.semiboldWeight)
    hoverEnabled: true
    opacity: enabled ? 1 : 0.5
    Accessible.role: Accessible.CheckBox
    Accessible.checked: selected
    contentItem: EchoTextLine {
        id: label
        text: option.text
        color: option.selected ? Theme.floating.accentText : Theme.floating.text
        font: option.font
        wrap: option.wrap
        fillTextWidth: true
        lineHeight: 20
    }
    EchoIcon {
        x: parent.width - 24
        y: (parent.height - height) / 2
        width: 14
        height: 14
        name: "check"
        color: Theme.floating.accentText
        visible: option.selected
    }
    background: Rectangle {
        radius: Theme.controlRadius
        color: option.selected ? Theme.withAlpha(Theme.floating.accent, option.down ? 0.22 : option.hovered ? 0.18 : 0.12) : option.down ? Theme.floating.neutralPressed : option.hovered ? Theme.floating.hover : "transparent"
        border.width: option.visualFocus || option.navigationFocus ? 1 : 0
        border.color: Theme.floating.accentText
        Behavior on color {
            ColorAnimation {
                duration: Theme.fastDuration
            }
        }
    }
    EchoToolTip {
        parent: option
        text: option.text
        requestedVisible: option.hovered && label.isTruncated() && !option.wrap
    }
}
