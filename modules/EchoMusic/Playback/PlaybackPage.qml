pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import EchoMusic.Theme
import EchoMusic.Controls
import EchoMusic.Playback

Popup {
    id: page
    EchoUiPart {
        id: playbackPart
        target: page.contentItem
        role: "playback.page"
        key: "player"
        surface: "playback"
        active: page.visible
        replaceable: true
        properties: ["background", "opacity"]
        actions: ["close", "settings", "skins"]
        onActionRequested: (action, arguments) => {
            if (action === "close")
                page.close();
            else if (action === "settings")
                page.globalSettingsRequested();
            else if (action === "skins")
                page.openSkins();
        }
    }
    objectName: "playbackPage"
    required property PlaybackService playback
    property real uiScale: 1
    property EchoCover sourceCover: null
    readonly property bool coverMoving: motion.coverFlight
    readonly property bool enteringPage: motion.entering
    property Component discussionComponent: null
    readonly property alias discussionHost: discussion.item
    readonly property bool discussionActive: presentationActive && (opened || enteringPage)
    property Component controlsComponent: null
    readonly property alias controlsItem: transport.item
    property real controlsTopReserve: 0
    property Component windowActions: null
    property rect nativeWindowControlsBounds: Qt.rect(0, 0, 0, 0)
    readonly property bool hasNativeWindowControls: Qt.platform.os === "osx" && nativeWindowControlsBounds.width > 0
    property bool coveredBySongWall: false
    readonly property bool presentationActive: visible && !coveredBySongWall && !playbackPart.replaced && (!contentItem.Window.window || (contentItem.Window.window.visible && contentItem.Window.window.visibility !== Window.Minimized))
    readonly property bool resourceActive: presentationActive && !skinHost.loaded
    readonly property bool portraitSelected: Theme.store.extensions.selectedSkin === "portrait"
    readonly property real portraitControlsOpacity: portraitBody.item ? (portraitBody.item as PortraitPlayback).controlsOpacity : 1
    readonly property bool portraitControlsShown: !portraitBody.item || (portraitBody.item as PortraitPlayback).controlsShown
    readonly property real lyricToolsMargin: 16
    // The lyric viewport reaches the centered body's right edge, so a row's
    // hover/press box would sit under the page-anchored lyric tools. Reserve
    // that overlap plus a safe gap.
    readonly property real lyricOverlayReserve: Math.max(0, coverBody.x + coverBody.width - (page.width - lyricTools.width - lyricToolsMargin)) + 12
    readonly property Item toastAnchor: transport.item as Item
    signal globalSettingsRequested
    function openSkins(): void {
        skinsOwner.open({
            showSettings: false
        });
    }
    function openSources(): void {
        sourceDialogOwner.open();
    }
    signal titleDragRequested
    signal titleDoubleClickRequested
    readonly property color backgroundColor: playbackPart.value("background", Theme.store.lyricBackground(playback.coverColor))
    // Match EchoMusic's LyricPage backgroundStyle -> lyricPageColorVariables:
    // artwork uses a black base; the solid mode uses the cover-derived color.
    // Hover ink still follows the current accent and its readability adjustment.
    readonly property color paletteBackground: (portraitSelected || playback.lyricAppearance.backgroundBlur && playback.cover.toString().length > 0) ? "#000000" : backgroundColor
    // Explicit inputs keep both background and accent changes observable to QML.
    readonly property var lyricPalette: Theme.store.lyricPalette(paletteBackground, Theme.accent)
    readonly property ThemeColors colors: ThemeColors {
        tone: "white"
        text: page.lyricPalette.text
        secondaryText: page.lyricPalette.secondary
        elevated: page.backgroundColor
        card: page.backgroundColor
        border: page.lyricPalette.border
        accent: Theme.accent
        accentText: page.lyricPalette.accentText
    }
    property real renderTime: playback.positionMs
    property bool clockPlaying: false
    property real clockSpeed: 1
    property real frameBudget: 0
    LyricClock {
        id: lyricClock
    }
    HoverHandler {
        id: contentHover
    }
    function syncClock(reset: bool): void {
        lyricClock.observe(playback.positionMs, playback.playing, playback.speed, reset);
        clockPlaying = playback.playing;
        clockSpeed = playback.speed;
        if (reset || !playback.playing)
            renderTime = lyricClock.sample();
    }
    onResourceActiveChanged: if (resourceActive) {
        syncClock(true);
        playback.ensureLyrics();
    }
    onAboutToShow: {
        syncClock(true);
        playback.ensureLyrics();
    }
    // The page only accepts pointer input once it has finished entering: until
    // then the layout is still moving, so one click can be split across two
    // surfaces (press on one item, release on another). A split click leaves the
    // window's pointer state stuck - no control reports hover any more and a
    // press on the progress row seeks by accident.
    property bool acceptingInput: false
    onOpened: {
        motion.finish();
        acceptingInput = true;
    }
    onClosed: {
        motion.finish();
        acceptingInput = false;
    }
    // Fallback for an interrupted entry transition: never leave the page
    // without interaction.
    Timer {
        interval: 700
        running: page.visible && page.enteringPage && !page.acceptingInput
        onTriggered: page.acceptingInput = true
    }
    onAboutToHide: {
        skinsOwner.close();
        sourceDialogOwner.close();
    }
    Connections {
        target: Theme.store.extensions
        function onRequestRaised(action: string): void {
            if (action === "closeSkinSettings" && page.visible)
                skinsOwner.close();
        }
    }
    Connections {
        target: page.playback
        function onPositionChanged() {
            page.syncClock(false);
        }
        function onChanged() {
            if (page.resourceActive)
                page.playback.ensureLyrics();
            if (page.clockPlaying !== page.playback.playing || page.clockSpeed !== page.playback.speed) {
                lyricClock.setPlaybackState(page.playback.playing, page.playback.speed);
                page.clockPlaying = page.playback.playing;
                page.clockSpeed = page.playback.speed;
                page.renderTime = lyricClock.sample();
            }
        }
        function onTimelineReset() {
            page.syncClock(true);
        }
    }
    FrameAnimation {
        running: page.resourceActive && page.playback.playing && page.playback.lyrics.count > 0 && page.playback.lyrics.scrollable
        onTriggered: {
            page.frameBudget += frameTime;
            if (!page.playback.lyricAppearance.highRefreshRate) {
                const steps = Math.floor((page.frameBudget + .0005) * 60);
                if (steps < 1)
                    return;
                page.frameBudget = Math.max(0, page.frameBudget - steps / 60);
            } else {
                page.frameBudget = 0;
            }
            page.renderTime = Math.min(page.playback.durationMs || Infinity, lyricClock.sample());
        }
    }
    parent: Overlay.overlay
    width: parent ? parent.width / uiScale : 0
    height: parent ? parent.height / uiScale : 0
    scale: uiScale
    transformOrigin: Popup.TopLeft
    padding: 0
    focus: true
    modal: true
    dim: false
    closePolicy: Popup.CloseOnEscape
    EchoPopupLifecycle {
        surface: page
    }
    // One ordinary scene subtree contains the actual page pixels. Popups remain
    // outside it in Overlay, so the existing local blur cannot capture itself.
    readonly property alias backgroundSurface: pageBackground
    background: null
    ParallelAnimation {
        id: openingMotion
        NumberAnimation {
            target: motion
            property: "progress"
            from: 0
            to: 1
            duration: motion.travelDuration
            easing.type: Easing.BezierSpline
            easing.bezierCurve: [.22, 1, .36, 1, 1, 1]
        }
        NumberAnimation {
            target: motion
            property: "hostOpacity"
            to: 1
            duration: Theme.store.motionEnabled ? (motion.coverFlight ? 200 : 400) : 0
            easing.type: Easing.BezierSpline
            easing.bezierCurve: [.22, 1, .36, 1, 1, 1]
        }
        SequentialAnimation {
            PauseAnimation {
                duration: Theme.store.motionEnabled && motion.coverFlight ? 70 : 0
            }
            ParallelAnimation {
                NumberAnimation {
                    target: motion
                    property: "contentOpacity"
                    to: 1
                    duration: Theme.store.motionEnabled ? (motion.coverFlight ? 360 : 400) : 0
                    easing.type: Easing.BezierSpline
                    easing.bezierCurve: [.22, 1, .36, 1, 1, 1]
                }
                NumberAnimation {
                    target: motion
                    property: "contentOffset"
                    to: 0
                    duration: Theme.store.motionEnabled && motion.coverFlight ? 360 : 0
                    easing.type: Easing.BezierSpline
                    easing.bezierCurve: [.22, 1, .36, 1, 1, 1]
                }
            }
        }
    }
    ParallelAnimation {
        id: closingMotion
        NumberAnimation {
            target: motion
            property: "progress"
            from: 0
            to: 1
            duration: motion.travelDuration
            easing.type: Easing.BezierSpline
            easing.bezierCurve: [.4, 0, .2, 1, 1, 1]
        }
        NumberAnimation {
            target: motion
            property: "hostOpacity"
            to: 0
            duration: Theme.store.motionEnabled ? (motion.coverFlight ? 220 : 400) : 0
            easing.type: Easing.BezierSpline
            easing.bezierCurve: [.4, 0, .2, 1, 1, 1]
        }
    }
    // The Popup owns modal/focus lifetime; ordinary native animations own
    // presentation. Its transition waits for that animation before closing.
    enter: Transition {
        SequentialAnimation {
            ScriptAction {
                script: {
                    closingMotion.stop();
                    openingMotion.stop();
                    motion.prepare(true);
                    openingMotion.start();
                }
            }
            PauseAnimation {
                // The transition job captures this before ScriptAction runs.
                duration: !Theme.store.motionEnabled ? 0 : !page.portraitSelected && page.sourceCover && page.sourceCover.width > 0 && page.sourceCover.height > 0 ? 460 : 400
            }
        }
    }
    exit: Transition {
        SequentialAnimation {
            ScriptAction {
                script: {
                    openingMotion.stop();
                    closingMotion.stop();
                    motion.prepare(false);
                    closingMotion.start();
                }
            }
            PauseAnimation {
                duration: !Theme.store.motionEnabled ? 0 : !page.portraitSelected && page.sourceCover && page.sourceCover.width > 0 && page.sourceCover.height > 0 ? 360 : 400
            }
        }
    }
    contentItem: EchoBackdropRoot {
        objectName: "playbackBackdropScene"
        enabled: page.acceptingInput
        samplingActive: page.visible
        readonly property string echoUiSurface: "playback"
        opacity: playbackPart.value("opacity", 1)
        AppearanceHost {
            id: skinHost
            objectName: "pluginPlaybackSkinHost"
            anchors.fill: parent
            z: 20
            entry: Theme.store.extensions.skin
            placement: "playback"
            resourceActive: page.presentationActive
        }
        Item {
            objectName: "nativePlaybackContents"
            anchors.fill: parent
            visible: !playbackPart.replaced && !skinHost.loaded

            PlaybackBackground {
                id: pageBackground
                anchors.fill: parent
                z: -1
                playback: page.playback
                resourceActive: page.resourceActive && (!page.portraitSelected || !portraitBody.item || !(portraitBody.item as PortraitPlayback).backgroundReady)
                visible: !page.portraitSelected || !portraitBody.item || !(portraitBody.item as PortraitPlayback).backgroundReady
                baseColor: page.backgroundColor
                opacity: motion.hostOpacity
            }
            PlaybackPageMotion {
                id: motion
                objectName: "playbackPageMotion"
                anchors.fill: parent
                z: 10
                sourceCover: page.portraitSelected ? null : page.sourceCover
                targetCover: coverBody.item ? (coverBody.item as CoverPlayback).cover : null
            }
            // AppKit owns the native traffic lights outside this subtree. Every
            // application control follows the same zoom as the playback content.
            Item {
                id: windowChrome
                parent: page.contentItem
                z: 30
                visible: !playbackPart.replaced && (!skinHost.loaded || Theme.store.extensions.skin.titlebar === "host")
                objectName: "playbackWindowChrome"
                width: parent.width
                height: Math.max(46, 35 / page.uiScale)
                EchoWindowDragArea {
                    objectName: "playbackTitleDragArea"
                    anchors.fill: parent
                    onMoveRequested: page.titleDragRequested()
                    onDoubleClickRequested: page.titleDoubleClickRequested()
                }
                Loader {
                    objectName: "playbackWindowActionsLoader"
                    opacity: motion.hostOpacity
                    x: parent.width - width - 12
                    y: (parent.height - height) / 2
                    sourceComponent: page.windowActions
                }
                EchoIconButton {
                    objectName: "playbackCollapseAction"
                    opacity: motion.hostOpacity
                    // AppKit owns its default buttons; the adjacent action uses
                    // their read-only window bounds converted to page coordinates.
                    x: page.hasNativeWindowControls ? (page.nativeWindowControlsBounds.x + page.nativeWindowControlsBounds.width) / page.uiScale + 8 : 16
                    y: page.hasNativeWindowControls ? (page.nativeWindowControlsBounds.y + page.nativeWindowControlsBounds.height / 2) / page.uiScale - height / 2 : (parent.height - height) / 2
                    hoverBackground: Qt.platform.os !== "osx"
                    implicitWidth: 34
                    implicitHeight: 34
                    padding: 7
                    colors: page.colors
                    glyph: "chevron-down"
                    text: qsTr("收起播放页")
                    onClicked: page.close()
                }
            }
            Loader {
                id: coverBody
                objectName: "coverPlaybackOwner"
                anchors.horizontalCenter: parent.horizontalCenter
                y: 96
                width: Math.min(1400, parent.width) - 64
                height: Math.max(0, transport.y - y)
                // The actual Popup is created on demand. Keep its cover layout
                // stable through open/close transitions; decoders and lyric rows
                // still follow resourceActive, and other skins unload this body.
                active: !page.portraitSelected && !skinHost.loaded
                sourceComponent: CoverPlayback {
                    playback: page.playback
                    colors: page.colors
                    pageMotion: motion
                    resourceActive: page.resourceActive
                    pageWidth: page.width
                    pageHeight: page.height
                    renderTime: page.renderTime
                    controlsTopReserve: page.controlsTopReserve
                    overlayRightReserve: page.lyricOverlayReserve
                }
            }
            Loader {
                id: portraitBody
                objectName: "portraitPlaybackOwner"
                anchors.fill: parent
                opacity: motion.hostOpacity
                active: page.resourceActive && page.portraitSelected
                sourceComponent: PortraitPlayback {
                    playback: page.playback
                    colors: page.colors
                    renderTime: page.renderTime
                    resourceActive: page.resourceActive
                    baseColor: page.backgroundColor
                    controlsReserve: Math.max(96, page.controlsTopReserve + 16)
                    controlsHeight: transport.height
                    controlsAboveReserve: page.controlsTopReserve
                    lyricToolsTop: lyricTools.y
                    lyricToolsLeft: lyricTools.x
                    popupOpen: skinsOwner.visible || sourceDialogOwner.visible
                }
            }
            Loader {
                id: discussion
                parent: page.contentItem
                opacity: motion.hostOpacity
                anchors.fill: parent
                z: 5
                sourceComponent: page.discussionComponent
                active: page.visible
            }
            PlaybackLyricTools {
                id: lyricTools
                parent: page.contentItem
                z: 30
                visible: !playbackPart.replaced && (!skinHost.loaded || Theme.store.extensions.skin.tools === "host")
                x: parent.width - width - page.lyricToolsMargin
                y: Math.max(104, 35 / page.uiScale + 58)
                height: Math.max(0, parent.height - y - 112)
                opacity: (page.portraitControlsShown && (contentHover.hovered || activeFocus) ? 1 : 0) * motion.hostOpacity
                enabled: opacity > 0
                playback: page.playback
                colors: page.colors
                onSourceRequested: sourceDialogOwner.open()
                Behavior on opacity {
                    NumberAnimation {
                        duration: 200
                    }
                }
            }
            EchoPopupLoader {
                id: skinsOwner
                objectName: "lyricSkinDrawerOwner"
                hostItem: page.contentItem
                sourceComponent: Component {
                    LyricSkinDrawer {
                        id: skins
                        playback: page.playback
                        uiScale: page.uiScale
                        onGlobalSettingsRequested: page.globalSettingsRequested()
                    }
                }
            }
            EchoPopupLoader {
                id: sourceDialogOwner
                objectName: "lyricSourceDialogOwner"
                hostItem: page.contentItem
                sourceComponent: Component {
                    LyricSourceDialog {
                        id: sourceDialog
                        playback: page.playback
                        uiScale: page.uiScale
                    }
                }
            }
            Loader {
                id: transport
                objectName: "playbackTransport"
                visible: page.portraitControlsOpacity > 0
                opacity: motion.hostOpacity * motion.contentOpacity * page.portraitControlsOpacity
                enabled: opacity > 0
                transform: Translate {
                    y: motion.contentOffset
                }
                anchors.bottom: parent.bottom
                width: parent.width
                height: 88
                sourceComponent: page.controlsComponent
                active: page.visible
            }
        }
    }
}
