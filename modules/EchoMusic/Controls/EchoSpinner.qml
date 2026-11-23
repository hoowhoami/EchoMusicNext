pragma ComponentBehavior: Bound
import QtQuick
import EchoMusic.Theme

EchoIcon {
    id: spinner
    implicitWidth: 28
    implicitHeight: 28
    name: "verification-spinner"
    color: Theme.accent
    property bool spinning: true
    property int rotationDuration: 800
    RotationAnimator on rotation {
        objectName: "spinnerRotation"
        from: 0
        to: 360
        duration: spinner.rotationDuration
        loops: Animation.Infinite
        running: spinner.spinning && spinner.visible && spinner.resourceActive && spinner.Window.window !== null && spinner.Window.window.visible && spinner.Window.window.visibility !== Window.Minimized && Theme.store.motionEnabled
    }
}
