pragma ComponentBehavior: Bound
import QtQuick

EchoIconButton {
    id: control
    property bool loading: false
    property bool playing: false
    scaleFeedback: true
    glyph: playing ? "pause" : "play"
    text: loading ? qsTr("加载中…") : playing ? qsTr("暂停") : qsTr("播放")
    enabled: !loading
    iconCovered: loading
    Loader {
        objectName: "playbackButtonLoading"
        anchors.centerIn: parent
        width: Math.min(18, control.availableWidth, control.availableHeight)
        height: width
        active: control.loading && control.visualContentActive && !control.publicPart.replaced && control.visible && control.Window.window !== null && control.Window.window.visible && control.Window.window.visibility !== Window.Minimized
        sourceComponent: EchoSpinner {
            objectName: "playbackButtonSpinner"
            anchors.fill: parent
            name: "loader-2"
            rotationDuration: 900
            color: control.visualForeground
        }
    }
}
