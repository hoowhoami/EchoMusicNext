pragma ComponentBehavior: Bound
import QtQuick
import EchoMusic.Theme
import EchoMusic.Controls

// Themed artwork derived from EchoMusic utils/themedCover.ts, GPL-3.0-only.
// Uses an accent gradient, decorative circles and an inline Tabler icon.
// Inline color avoids currentColor resolution differences in Qt SVG.
EchoCover {
    id: cover
    retainWhileLoading: true
    objectName: "themedDetailCover"
    /// Tabler 24x24 stroke path, exactly as bundled under Controls/icons.
    property string iconPath: ""
    readonly property color base: Theme.accent
    readonly property real hue: Math.max(0, base.hslHue)
    readonly property real saturation: base.hslSaturation
    readonly property real lightness: Math.min(.5, Math.max(.35, base.hslLightness))
    readonly property color gradientFrom: Qt.hsla(hue, Math.min(.9, saturation), lightness, 1)
    readonly property color gradientTo: Qt.hsla((hue + 20 / 360) % 1, Math.min(.7, saturation * .75), Math.min(.7, lightness + .2), 1)
    readonly property string artwork: '<svg xmlns="http://www.w3.org/2000/svg" width="400" height="400">' + '<defs><linearGradient id="g" x1="0" y1="0" x2="1" y2="1">' + '<stop offset="0%" stop-color="' + gradientFrom + '"/>' + '<stop offset="100%" stop-color="' + gradientTo + '"/></linearGradient></defs>' + '<rect width="400" height="400" fill="url(#g)"/>' + '<circle cx="104" cy="96" r="52" fill="#fff" opacity=".14"/>' + '<circle cx="308" cy="304" r="72" fill="#fff" opacity=".10"/>' + '<g transform="translate(200 200)">' + '<rect x="-92" y="-92" width="184" height="184" rx="46" fill="#fff" opacity=".18"/>' + '<path transform="translate(-84 -84) scale(7)" fill="none" stroke="#FFFFFF" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" d="' + iconPath + '"/></g></svg>'
    source: iconPath.length > 0 ? "data:image/svg+xml;utf8," + encodeURIComponent(artwork) : ""
}
