pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import EchoMusic.Theme

ToolButton {
    id: control
    readonly property EchoUiPart publicPart: EchoUiPart {
        target: control
        role: "control.iconButton"
        key: control.objectName
        active: control.visualContentActive
        properties: ["foreground", "background", "radius", "borderColor", "borderWidth"]
        actions: ["activate"]
        replaceable: true
        baseValues: ({
                foreground: control.foreground
            })
        onActionRequested: (action, arguments) => {
            if (action === "activate")
                control.clicked();
        }
    }
    readonly property color visualForeground: publicPart.value("foreground", foreground)
    Connections {
        target: control
        function onClicked(): void {
            control.publicPart.publish("activated");
        }
    }
    required property string glyph
    // Pure icon actions expose their label on hover as well as to accessibility.
    property string tooltipText: text
    property bool animatePlayPause: false
    // Cover presentation without replacing the semantic glyph/state.
    property bool iconCovered: false
    // Semantic buttons keep their focus/actions while optional row visuals sleep.
    property bool visualContentActive: true
    property real playIconOffset: 0
    property ThemeColors colors: Theme.colors
    property bool hoverBackground: true
    property bool hoverWhenDisabled: false
    // Qt 6 delivers hover to disabled controls. A locally unavailable titlebar
    // action can highlight, while an entire disabled overlay subtree cannot.
    readonly property bool hoverHighlighted: hovered && (enabled || hoverWhenDisabled && parent !== null && parent.enabled)
    property color foreground: hoverHighlighted || visualFocus ? colors.text : colors.iconMain
    property real disabledOpacity: 0.6
    implicitWidth: 34
    implicitHeight: 34
    hoverEnabled: true
    Accessible.name: text
    Loader {
        // This is only a trigger recipe. Native hints are still created after
        // the hover delay and destroyed when the interaction ends.
        active: control.visualContentActive && control.tooltipText.length > 0
        sourceComponent: EchoToolTip {
            objectName: "iconButtonToolTip"
            parent: control
            requestedVisible: control.hovered && control.enabled && control.tooltipText.length > 0
            text: control.tooltipText
        }
    }
    contentItem: EchoContentLoader {
        active: control.visualContentActive
        visible: !control.iconCovered && !control.publicPart.replaced
        opacity: control.enabled ? 1 : control.disabledOpacity
        sourceComponent: control.animatePlayPause ? EchoControlContent.playPauseIcon : EchoControlContent.icon
    }
    padding: 7
    background: EchoContentLoader {
        active: control.visualContentActive && control.visible && control.Window.window !== null && control.Window.window.visible && control.Window.window.visibility !== Window.Minimized && !control.publicPart.replaced && (control.publicPart.value("background", Qt.rgba(0, 0, 0, 0)).a > 0 || control.publicPart.value("borderWidth", 0) > 0 || control.visualFocus || control.hoverBackground && control.hoverHighlighted)
        sourceComponent: EchoControlContent.iconButtonBackground
    }
}
