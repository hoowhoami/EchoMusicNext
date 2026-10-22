pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import EchoMusic.Theme

// Shared wrapping context-menu row. EchoMenu uses it for Action entries;
// it can also be declared directly for custom visibility or danger state.
MenuItem {
    id: entry
    property bool danger: false
    property real uiScale: 1
    // Action-driven rows read the same flag from the Action's dynamic property.
    // Action carries `danger` as a dynamic property; subscript access avoids
    // declaring every Action subclass used by callers.
    readonly property bool actionDanger: action !== null && action !== undefined && action["danger"] === true
    readonly property color labelColor: entry.danger || entry.actionDanger ? Theme.floating.dangerText : Theme.floatingText
    font: Theme.font(13 * uiScale, Theme.semiboldWeight)
    leftPadding: 10 * uiScale
    rightPadding: (checkable ? 32 : 10) * uiScale
    topPadding: 8 * uiScale
    bottomPadding: 8 * uiScale
    implicitWidth: Math.max(168 * uiScale, measure.advanceWidth + entry.leftPadding + entry.rightPadding)
    // Hidden rows collapse inside the ListView.
    implicitHeight: entry.visible ? Math.max(36 * uiScale, body.implicitHeight + entry.topPadding + entry.bottomPadding) : 0
    TextMetrics {
        id: measure
        font: entry.font
        text: entry.text
    }
    indicator: EchoIcon {
        objectName: "menuEntryCheck"
        x: entry.width - 10 * entry.uiScale - width
        y: entry.topPadding + (entry.availableHeight - height) / 2
        width: 16 * entry.uiScale
        height: width
        visible: entry.checked
        name: "check"
        color: entry.labelColor
    }
    contentItem: EchoButtonContent {
        id: body
        text: entry.text
        font: entry.font
        color: entry.labelColor
        opacity: entry.enabled ? 1 : 0.5
        wrap: true
        lineHeight: 20 * entry.uiScale
        horizontalAlignment: Qt.AlignLeft
    }
    background: Rectangle {
        radius: Theme.controlRadius * entry.uiScale
        color: !entry.enabled ? "transparent" : entry.danger || entry.actionDanger ? (entry.down ? Theme.withAlpha(Theme.floating.danger, 0.16) : entry.highlighted ? Theme.withAlpha(Theme.floating.danger, 0.12) : "transparent") : entry.down ? Theme.floating.neutralPressed : entry.highlighted ? Theme.floating.hover : "transparent"
    }
}
