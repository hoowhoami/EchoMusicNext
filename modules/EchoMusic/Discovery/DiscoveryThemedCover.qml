pragma ComponentBehavior: Bound
import QtQuick
import EchoMusic.Theme
import EchoMusic.Controls

// Themed artwork derived from EchoMusic utils/themedCover.ts, GPL-3.0-only.
// Decode once at the requested size rather than on every scroll step.
EchoCover {
    id: cover
    retainWhileLoading: true
    property string day: ""
    property bool ranking: false
    property string glyph: ranking ? "trophy" : ""
    readonly property color base: Theme.accent
    readonly property real hue: Math.max(0, base.hslHue)
    readonly property real saturation: base.hslSaturation
    readonly property real lightness: Math.min(.5, Math.max(.35, base.hslLightness))
    readonly property color gradientFrom: Qt.hsla(hue, Math.min(.9, saturation), lightness, 1)
    readonly property color gradientTo: Qt.hsla((hue + 20 / 360) % 1, Math.min(.7, saturation * .75), Math.min(.7, lightness + .2), 1)
    readonly property string artwork: '<svg xmlns="http://www.w3.org/2000/svg" width="400" height="400"><defs><linearGradient id="g" x1="0" y1="0" x2="1" y2="1"><stop offset="0%" stop-color="' + gradientFrom + '"/><stop offset="100%" stop-color="' + gradientTo + '"/></linearGradient></defs><rect width="400" height="400" fill="url(#g)"/><circle cx="104" cy="96" r="52" fill="#fff" opacity=".14"/><circle cx="308" cy="304" r="72" fill="#fff" opacity=".10"/><g transform="translate(200 200)"><rect x="-92" y="-92" width="184" height="184" rx="46" fill="#fff" opacity=".18"/></g></svg>'
    source: "data:image/svg+xml;utf8," + encodeURIComponent(artwork)
    TextMetrics {
        id: dateBounds
        font: dateText.font
        text: dateText.text
    }
    EchoTextInkMetrics {
        id: dateInk
        font: dateText.font
        text: dateText.text
    }
    Text {
        id: dateText
        objectName: "discoveryCoverDate"
        // Center native digit ink independently of SVG baseline rules,
        // retaining the source artwork's size and weight ratios.
        x: cover.width / 2 - dateBounds.tightBoundingRect.x - dateBounds.tightBoundingRect.width / 2
        y: cover.height / 2 - baselineOffset - dateInk.inkCenterFromBaseline
        text: cover.glyph.length ? "" : cover.day
        textFormat: Text.PlainText
        font: Theme.font(cover.width * (cover.day.length > 1 ? 132 : 150) / 400, Theme.extraBoldWeight)
        color: "#f0ffffff"
    }
    EchoIcon {
        anchors.centerIn: parent
        visible: cover.glyph.length > 0
        width: cover.width * 168 / 400
        height: width
        name: cover.glyph
        color: "white"
        resourceActive: cover.resourceActive
    }
}
