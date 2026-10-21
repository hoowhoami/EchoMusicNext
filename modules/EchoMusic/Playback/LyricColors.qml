pragma ComponentBehavior: Bound
pragma Singleton
import QtQuick

// Page colors are the global baseline; the active skin overrides individual
// slots and keeps inheriting every slot it leaves unset. Reads stay in QML so
// bindings track each layer. Global previews never read skin overrides. The
// neutral unplayed default stays independent of either layer's played ink.
QtObject {
    readonly property color defaultUnplayedColor: "#b9b9b9"
    function globalPlayed(appearance: var, accent: color): color {
        if (appearance.followCoverColor)
            return accent;
        return appearance.playedColor.length > 0 ? appearance.playedColor : "#ffffff";
    }
    function globalUnplayed(appearance: var, accent: color): color {
        if (appearance.unplayedFollowCoverColor)
            return accent;
        if (appearance.unplayedColor.length > 0)
            return appearance.unplayedColor;
        return defaultUnplayedColor;
    }
    function played(appearance: var, accent: color): color {
        if (appearance.textColor.length > 0)
            return appearance.textColor;
        if (appearance.useCoverColor)
            return accent;
        return globalPlayed(appearance, accent);
    }
    function unplayed(appearance: var, accent: color): color {
        if (appearance.unplayedTextColor.length > 0)
            return appearance.unplayedTextColor;
        if (appearance.unplayedUseCoverColor)
            return accent;
        return globalUnplayed(appearance, accent);
    }
    // The shared picker accepts opaque #rgb/#rrggbb only; preview alpha at the
    // swatch instead of letting the picker normalize the derived color away.
    function opaqueHex(value: color): string {
        return Qt.rgba(value.r, value.g, value.b, 1).toString();
    }
}
