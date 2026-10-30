pragma ComponentBehavior: Bound
pragma Singleton
import QtQuick

QtObject {
    // Preserve EchoMusic's numeric weights and the complete application font.
    readonly property int regularWeight: 400
    readonly property int mediumWeight: 500
    readonly property int semiboldWeight: 600
    readonly property int strongWeight: 650
    readonly property int boldWeight: 700
    readonly property int extraBoldWeight: 800
    readonly property int blackWeight: 900
    readonly property font bodyFont: store.applicationFont
    // 全局字号/字重：读取 store 属性使依赖注册进绑定，设置变更时全部
    // 字体绑定随 fontChanged 重新求值。
    function font(pixelSize: real, weight: int): font {
        return store.styledFont(bodyFont, pixelSize * store.globalFontSizeScale / 100,
                                Math.min(1000, Math.max(1, weight + store.globalFontWeightOffset)), 0, false,
                                false);
    }
    function spacedFont(pixelSize: real, weight: int, letterSpacing: real): font {
        return store.styledFont(bodyFont, pixelSize * store.globalFontSizeScale / 100,
                                Math.min(1000, Math.max(1, weight + store.globalFontWeightOffset)), letterSpacing,
                                false, false);
    }
    function decoratedFont(pixelSize: real, weight: int, letterSpacing: real, italic: bool, underline: bool): font {
        return store.styledFont(bodyFont, pixelSize * store.globalFontSizeScale / 100,
                                Math.min(1000, Math.max(1, weight + store.globalFontWeightOffset)), letterSpacing,
                                italic, underline);
    }
    function withAlpha(value: color, opacity: real): color {
        return Qt.rgba(value.r, value.g, value.b, opacity);
    }
    // CSS color-mix(in srgb): retain premultiplied alpha for transparent themes.
    function mix(base: color, tint: color, proportion: real): color {
        const t = Math.max(0, Math.min(1, proportion));
        const a = base.a * (1 - t);
        const b = tint.a * t;
        const alpha = a + b;
        return alpha > 0 ? Qt.rgba((base.r * a + tint.r * b) / alpha, (base.g * a + tint.g * b) / alpha, (base.b * a + tint.b * b) / alpha, alpha) : Qt.rgba(0, 0, 0, 0);
    }
    readonly property ThemeStore store: ThemeStore
    readonly property bool dark: store.dark
    readonly property color shell: store.shell
    readonly property color sidebar: store.sidebar
    readonly property color main: store.main
    readonly property color player: store.player
    readonly property color card: store.card
    readonly property color elevated: store.elevated
    readonly property int panelOpacity: store.panelOpacity
    readonly property color panelColor: store.panelColor
    readonly property color panelBackdrop: store.panelBackdrop
    readonly property color floatingText: store.floatingText
    readonly property color floatingSecondaryText: store.floatingSecondaryText
    readonly property color floatingCard: store.floatingCard
    readonly property color floatingBorder: store.floatingBorder
    readonly property color floatingAccentText: store.floatingAccentText
    readonly property ThemeColors colors: ThemeColors {
        tone: Theme.store.tone
        text: Theme.text
        secondaryText: Theme.secondaryText
        elevated: Theme.elevated
        card: Theme.card
        border: Theme.border
        accent: Theme.accent
        accentText: Theme.accentText
    }
    readonly property ThemeColors floating: ThemeColors {
        tone: Theme.store.floatingTone
        text: Theme.floatingText
        secondaryText: Theme.floatingSecondaryText
        elevated: Theme.store.floatingBackground
        card: Theme.floatingCard
        border: Theme.floatingBorder
        accent: Theme.store.floatingAccent
        accentText: Theme.floatingAccentText
    }
    readonly property color text: store.text
    readonly property color secondaryText: store.secondaryText
    readonly property color iconMain: Theme.withAlpha(text, 0.8)
    readonly property color iconSecondary: Theme.withAlpha(text, 0.6)
    readonly property color border: store.border
    readonly property color rowHover: Theme.withAlpha(store.tone, 0.05)
    readonly property color hotCard: Theme.withAlpha(text, 0.03)
    readonly property color fieldFocus: Theme.withAlpha(store.tone, 0.07)
    readonly property color subtleBorder: Theme.withAlpha(border, 0.72)
    readonly property color hover: store.controlHover
    readonly property color mutedSurface: store.mutedSurface
    readonly property color selected: store.selectedSurface
    readonly property color secondaryAccent: "#5ac8fa"
    readonly property color accent: store.accent
    // Qt 6.12 treats an on-prefixed binding name as a handler in this singleton.
    // Keep the readable token on a normal property; ThemeStore remains its owner.
    readonly property color accentForeground: store.onAccent
    readonly property color accentText: store.accentText
    // Use semantic radii for detail, control, card and dialog surfaces.
    // These values are independent of generic framework size names.
    readonly property int controlRadius: 6
    readonly property int itemRadius: 6
    readonly property int cardRadius: 10
    readonly property int mediaRadius: 6
    readonly property int popoverRadius: 10
    readonly property int dialogRadius: 12
    readonly property int shellRadius: 10
    readonly property int detailRadius: 4
    readonly property int microRadius: 2
    readonly property int fastDuration: store.motionEnabled ? 120 : 0
    readonly property int normalDuration: store.motionEnabled ? 180 : 0
    readonly property int pageDuration: store.motionEnabled ? 220 : 0
    readonly property int panelDuration: store.motionEnabled ? 240 : 0
    readonly property int exitDuration: store.motionEnabled ? 140 : 0
    readonly property int contentSwapDuration: store.motionEnabled ? 150 : 0
    readonly property int mediaFadeDuration: store.motionEnabled ? 400 : 0
    readonly property int staggerDelay: store.motionEnabled ? 32 : 0
    // BezierSpline accepts control points followed by the terminal point.
    readonly property var easeDefault: [0.25, 0.1, 0.25, 1, 1, 1]
    readonly property var easeEnter: [0.16, 1, 0.3, 1, 1, 1]
    readonly property var easeStandard: [0.2, 0, 0, 1, 1, 1]
    readonly property var easeExit: [0.4, 0, 1, 1, 1, 1]
}
