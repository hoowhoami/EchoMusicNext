pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import EchoMusic.Controls

// Native input boundary for a popup's painted rectangle. Child controls keep
// first refusal; unhandled presses, touch and wheel stay inside the surface.
// This does not change modality, outside dismissal or replay any input.
EchoFloatingSurface {
    Pane {
        anchors.fill: parent
        padding: 0
        wheelEnabled: true
        background: null
    }
}
