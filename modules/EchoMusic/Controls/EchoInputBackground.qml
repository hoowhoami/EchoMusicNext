pragma ComponentBehavior: Bound
import QtQuick
import EchoMusic.Theme

Rectangle {
    property ThemeColors colors: Theme.colors
    property bool focused: false
    // Floating composers need an opaque base to occlude scrolled content.
    property color baseColor: "transparent"
    radius: Theme.controlRadius
    color: Theme.mix(baseColor, colors.tone, focused ? 0.07 : 0.05)
}
