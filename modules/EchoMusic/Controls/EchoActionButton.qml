pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic

// Interaction foundation for custom cards, tabs and other button surfaces.
// Keep Qt's geometry/content defaults; styled actions use EchoButton/SoftButton.
Button {
    id: control
    property int cursorShape: Qt.PointingHandCursor
    hoverEnabled: true
    EchoActionCursor {
        cursorShape: control.cursorShape
    }
}
