pragma ComponentBehavior: Bound
import QtQuick
import EchoMusic.Theme
import EchoMusic.Controls

// Themed artwork with an accent gradient and two decorative circles.
// Inline the icon color because Qt SVG does not resolve the iconify body's currentColor.
EchoCover {
    id: cover
    readonly property color base: Theme.accent
    readonly property real hue: Math.max(0, base.hslHue)
    readonly property real saturation: base.hslSaturation
    readonly property real lightness: Math.min(.5, Math.max(.35, base.hslLightness))
    readonly property color gradientFrom: Qt.hsla(hue, Math.min(.9, saturation), lightness, 1)
    readonly property color gradientTo: Qt.hsla((hue + 20 / 360) % 1, Math.min(.7, saturation * .75), Math.min(.7, lightness + .2), 1)
    // Tabler gift path (MIT), drawn at 7x scale.
    readonly property string artwork: '<svg xmlns="http://www.w3.org/2000/svg" width="400" height="400">' + '<defs><linearGradient id="g" x1="0" y1="0" x2="1" y2="1">' + '<stop offset="0%" stop-color="' + gradientFrom + '"/>' + '<stop offset="100%" stop-color="' + gradientTo + '"/></linearGradient></defs>' + '<rect width="400" height="400" fill="url(#g)"/>' + '<circle cx="104" cy="96" r="52" fill="#fff" opacity=".14"/>' + '<circle cx="308" cy="304" r="72" fill="#fff" opacity=".10"/>' + '<g transform="translate(200 200)">' + '<rect x="-92" y="-92" width="184" height="184" rx="46" fill="#fff" opacity=".18"/>' + '<path transform="translate(-84 -84) scale(7)" fill="none" stroke="#FFFFFF" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" d="M3 8m0 1a1 1 0 0 1 1 -1h16a1 1 0 0 1 1 1v2a1 1 0 0 1 -1 1h-16a1 1 0 0 1 -1 -1zM12 8l0 13M19 12v7a2 2 0 0 1 -2 2h-10a2 2 0 0 1 -2 -2v-7M7.5 8a2.5 2.5 0 0 1 0 -5a4.8 8 0 0 1 4.5 5a4.8 8 0 0 1 4.5 -5a2.5 2.5 0 0 1 0 5"/>' + '</g></svg>'
    source: "data:image/svg+xml;utf8," + encodeURIComponent(artwork)
}
