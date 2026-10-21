pragma ComponentBehavior: Bound
import QtQuick
import EchoMusic.Theme

// Count badge. Inline spacing belongs to the surrounding layout;
// floating badges require an explicit visual parent outside the layout.
Item {
    id: badge
    enum Tone {
        Neutral,
        Muted,
        Accent
    }
    enum Placement {
        Inline,
        Floating
    }
    // EchoMusic accepts number|string and hides only an undefined count.
    property bool pill: false
    property string glyph: ""
    property font font: Theme.font(10, Theme.semiboldWeight)
    property alias foreground: surface.foreground
    property alias horizontalPadding: surface.horizontalPadding
    property var count: undefined
    property string displayText: count === undefined || count === null ? "" : String(count)
    property int tone: EchoBadge.Neutral
    property int placement: EchoBadge.Inline
    property ThemeColors colors: Theme.colors
    visible: count !== undefined
    implicitWidth: surface.implicitWidth
    implicitHeight: surface.implicitHeight
    x: placement === EchoBadge.Floating && parent ? parent.width - width * 0.65 : 0
    y: placement === EchoBadge.Floating ? -height * 0.28 : 0
    Accessible.role: Accessible.StaticText
    Accessible.name: count === undefined || count === null ? "" : String(count)
    EchoLabelSurface {
        id: surface
        partRole: "control.badge"
        anchors.fill: parent
        pill: badge.pill
        labelFont: badge.font
        text: badge.displayText
        tone: badge.tone === EchoBadge.Accent ? "accent" : badge.tone === EchoBadge.Muted ? "muted" : "neutral"
        colors: badge.colors
        contentComponent: badge.glyph.length > 0 ? countContent : null
    }
    Component {
        id: countContent
        EchoButtonContent {
            glyph: badge.glyph
            glyphSize: 12
            spacing: 6
            text: badge.displayText
            font: surface.paintedFont
            color: surface.paintedForeground
        }
    }
}
