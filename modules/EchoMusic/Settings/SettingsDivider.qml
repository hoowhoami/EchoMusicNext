pragma ComponentBehavior: Bound
import QtQuick.Layouts
import EchoMusic.Theme
import EchoMusic.Controls

// The settings-page divider: one device pixel on the window's grid, whatever
// the fractional DPI and the accumulated layout offsets are (EchoDivider).
EchoDivider {
    Layout.fillWidth: true
    lineColor: Theme.withAlpha(Theme.floatingText, 0.12)
}
