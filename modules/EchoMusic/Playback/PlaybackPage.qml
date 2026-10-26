pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
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
    readonly property bool presentationActive: visible && !playbackPart.replaced && (!contentItem.Window.window || (contentItem.Window.window.visible && contentItem.Window.window.visibility !== Window.Minimized))
    readonly property bool resourceActive: presentationActive && !skinHost.loaded
    readonly property real lyricToolsMargin: 16
    // The lyric viewport reaches the centered body's right edge, so a row's
    // hover/press box would sit under the page-anchored lyric tools. Reserve
    // that overlap plus a safe gap.
    readonly property real lyricOverlayReserve: Math.max(0, body.x + body.width - (page.width - lyricTools.width - lyricToolsMargin)) + 12
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
    readonly property color paletteBackground: playback.lyricAppearance.backgroundBlur && playback.cover.toString().length > 0 ? "#000000" : backgroundColor
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
                duration: !Theme.store.motionEnabled ? 0 : page.sourceCover && page.sourceCover.width > 0 && page.sourceCover.height > 0 ? 460 : 400
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
                duration: !Theme.store.motionEnabled ? 0 : page.sourceCover && page.sourceCover.width > 0 && page.sourceCover.height > 0 ? 360 : 400
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
                resourceActive: page.resourceActive
                baseColor: page.backgroundColor
                opacity: motion.hostOpacity
            }
            PlaybackPageMotion {
                id: motion
                objectName: "playbackPageMotion"
                anchors.fill: parent
                z: 10
                sourceCover: page.sourceCover
                targetCover: albumCover
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
            GridLayout {
                id: body
                readonly property bool narrow: page.width <= 768
                columns: narrow ? 1 : 2
                anchors.top: parent.top
                anchors.bottom: transport.top
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.topMargin: 96
                width: Math.min(1400, parent.width) - 64
                columnSpacing: 32
                rowSpacing: 16
                ColumnLayout {
                    id: coverSide
                    objectName: "playbackPageCoverSide"
                    Layout.minimumWidth: 0
                    Layout.maximumWidth: Layout.preferredWidth
                    Layout.preferredWidth: body.narrow ? body.width : Math.max(280, Math.min((body.width + 64) * 0.35, 420))
                    Layout.preferredHeight: body.narrow ? 16 + 160 + 24 + 33 + 6 + 21 : -1
                    Layout.fillHeight: !body.narrow
                    spacing: 0
                    Item {
                        visible: !body.narrow
                        Layout.fillHeight: true
                    }
                    PlaybackAlbumCover {
                        id: albumCover
                        opacity: motion.hostOpacity
                        artworkOpacity: motion.coverFlight ? 0 : 1
                        Layout.topMargin: body.narrow ? 16 : 24
                        objectName: "playbackPageCover"
                        Layout.alignment: Qt.AlignHCenter
                        Layout.preferredWidth: body.narrow ? 160 : Math.min(380, Math.max(220, parent.width * 0.8))
                        Layout.preferredHeight: Layout.preferredWidth
                        playback: page.playback
                        colors: page.colors
                        placeholderColor: page.colors.mutedSurface
                        placeholderGlyphSize: 32
                        resourceActive: page.resourceActive
                        EchoSurfaceShadow {
                            objectName: "playbackCoverShadow"
                            // One shadow follows the flight and remains in place
                            // on landing, without appearing as a new final layer.
                            x: motion.coverFlight ? motion.currentRect.x - motion.largeRect.x : 0
                            y: motion.coverFlight ? motion.currentRect.y - motion.largeRect.y : 0
                            width: parent.width
                            height: parent.height
                            surface: parent
                            z: -1
                            opacity: motion.coverFlight ? motion.cornerBlend : 1
                            radius: motion.coverFlight ? motion.smallRadius + (Theme.mediaRadius - motion.smallRadius) * motion.cornerBlend : Theme.mediaRadius
                            blur: 64
                            offset: Qt.vector2d(0, 24)
                            color: Qt.rgba(0, 0, 0, 0.3)
                            transform: Scale {
                                xScale: motion.coverFlight ? motion.currentRect.width / Math.max(1, motion.largeRect.width) : 1
                                yScale: motion.coverFlight ? motion.currentRect.height / Math.max(1, motion.largeRect.height) : 1
                            }
                        }
                    }
                    EchoTextTagRow {
                        objectName: "playbackPageTitle"
                        // Single-line metadata keeps its final line box even when
                        // empty text has no Flow width during the first layout.
                        Layout.preferredHeight: lineHeight
                        opacity: motion.hostOpacity * motion.contentOpacity
                        transform: Translate {
                            y: motion.contentOffset
                        }
                        fillTextWidth: true
                        Layout.topMargin: 24
                        Layout.leftMargin: 16
                        Layout.rightMargin: 16
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        Layout.maximumWidth: 380 - 32
                        Layout.alignment: Qt.AlignHCenter
                        text: page.playback.title
                        horizontalAlignment: Text.AlignHCenter
                        color: page.colors.text
                        lineHeight: 33
                        font: Theme.font(22, Theme.boldWeight)
                    }
                    EchoTextTagRow {
                        objectName: "playbackPageArtist"
                        Layout.preferredHeight: lineHeight
                        opacity: motion.hostOpacity * motion.contentOpacity
                        transform: Translate {
                            y: motion.contentOffset
                        }
                        fillTextWidth: true
                        Layout.topMargin: 6
                        Layout.bottomMargin: body.narrow ? 0 : 24
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        Layout.maximumWidth: 380 - 32
                        Layout.alignment: Qt.AlignHCenter
                        text: page.playback.artist
                        horizontalAlignment: Text.AlignHCenter
                        color: page.colors.secondaryText
                        lineHeight: 21
                        font: Theme.font(14, Theme.mediumWeight)
                    }
                    Item {
                        visible: !body.narrow
                        Layout.fillHeight: true
                    }
                }
                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumWidth: 0
                    Layout.minimumHeight: 0
                    Loader {
                        anchors.fill: parent
                        // Keep the lower page clear without letting the reserve
                        // alter the GridLayout row or the cover's geometry.
                        anchors.bottomMargin: Math.max(96, page.controlsTopReserve + 16)
                        opacity: motion.hostOpacity * motion.contentOpacity
                        transform: Translate {
                            y: motion.contentOffset
                        }
                        // Destroy the native delegate pool on close/minimize.
                        active: page.resourceActive
                        sourceComponent: CoverLyricViewport {
                            objectName: "playbackLyricViewport"
                            playback: page.playback
                            colors: page.colors
                            renderTime: page.renderTime
                            resourceActive: page.resourceActive
                            pageWidth: page.width
                            pageHeight: page.height
                            overlayRightReserve: page.lyricOverlayReserve
                        }
                    }
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
                opacity: (contentHover.hovered || activeFocus ? 1 : 0) * motion.hostOpacity
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
                opacity: motion.hostOpacity * motion.contentOpacity
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
