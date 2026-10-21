pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import EchoMusic.Theme

Button {
    id: button
    readonly property EchoUiPart publicPart: EchoUiPart {
        target: button
        role: "control.button"
        key: button.objectName
        properties: ["foreground", "background", "borderColor", "borderWidth", "radius", "font", "fontSize", "fontWeight", "letterSpacing"]
        actions: ["activate"]
        replaceable: true
        baseValues: ({
                foreground: button.foreground,
                font: button.font
            })
        onActionRequested: (action, arguments) => {
            if (action === "activate")
                button.clicked();
        }
    }
    Connections {
        target: button
        function onClicked(): void {
            button.publicPart.publish("activated");
        }
    }
    property int size: EchoControlSize.Standard
    property string glyph: ""
    property real glyphSize: 16
    property bool accent: false
    property bool danger: false
    property bool plain: false
    /// Same contract as EchoButton: a busy action shows the shared spinner in
    /// place of its glyph and is not pressable.
    property bool loading: false
    property ThemeColors colors: Theme.colors
    property color foreground: plain && hovered && !danger ? colors.accentText : danger ? colors.dangerText : accent ? colors.accentText : colors.text
    implicitHeight: EchoControlSize.heightFor(size)
    implicitWidth: contentItem.implicitWidth + leftPadding + rightPadding
    leftPadding: EchoControlSize.buttonPaddingFor(size)
    rightPadding: leftPadding
    topPadding: 0
    bottomPadding: 0
    font: Theme.font(size === EchoControlSize.Large ? 15 : 12, size === EchoControlSize.Large ? Theme.blackWeight : Theme.semiboldWeight)
    opacity: enabled ? 1 : 0.6
    hoverEnabled: true
    enabled: !loading
    scale: enabled && down && Theme.store.motionEnabled ? 0.98 : 1
    Behavior on scale {
        NumberAnimation {
            duration: Theme.fastDuration
            easing.type: Easing.BezierSpline
            easing.bezierCurve: Theme.easeStandard
        }
    }
    Accessible.name: text
    contentItem: EchoButtonContent {
        visible: !button.publicPart.replaced
        spacing: button.size === EchoControlSize.Small ? 6 : 8
        glyph: button.glyph
        glyphSize: button.glyphSize
        loading: button.loading
        text: button.text
        color: button.publicPart.value("foreground", button.foreground)
        font: button.publicPart.styledFont(button.font)
    }
    background: EchoActionBackground {
        publicPart: button.publicPart
        colors: button.colors
        accent: button.accent
        danger: button.danger
        visible: !button.publicPart.replaced && (!button.plain || button.visualFocus)
        pressed: button.down
        highlighted: button.enabled && (button.hovered || button.visualFocus)
        border.width: button.publicPart.value("borderWidth", button.visualFocus ? 2 : 0)
        border.color: button.publicPart.value("borderColor", Theme.accent)
    }
}
