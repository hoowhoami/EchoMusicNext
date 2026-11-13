pragma ComponentBehavior: Bound
import QtQuick

// Qt chooses rounded-rectangle segments from the logical radius, not the final
// pixel density. Use larger drawing units internally to reach its bounded
// high-resolution geometry, then map back to the owner's unchanged rectangle.
// This remains native Rectangle AA, with no image, layer or extra framebuffer.
Item {
    id: rounded
    property color color: "white"
    property real radius: 0
    antialiasing: true
    Rectangle {
        width: rounded.width * 4
        height: rounded.height * 4
        radius: rounded.radius * 4
        scale: 0.25
        transformOrigin: Item.TopLeft
        color: rounded.color
        antialiasing: rounded.antialiasing
    }
}
