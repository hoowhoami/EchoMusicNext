pragma ComponentBehavior: Bound
pragma Singleton
import QtQuick

// Explicit skin colors win over explicit global colors. With neither set, the
// cover skin owns its resting white ink. Global previews resolve their own
// default independently. Reads stay in QML so every preference is observable.
QtObject {
    readonly property color defaultUnplayedColor: Qt.rgba(1, 1, 1, .2)
    readonly property color defaultCoverPlayedColor: "#ffffff"
    readonly property color defaultCoverUnplayedColor: Qt.rgba(1, 1, 1, .2)
    readonly property color defaultPortraitUnplayedColor: defaultCoverUnplayedColor
    function portraitPlayed(appearance: var, accent: color): color {
        const skin = appearance.portrait;
        if (skin.textColor.length > 0)
            return skin.textColor;
        return skin.useCoverColor ? accent : globalPlayed(appearance, accent);
    }
    function portraitUnplayed(appearance: var, accent: color): color {
        const skin = appearance.portrait;
        if (skin.unplayedTextColor.length > 0)
            return skin.unplayedTextColor;
        if (skin.unplayedUseCoverColor)
            return accent;
        if (appearance.unplayedFollowCoverColor || appearance.unplayedColor.length > 0)
            return globalUnplayed(appearance, accent);
        return defaultPortraitUnplayedColor;
    }
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
        if (appearance.unplayedFollowCoverColor || appearance.unplayedColor.length > 0)
            return globalUnplayed(appearance, accent);
        return defaultCoverUnplayedColor;
    }
    // The shared picker accepts opaque #rgb/#rrggbb only; preview alpha at the
    // swatch instead of letting the picker normalize the derived color away.
    function opaqueHex(value: color): string {
        return Qt.rgba(value.r, value.g, value.b, 1).toString();
    }
}
