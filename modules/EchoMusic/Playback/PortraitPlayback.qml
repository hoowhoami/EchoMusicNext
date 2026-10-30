pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import EchoMusic.Theme
import EchoMusic.Controls
import EchoMusic.Playback

Item {
    id: portrait
    objectName: "portraitPlayback"
    required property PlaybackService playback
    required property ThemeColors colors
    required property real renderTime
    required property bool resourceActive
    required property color baseColor
    property real controlsReserve: 96
    property real controlsHeight: 88
    property real controlsAboveReserve: 0
    property bool popupOpen: false
    property real lyricToolsTop: 104
    property real lyricToolsLeft: width - 52
    readonly property LyricPortraitAppearance settings: playback.lyricAppearance.portrait
    readonly property bool backgroundReady: backdrop.ready
    readonly property bool galleryActive: resourceActive && !Theme.store.windowFrosted && Theme.store.transparency < 100
    property bool collapsed: false
    property bool initialized: false
    property bool mouseActive: true
    property point lastPointerPosition: Qt.point(-1, -1)
    property bool pointerObserved: false
    readonly property bool controlsShown: !collapsed || !settings.hideControls || mouseActive || popupOpen
    property real controlsOpacity: controlsShown ? 1 : 0
    readonly property real expandedBottomInset: controlsHeight + controlsReserve
    readonly property real compactBottomInset: 24 + (controlsShown ? controlsHeight + controlsAboveReserve : 0)
    readonly property bool layoutMotionEnabled: resourceActive && Theme.store.motionEnabled
    // Translate the existing virtual viewport, keeping its row layout and GPU
    // glyphs intact. The measured singing block lands above the actual controls.
    readonly property real lyricOffsetTarget: collapsed ? Math.max(0, viewport.height * (1 - viewport.focusPosition) + expandedBottomInset - compactBottomInset - viewport.focusBlockHeight / 2) : 0
    readonly property real lyricOffset: placement.value
    readonly property bool placementRunning: placement.running
    function updatePlacement(): void {
        if (!initialized)
            return;
        if (layoutMotionEnabled)
            placement.retarget(lyricOffsetTarget, 0, 0, settings.enableSpring);
        else
            placement.snap(lyricOffsetTarget);
    }
    onLyricOffsetTargetChanged: updatePlacement()
    onLayoutMotionEnabledChanged: updatePlacement()
    LyricSpring {
        id: placement
    }
    FrameAnimation {
        running: portrait.layoutMotionEnabled && placement.running
        onTriggered: placement.advance()
    }
    Behavior on controlsOpacity {
        NumberAnimation {
            id: controlsFade
            duration: Theme.panelDuration
            easing.type: Easing.BezierSpline
            easing.bezierCurve: Theme.easeStandard
        }
    }
    Connections {
        target: Theme.store
        function onChanged(): void {
            if (!Theme.store.motionEnabled)
                controlsFade.complete();
        }
    }
    function scheduleCollapse(): void {
        collapseDelay.stop();
        if (settings.autoCollapse && !collapsed && !viewport.browsing && !popupOpen && playback.lyrics.scrollable)
            collapseDelay.start();
    }
    function activity(): void {
        mouseActive = true;
        mouseIdle.restart();
        scheduleCollapse();
    }
    function toggleLyrics(): void {
        collapsed = !collapsed;
        viewport.resumeFollow();
        activity();
    }
    onGalleryActiveChanged: playback.portraits.setActive(galleryActive)
    onPopupOpenChanged: scheduleCollapse()
    Component.onCompleted: {
        initialized = true;
        playback.portraits.setActive(galleryActive);
        activity();
        updatePlacement();
    }
    Component.onDestruction: playback.portraits.setActive(false)
    Connections {
        target: portrait.settings
        function onChanged(): void {
            portrait.scheduleCollapse();
        }
    }
    Connections {
        target: portrait.playback.lyrics
        function onChanged(): void {
            if (!portrait.playback.lyrics.scrollable)
                portrait.collapsed = false;
            portrait.scheduleCollapse();
        }
    }
    TapHandler {
        enabled: portrait.collapsed
        acceptedButtons: Qt.LeftButton
        onTapped: {
            portrait.collapsed = false;
            viewport.resumeFollow();
            portrait.activity();
        }
    }
    HoverHandler {
        blocking: false
        onPointChanged: {
            const position = point.scenePosition;
            // Native hover refreshes can update HandlerPoint while a pointer
            // stays still. Only actual movement extends the idle deadline.
            if (!portrait.pointerObserved || Math.abs(position.x - portrait.lastPointerPosition.x) + Math.abs(position.y - portrait.lastPointerPosition.y) > .5) {
                portrait.pointerObserved = true;
                portrait.lastPointerPosition = position;
                portrait.activity();
            }
        }
    }
    Timer {
        id: mouseIdle
        interval: 3000
        onTriggered: portrait.mouseActive = false
    }
    Timer {
        id: collapseDelay
        objectName: "portraitIdleCollapseTimer"
        interval: portrait.settings.collapseDelay * 1000
        onTriggered: portrait.collapsed = true
    }
    PortraitBackdrop {
        id: backdrop
        anchors.fill: parent
        playback: portrait.playback
        baseColor: portrait.baseColor
        resourceActive: portrait.resourceActive
    }
    EchoSoftButton {
        id: collapseButton
        objectName: "portraitCollapseLyrics"
        x: 20
        y: portrait.lyricToolsTop
        implicitWidth: 36
        implicitHeight: 36
        colors: portrait.colors
        glyphSize: 16
        leftPadding: 8
        rightPadding: 8
        glyph: portrait.collapsed ? "arrow-bar-to-up" : "arrow-bar-down"
        text: ""
        Accessible.name: portrait.collapsed ? qsTr("展开歌词") : qsTr("收起歌词")
        EchoToolTip {
            parent: collapseButton
            text: collapseButton.Accessible.name
            requestedVisible: collapseButton.hovered
        }
        opacity: portrait.mouseActive || activeFocus ? 1 : .65
        onClicked: portrait.toggleLyrics()
    }
    RowLayout {
        objectName: "portraitGalleryNavigation"
        anchors.horizontalCenter: parent.horizontalCenter
        y: portrait.lyricToolsTop + (36 - height) / 2
        spacing: 4
        visible: portrait.playback.portraits.count > 1 && !portrait.collapsed
        EchoIconButton {
            objectName: "portraitPreviousImage"
            colors: portrait.colors
            glyph: "chevron-left"
            text: qsTr("上一张写真")
            onClicked: {
                portrait.playback.portraits.previous();
                portrait.activity();
            }
        }
        EchoTextTagRow {
            text: (portrait.playback.portraits.index + 1) + " / " + portrait.playback.portraits.count
            color: portrait.colors.text
            font: Theme.font(12, 600)
            lineHeight: 20
        }
        EchoIconButton {
            objectName: "portraitNextImage"
            colors: portrait.colors
            glyph: "chevron-right"
            text: qsTr("下一张写真")
            onClicked: {
                portrait.playback.portraits.next();
                portrait.activity();
            }
        }
    }
    Item {
        id: lyricStage
        objectName: "portraitLyricStage"
        anchors.fill: parent
        anchors.topMargin: 112
        anchors.bottomMargin: 16 + portrait.controlsOpacity * (portrait.controlsHeight + portrait.controlsAboveReserve)
        clip: true
        PlaybackLyricViewport {
            id: viewport
            objectName: "portraitLyricViewport"
            anchors.horizontalCenter: parent.horizontalCenter
            y: portrait.height - portrait.expandedBottomInset - height - lyricStage.y
            transform: Translate {
                y: portrait.lyricOffset
            }
            width: Math.max(0, Math.min(920, parent.width - (parent.width < 600 ? 48 : 128)))
            height: Math.max(0, Math.min(portrait.height - 112 - portrait.expandedBottomInset, Math.max(260, portrait.height * .43)))
            playback: portrait.playback
            colors: portrait.colors
            renderTime: portrait.renderTime
            resourceActive: portrait.resourceActive
            pageWidth: portrait.width
            pageHeight: portrait.height
            fontSize: Math.max(22, Math.min(32, portrait.width * .03)) * portrait.settings.fontScale
            textWeight: portrait.settings.fontWeight
            textColor: LyricColors.portraitPlayed(portrait.playback.lyricAppearance, coverAccent)
            unplayedColor: LyricColors.portraitUnplayed(portrait.playback.lyricAppearance, coverAccent)
            horizontalAlignment: Qt.AlignHCenter
            secondaryFontSize: Math.max(12, fontSize * .55)
            secondaryGap: fontSize * .22
            softFocus: portrait.settings.enableBlur
            blurEnabled: portrait.settings.enableBlur
            springEnabled: portrait.settings.enableSpring
            staggerEnabled: portrait.settings.enableSpring
            scaleEnabled: portrait.settings.enableScale
            hidePassed: false
            focusPosition: .55
            compact: portrait.collapsed
            edgeFade: fontSize * 1.8
            rowBackgroundEnabled: !portrait.collapsed
            overlayRightReserve: Math.max(0, x + width - portrait.lyricToolsLeft) + 12
            pauseMotionWithPlayback: true
            onBrowseStarted: {
                portrait.collapsed = false;
                collapseDelay.stop();
            }
            onBrowsingChanged: portrait.scheduleCollapse()
        }
    }
}
