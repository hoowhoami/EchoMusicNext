pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Window
import EchoMusic.Shell

Window {
    id: window
    objectName: "desktopLyricUnlockWindow"
    required property FloatingPlayerController service
    width: 32
    height: 32
    color: "transparent"
    title: qsTr("解锁桌面歌词")
    flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.WindowDoesNotAcceptFocus
    // See DesktopLyricWindow: the automatic transient parent would tie this
    // handle to the main window instead of the desktop lyrics window it
    // serves.
    transientParent: null
    visible: service.unlockVisible
    Component.onCompleted: service.attachUnlock(window)
    Component.onDestruction: service.attachUnlock(null)
    DesktopLyricButton {
        objectName: "desktopLyricUnlock"
        anchors.fill: parent
        unlock: true
        compact: window.width === 30
        glyph: "lock-open"
        text: qsTr("解锁桌面歌词")
        onClicked: window.service.locked = false
    }
}
