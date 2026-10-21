pragma ComponentBehavior: Bound
import QtQuick
import EchoMusic.Theme

// Shared visual surface; public count and tag semantics remain on EchoBadge and EchoTag.
Item {
    id: surface
    property string partRole: "control.tag"
    EchoUiPart {
        id: labelPart
        target: surface
        role: surface.partRole
        key: surface.parent ? surface.parent.objectName : surface.objectName
        properties: ["foreground", "background", "borderColor", "borderWidth", "radius", "font", "fontSize", "fontWeight", "letterSpacing"]
        baseValues: ({
                foreground: surface.foreground,
                background: surface.fillColor,
                borderColor: surface.ringColor,
                radius: surface.radius,
                font: surface.labelFont
            })
    }
    readonly property color paintedForeground: labelPart.value("foreground", foreground)
    readonly property font paintedFont: labelPart.styledFont(labelFont)
    anchors.alignWhenCentered: false
    property string text: ""
    property Component contentComponent: null
    property string tone: "neutral"
    property string variant: "soft"
    property color customTone: Theme.accent
    property ThemeColors colors: Theme.colors
    property bool pill: false
    property real radius: Theme.detailRadius
    property bool borderVisible: true
    property font labelFont: Theme.font(small ? 11 : 10, Theme.semiboldWeight)
    property bool small: false
    property bool wrap: false
    property real horizontalPadding: small ? 7 : 5
    property color foreground: variant === "solid" ? "white" : tone === "muted" ? colors.secondaryText : tone === "accent" ? colors.accentText : tone === "custom" ? Qt.rgba(customTone.r * 0.68 + colors.text.r * 0.32, customTone.g * 0.68 + colors.text.g * 0.32, customTone.b * 0.68 + colors.text.b * 0.32, 1) : colors.text
    readonly property color fillColor: variant === "outline" ? "transparent" : variant === "solid" ? (tone === "custom" ? customTone : colors.accent) : tone === "accent" ? colors.selected : tone === "custom" ? Theme.withAlpha(customTone, 0.1) : colors.mutedSurface
    readonly property color ringColor: variant === "solid" ? "transparent" : tone === "accent" || tone === "custom" ? mixedRing(tone === "custom" ? customTone : colors.accent, colors.controlBorder) : colors.strongBorder
    function mixedRing(tint: color, border: color): color {
        const alpha = 0.24 + border.a * 0.76;
        return Qt.rgba((tint.r * 0.24 + border.r * border.a * 0.76) / alpha, (tint.g * 0.24 + border.g * border.a * 0.76) / alpha, (tint.b * 0.24 + border.b * border.a * 0.76) / alpha, alpha);
    }
    // Requested content changes before Loader commits its new item. Size from
    // that committed item, so replacement cannot re-enter this width binding.
    implicitWidth: {
        return Math.max(18, EchoControlContent.contentWidth(content.item as Item) + horizontalPadding * 2);
    }
    implicitHeight: {
        return EchoControlContent.contentHeight(content.item as Item, wrap, small);
    }
    Rectangle {
        anchors.fill: parent
        radius: labelPart.value("radius", surface.pill ? height / 2 : surface.radius)
        color: labelPart.value("background", surface.fillColor)
    }
    Rectangle {
        anchors.fill: parent
        radius: labelPart.value("radius", surface.pill ? height / 2 : surface.radius)
        color: "transparent"
        visible: surface.borderVisible
        border.width: labelPart.value("borderWidth", surface.pill ? 0 : 1)
        border.color: labelPart.value("borderColor", surface.ringColor)
    }
    // Exactly one content branch exists. A custom icon/count recipe does not
    // keep an invisible default Text and ink observer beside it.
    EchoContentLoader {
        id: content
        anchors.horizontalCenter: parent.horizontalCenter
        // The original Text used Qt's default center rounding. Custom content
        // used an unrounded Loader; retain both placement contracts.
        anchors.alignWhenCentered: surface.contentComponent === null
        width: surface.contentComponent !== null ? content.implicitWidth : Math.max(0, surface.width - surface.horizontalPadding * 2)
        y: surface.contentComponent !== null ? (surface.height - content.height) / 2 : 0
        sourceComponent: surface.contentComponent !== null ? surface.contentComponent : EchoControlContent.text
    }
}
