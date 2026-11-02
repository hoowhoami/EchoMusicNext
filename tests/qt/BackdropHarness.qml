pragma ComponentBehavior: Bound
import QtQuick
import EchoMusic.Controls

Window {
    id: window
    width: 440
    height: 320
    visible: true
    color: "black"
    property real sourceOffset: 0
    EchoBackdropRoot {
        x: window.sourceOffset
        width: window.width
        height: window.height
        Rectangle {
            width: parent.width / 2
            height: parent.height
            color: "#ff0000"
        }
        Rectangle {
            x: parent.width / 2
            width: parent.width / 2
            height: parent.height
            color: "#0000ff"
        }
    }
    EchoFloatingSurface {
        objectName: "frostProbe"
        x: 120
        y: 70
        width: 200
        height: 180
        radius: 10
        border.width: 0
    }
}
