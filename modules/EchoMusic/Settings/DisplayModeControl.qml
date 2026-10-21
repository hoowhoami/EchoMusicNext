pragma ComponentBehavior: Bound
import QtQuick
import EchoMusic.Theme
import EchoMusic.Controls

EchoSelect {
    colors: Theme.floating
    // Explicit user modes override the selected theme default.
    readonly property list<int> modes: [ThemeStore.ThemeDefault, ThemeStore.System, ThemeStore.Light, ThemeStore.Dark]
    model: [qsTr("跟随主题"), qsTr("跟随系统"), qsTr("浅色"), qsTr("深色")]
    currentIndex: modes.indexOf(Theme.store.mode)
    Accessible.name: qsTr("深浅色模式")
    onActivated: Theme.store.setMode(modes[currentIndex])
}
