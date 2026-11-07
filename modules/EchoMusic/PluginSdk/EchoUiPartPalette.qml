pragma ComponentBehavior: Bound
import QtQuick
import EchoMusic.Theme
import EchoMusic.PluginSdk

// Use the target's presentation palette, including cover playback and Mini.
// Only typed color values cross the SDK boundary, never a host QObject.
ThemeColors {
    required property EchoUiPartRef part
    readonly property var values: part && part.valid ? part.palette : ({})
    text: values.text ?? Theme.colors.text
    tone: values.tone ?? Theme.colors.tone
    secondaryText: values.secondaryText ?? Theme.colors.secondaryText
    elevated: values.elevated ?? Theme.colors.elevated
    card: values.card ?? Theme.colors.card
    border: values.border ?? Theme.colors.border
    accent: values.accent ?? Theme.colors.accent
    accentText: values.accentText ?? Theme.colors.accentText
}
