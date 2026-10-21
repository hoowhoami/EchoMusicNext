pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import EchoMusic.Theme

Button {
    id: control
    readonly property EchoUiPart publicPart: EchoUiPart {
        target: control
        role: "control.button"
        key: control.objectName
        properties: ["foreground", "background", "borderColor", "borderWidth", "radius", "font", "fontSize", "fontWeight", "letterSpacing"]
        actions: ["activate"]
        replaceable: true
        baseValues: ({
                foreground: control.foreground,
                font: control.font
            })
        onActionRequested: (action, arguments) => {
            if (action === "activate")
                control.clicked();
        }
    }
    Connections {
        target: control
        function onClicked(): void {
            control.publicPart.publish("activated");
        }
    }

    // Public Qt component names used by shipped author QML; geometry has one
    // owner in EchoControlSize (xs is compact, ordinary sm/md are standard).
    enum Size {
        ExtraSmall = 1,
        Small = 2,
        Medium = 2,
        Large = 3
    }

    property bool accent: true
    property ThemeColors colors: Theme.colors
    property color foreground: accent ? colors.accentText : colors.text
    property string glyph: ""
    property real glyphSpacing: 8
    property bool loading: false
    property int size: EchoControlSize.Standard

    bottomPadding: 0
    enabled: !loading
    // Standard actions use 36px/12px/600; login selects the Large role explicitly.
    font: Theme.font(size === EchoControlSize.Large ? 15 : 12, size === EchoControlSize.Large ? Theme.blackWeight : Theme.semiboldWeight)
    hoverEnabled: true
    implicitHeight: EchoControlSize.heightFor(size)
    leftPadding: EchoControlSize.buttonPaddingFor(size)
    opacity: enabled ? 1 : 0.6
    rightPadding: leftPadding
    scale: enabled && down && Theme.store.motionEnabled ? 0.98 : 1
    topPadding: 0

    background: EchoActionBackground {
        publicPart: control.publicPart
        visible: !control.publicPart.replaced
        accent: control.accent
        border.color: control.publicPart.value("borderColor", Theme.accent)
        border.width: control.publicPart.value("borderWidth", control.visualFocus ? 2 : 0)
        colors: control.colors
        highlighted: control.enabled && (control.hovered || control.visualFocus)
        pressed: control.down
    }
    contentItem: EchoButtonContent {
        visible: !control.publicPart.replaced
        color: control.publicPart.value("foreground", control.foreground)
        font: control.publicPart.styledFont(control.font)
        glyph: control.glyph
        loading: control.loading
        spacing: control.size === EchoControlSize.Small ? 6 : control.glyphSpacing
        text: control.text
    }
    Behavior on scale {
        NumberAnimation {
            duration: Theme.fastDuration
            easing.bezierCurve: Theme.easeStandard
            easing.type: Easing.BezierSpline
        }
    }
}
