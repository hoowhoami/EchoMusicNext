pragma ComponentBehavior: Bound
import QtQuick
import EchoMusic.Theme
import EchoMusic.Playback

Item {
    id: body
    objectName: "simplePlayback"
    required property PlaybackService playback
    required property ThemeColors colors
    required property real renderTime
    required property bool resourceActive
    required property real pageWidth
    required property real pageHeight
    property real controlsTopReserve: 0
    property real overlayRightReserve: 0
    readonly property LyricSimpleAppearance settings: playback.lyricAppearance.simple
    PlaybackLyricViewport {
        objectName: "simpleLyricViewport"
        anchors.fill: parent
        anchors.leftMargin: 32
        anchors.rightMargin: 32
        anchors.bottomMargin: Math.max(96, body.controlsTopReserve + 16)
        playback: body.playback
        colors: body.colors
        renderTime: body.renderTime
        resourceActive: body.resourceActive
        pageWidth: body.pageWidth
        pageHeight: body.pageHeight
        fontSize: (pageWidth <= 768 ? Math.max(12, pageWidth * .08) : Math.max(12, pageHeight * .05, pageWidth * .025)) * body.settings.fontScale
        textWeight: body.settings.fontWeight
        secondaryColor: LyricColors.pageSecondary(body.playback.lyricAppearance, body.playback.lyricAppearance.simple, coverAccent, Theme.withAlpha(textColor, .3))
        textColor: LyricColors.simplePlayed(body.playback.lyricAppearance, coverAccent)
        unplayedColor: LyricColors.simpleUnplayed(body.playback.lyricAppearance, coverAccent)
        horizontalAlignment: Qt.AlignHCenter
        // The lyrics-only skin owns its text style; Apple Music motion stays
        // consistent without depending on another skin's display preferences.
        springEnabled: true
        blurEnabled: true
        scaleEnabled: true
        hidePassed: false
        focusPosition: .5
        wordFade: .5
        overlayRightReserve: body.overlayRightReserve
    }
}
