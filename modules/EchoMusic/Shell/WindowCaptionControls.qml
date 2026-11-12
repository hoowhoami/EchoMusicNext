pragma ComponentBehavior: Bound
import QtQuick
import EchoMusic.Theme
import EchoMusic.Controls
import EchoMusic.Shell

WindowActionGroup {
    id: controls
    required property WindowController service
    property ThemeColors colors: Theme.colors
    visible: !service.fullscreen
    EchoIconButton {
        cursorShape: Qt.ArrowCursor
        hoverWhenDisabled: true
        objectName: "windowMinimizeAction"
        colors: controls.colors
        padding: 9.5
        glyph: "minus"
        text: qsTr("最小化")
        Accessible.name: qsTr("最小化窗口")
        onClicked: controls.service.minimize()
    }
    EchoIconButton {
        cursorShape: Qt.ArrowCursor
        hoverWhenDisabled: true
        objectName: "windowMaximizeAction"
        colors: controls.colors
        padding: 10
        glyph: controls.service.maximized ? "copy" : "square"
        text: controls.service.maximized ? qsTr("还原") : qsTr("最大化")
        Accessible.name: controls.service.maximized ? qsTr("还原窗口") : qsTr("最大化窗口")
        onClicked: controls.service.toggleMaximized()
    }
    EchoIconButton {
        id: closeAction
        cursorShape: Qt.ArrowCursor
        hoverWhenDisabled: true
        objectName: "windowCloseAction"
        colors: controls.colors
        padding: 9
        glyph: "x"
        text: qsTr("关闭")
        Accessible.name: qsTr("关闭窗口")
        onClicked: controls.service.close()
        foreground: hoverHighlighted ? "#ffffff" : visualFocus ? colors.text : colors.iconMain
        background: Rectangle {
            radius: Theme.controlRadius
            color: closeAction.hoverHighlighted ? "#c42b1c" : "transparent"
            border.width: closeAction.visualFocus ? 2 : 0
            border.color: Theme.accent
        }
    }
}
