pragma ComponentBehavior: Bound
import QtQuick
import EchoMusic.Controls
import EchoMusic.Playback
import EchoMusic.Theme

Window {
    id: host
    required property PlaybackService playback
    property real zoom: 1
    width: 1000
    height: 800
    visible: true
    color: Theme.main
    EchoButton {
        id: anchor
        x: 600
        y: 740
        text: "Effects"
        onClicked: effect.open()
    }
    PlaybackEffectPopup {
        id: effect
        objectName: "effectSearchPopup"
        playback: host.playback
        anchorItem: anchor
        uiScale: host.zoom
        tab: 3
    }
    Component.onCompleted: Qt.callLater(function () {
        effect.open();
    })
}
