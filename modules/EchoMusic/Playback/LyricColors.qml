pragma ComponentBehavior: Bound
pragma Singleton
import QtQuick

// Explicit skin colors win over explicit global colors. With neither set, the
// cover skin owns its resting white ink. Global previews resolve their own
// default independently. Reads stay in QML so every preference is observable.
QtObject {
    readonly property color defaultPlayedColor: "#ffffff"
    readonly property color defaultUnplayedColor: Qt.rgba(1, 1, 1, .2)
    readonly property color defaultCoverPlayedColor: defaultPlayedColor
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
        return appearance.playedColor.length > 0 ? appearance.playedColor : defaultPlayedColor;
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
    // Mini keeps the playback page's default alpha hierarchy. The floating
    // palette already owns a readable foreground for its actual surface;
    // mirror the white defaults to black when that foreground is dark.
    function miniPlayed(preferences: var, accent: color, foreground: color): color {
        if (preferences.followCoverColor)
            return accent;
        if (preferences.playedColor.length > 0)
            return preferences.playedColor;
        return foreground.hslLightness > .5 ? defaultCoverPlayedColor : Qt.rgba(0, 0, 0, defaultCoverPlayedColor.a);
    }
    function miniUnplayed(preferences: var, accent: color, foreground: color): color {
        if (preferences.unplayedFollowCoverColor)
            return accent;
        if (preferences.unplayedColor.length > 0)
            return preferences.unplayedColor;
        return foreground.hslLightness > .5 ? defaultCoverUnplayedColor : Qt.rgba(0, 0, 0, defaultCoverUnplayedColor.a);
    }
    // The shared picker accepts opaque #rgb/#rrggbb only; preview alpha at the
    // swatch instead of letting the picker normalize the derived color away.
    function opaqueHex(value: color): string {
        return Qt.rgba(value.r, value.g, value.b, 1).toString();
    }
}
