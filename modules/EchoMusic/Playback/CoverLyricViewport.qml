pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Effects
import EchoMusic.Theme
import EchoMusic.Controls
import EchoMusic.Playback

Item {
    id: viewport
    Connections {
        target: EchoLyricEffectRegistry
        function onChanged() {
            viewport.scheduleUpdate(false, false, true);
        }
    }
    EchoUiPart {
        id: effectHost
        target: viewport
        role: "lyrics.viewport"
        key: "page-lyrics"
        surface: "playback"
        active: viewport.resourceActive
        actions: ["scrollTo", "resumeFollow"]
        state: observed ? ({
                scope: "page",
                timelineMs: viewport.renderTime,
                positionMs: viewport.playback.positionMs,
                durationMs: viewport.playback.durationMs,
                playbackRate: viewport.playback.speed,
                playing: viewport.playback.playing,
                appearance: {
                    font: viewport.baseFont,
                    playedColor: viewport.textColor,
                    unplayedColor: viewport.unplayedColor
                },
                currentIndex: viewport.focusLine,
                scrollIndex: lines.currentIndex,
                lyricsMode: playback.lyricText.wantTranslation ? playback.lyricText.wantRomanization ? "both" : "translation" : playback.lyricText.wantRomanization ? "romanization" : "none",
                lineCount: viewport.playback.lyrics.count,
                following: lines.following,
                browsing: !lines.following,
                reducedMotion: !Theme.store.motionEnabled,
                offsetMs: viewport.playback.lyrics.offsetMs
            }) : ({})
        onActionRequested: (action, args) => {
            if (action === "resumeFollow") {
                lines.following = true;
                viewport.scheduleUpdate(false, false, true);
            } else if (action === "scrollTo" && lines.following && Number.isFinite(args.position)) {
                lines.cancelFlick();
                lines.contentY = Math.max(lines.originY, Math.min(lines.originY + Math.max(0, lines.contentHeight - lines.height), args.position));
            }
        }
    }
    required property PlaybackService playback
    required property ThemeColors colors
    required property real renderTime
    property bool resourceActive: visible
    property real pageWidth: width
    property real pageHeight: height
    // Right overlay (lyric tools) reserve so a row's hover/press box clears it.
    property real overlayRightReserve: 0
    property font baseFont: Theme.store.lyricBaseFont
    property int textWeight: appearance.fontWeight
    readonly property LyricAppearance appearance: playback.lyricAppearance
    property real fontSize: (pageWidth <= 768 ? Math.max(12, pageWidth * .08) : Math.max(12, pageHeight * .05, pageWidth * .025)) * appearance.fontScale
    readonly property color coverAccent: Theme.store.lyricAccent(playback.coverColor)
    property color textColor: LyricColors.played(appearance, coverAccent)
    property color unplayedColor: LyricColors.unplayed(appearance, coverAccent)
    property int horizontalAlignment: Qt.AlignLeft
    property real secondaryFontSize: Math.max(10, fontSize * .5)
    property real secondaryGap: fontSize * .3
    property bool softFocus: true
    property bool filterLyrics: true
    property bool springEnabled: appearance.enableSpring
    property bool scaleEnabled: appearance.enableScale
    property bool hidePassed: appearance.hidePassedLines
    property real focusPosition: appearance.alignPosition
    property real wordFade: appearance.wordFadeWidth
    function nextLineIndex(index: int): int {
        return timeline.nextRow(index);
    }
    LyricTimeline {
        id: timeline
        objectName: "coverLyricTimeline"
        model: viewport.resourceActive ? viewport.playback.lyrics : null
        filter: viewport.filterLyrics
        timeMs: viewport.renderTime
        onChanged: viewport.scheduleUpdate(true, false, false)
    }
    readonly property int focusLine: timeline.focusIndex
    readonly property real gapStart: timeline.gapStart
    readonly property real nextStart: timeline.gapEnd
    readonly property bool waiting: timeline.gapStart >= 0
    readonly property bool interlude: waiting && interludeAnimation.available
    readonly property real interludeRowHeight: Math.max(fontSize * .5, Math.min(pageHeight * .01, fontSize * 3)) + fontSize * 1.6
    LyricInterlude {
        id: interludeAnimation
        timeMs: viewport.waiting && viewport.resourceActive ? viewport.renderTime : 0
    }
    function syncInterlude(restart: bool): void {
        interludeAnimation.configure(waiting ? gapStart : -1, waiting ? nextStart : -1, renderTime, timeline.gapAnchor < 0, restart);
    }
    property bool pendingInterludeSync: false
    property bool pendingInterludeReset: false
    property bool pendingImmediateFollow: false
    property bool seekingLayout: false
    function scheduleUpdate(sync: bool, reset: bool, immediate: bool): void {
        pendingInterludeSync = pendingInterludeSync || sync;
        pendingInterludeReset = pendingInterludeReset || reset;
        pendingImmediateFollow = pendingImmediateFollow || immediate;
        if (deferredUpdate)
            deferredUpdate.restart();
    }
    // Unlike engine-wide callLater closures, this one-shot belongs to the
    // viewport and is cancelled when a fast/reduced-motion close destroys it.
    Timer {
        id: deferredUpdate
        interval: 0
        onTriggered: {
            const sync = viewport.pendingInterludeSync;
            const reset = viewport.pendingInterludeReset;
            const immediate = viewport.pendingImmediateFollow;
            viewport.pendingInterludeSync = false;
            viewport.pendingInterludeReset = false;
            viewport.pendingImmediateFollow = false;
            if (reset)
                timeline.reset(viewport.renderTime);
            if (sync)
                viewport.syncInterlude(reset);
            viewport.seekingLayout = reset;
            viewport.follow(immediate);
            viewport.seekingLayout = false;
        }
    }
    Component.onCompleted: scheduleUpdate(true, true, true)
    onGapStartChanged: scheduleUpdate(true, false, false)
    onNextStartChanged: scheduleUpdate(true, false, false)
    onInterludeChanged: scheduleUpdate(false, false, false)
    Connections {
        target: viewport.playback
        function onTimelineReset() {
            lines.following = true;
            resume.stop();
            viewport.scheduleUpdate(true, true, false);
        }
    }
    signal scrollRequested(real from, real destination, bool animate)
    // Coalesce presentation-state bindings before measuring the new anchor.
    onWaitingChanged: {
        scheduleUpdate(true, false, false);
    }
    function delayFor(index: int): real {
        // Only materialized delegates participate. Count rows, never divide a
        // screen coordinate by one delegate's variable wrapped height.
        const rows = lines.contentItem.children.filter(item => item.index !== undefined && item.pooled === false && item.y + item.height >= lines.contentY).sort((a, b) => timeline.ordinal(a.index) - timeline.ordinal(b.index));
        let delay = 0;
        let step = 50;
        for (const row of rows) {
            if (timeline.ordinal(row.index) >= timeline.ordinal(index))
                return delay;
            delay += step;
            if (timeline.ordinal(row.index) >= timeline.ordinal(focusLine))
                step /= 1.05;
        }
        return 0;
    }
    function follow(immediate: bool): void {
        if (!resourceActive || !lines.following || playback.lyrics.count === 0 || lines.height <= 0)
            return;
        const target = Math.max(0, waiting ? nextLineIndex(timeline.gapAnchor) : focusLine);
        const animate = !immediate && lines.initialized && Math.abs(timeline.ordinal(target) - lines.lastFollowIndex) <= 2 && Theme.store.motionEnabled;
        const previous = lines.contentY;
        const previousOrigin = lines.originY;
        // Qt measures/places the current delegate. Keep no live range binding
        // to currentItem.height: virtualized replacement can feed that back into
        // item selection. Large seeks/opening snap directly to the live slice.
        lines.currentIndex = timeline.ordinal(target);
        lines.forceLayout();
        let measured = lines.itemAtIndex(timeline.ordinal(target));
        if (!measured) {
            lines.positionViewAtIndex(timeline.ordinal(target), ListView.Center);
            lines.forceLayout();
            measured = lines.itemAtIndex(timeline.ordinal(target));
        }
        if (!measured)
            return;
        // Center the measured delegate. positionViewAtIndex can use an
        // estimated prefix while newly materialized wrapped rows settle.
        // When a short window cannot fit the complete main/translation block,
        // keep the singing text centered rather than centering the subtitles
        // too and clipping the whole bright line above the visible area.
        const textAnchor = lines.height < measured.textRowHeight ? viewport.fontSize * .4 + measured.mainHeight / 2 : measured.textRowHeight / 2;
        const anchor = viewport.interlude ? measured.headerHeight / 2 : measured.headerHeight + textAnchor;
        const desired = measured.y + anchor - lines.focusCenter;
        if (EchoLyricEffectRegistry.hasHandlers("page") && EchoLyricEffectRegistry.requestScroll(effectHost.identity, {
            scope: "page",
            index: target,
            position: desired,
            previous: previous,
            smooth: animate,
            browsing: !lines.following,
            reducedMotion: !Theme.store.motionEnabled
        })) {
            lines.initialized = true;
            lines.lastFollowIndex = timeline.ordinal(target);
            return;
        }
        lines.contentY = desired;
        lines.animateScroll = animate;
        // ListView owns virtualization and target geometry. Each materialized
        // line follows that target independently, retaining its current spring
        // position when another lyric arrives during a transition.
        scrollRequested(previous + lines.originY - previousOrigin, desired, animate);
        lines.initialized = true;
        lines.lastFollowIndex = timeline.ordinal(target);
    }
    onResourceActiveChanged: {
        resume.stop();
        lines.initialized = false;
        if (resourceActive) {
            lines.following = true;
            viewport.scheduleUpdate(true, true, true);
        }
    }
    onHeightChanged: scheduleUpdate(false, false, true)
    onWidthChanged: scheduleUpdate(false, false, true)
    onFontSizeChanged: scheduleUpdate(false, false, true)
    onBaseFontChanged: scheduleUpdate(false, false, true)
    onTextWeightChanged: scheduleUpdate(false, false, true)
    onSecondaryFontSizeChanged: scheduleUpdate(false, false, true)
    onSecondaryGapChanged: scheduleUpdate(false, false, true)
    Connections {
        target: viewport.playback.lyrics
        function onChanged() {
            resume.stop();
            viewport.scheduleUpdate(true, true, true);
        }
    }
    Connections {
        target: viewport.appearance
        function onChanged() {
            viewport.scheduleUpdate(false, false, true);
        }
    }
    ListView {
        id: lines
        EchoWheelScroll.enabled: viewport.resourceActive
        EchoWheelScroll.motionEnabled: Theme.store.motionEnabled
        objectName: "playbackLyricList"
        anchors.fill: parent
        clip: true
        reuseItems: true
        cacheBuffer: 200
        topMargin: height
        bottomMargin: height
        model: viewport.resourceActive ? timeline.rows : null
        boundsBehavior: Flickable.StopAtBounds
        currentIndex: -1
        keyNavigationEnabled: false
        highlightRangeMode: ListView.NoHighlightRange
        readonly property real focusCenter: height * viewport.focusPosition
        property bool initialized: false
        property int lastFollowIndex: -1
        onCountChanged: viewport.scheduleUpdate(false, false, true)
        property bool following: true
        property bool animateScroll: false
        onMovementStarted: {
            following = false;
            resume.stop();
        }
        onMovementEnded: if (viewport.playback.lyrics.scrollable)
            resume.restart()
        WheelHandler {
            blocking: false
            onWheel: event => {
                lines.following = false;
                if (viewport.playback.lyrics.scrollable)
                    resume.restart();
                event.accepted = false;
            }
        }
        delegate: Item {
            id: line
            required property int sourceRow
            readonly property int index: sourceRow
            required property string lineText
            required property real startMs
            required property real durationMs
            required property bool wordTimed
            required property var words
            required property string translation
            required property string romanization
            required property bool filtered
            property bool pooled: false
            readonly property bool active: {
                const revision = timeline.revision;
                return !viewport.playback.lyrics.scrollable || timeline.active(index);
            }
            readonly property bool inViewport: !pooled && y + height >= Math.min(lines.contentY, scrollY) - 64 && y <= Math.max(lines.contentY, scrollY) + lines.height + 64
            readonly property real scrollY: scrollMotion.value
            property bool animatePosition: false
            property bool freshPosition: true
            readonly property real headerHeight: viewport.interlude && index === viewport.nextLineIndex(timeline.gapAnchor) ? viewport.interludeRowHeight : 0
            readonly property var rubyUnits: wordTimed && romanization.length > 0 ? viewport.playback.lyrics.rubyAt(line.index) : []
            readonly property bool ruby: viewport.playback.lyricText.wantRomanization && viewport.playback.lyricText.romanizationAsRuby && rubyUnits.length > 0
            readonly property real mainHeight: ruby && rubyLine.item ? (rubyLine.item as LyricRubyLine).implicitHeight : lyric.textHeight
            readonly property real textRowHeight: viewport.filterLyrics && filtered ? 0 : mainHeight + (secondary.visible ? secondary.implicitHeight + viewport.secondaryGap : 0) + viewport.fontSize * .8
            property bool positioned: false
            property real previousY: 0
            property real previousHeader: 0
            property bool dotsPositioned: false
            onYChanged: {
                if (positioned && lines.initialized && !pooled) {
                    scrollMotion.shift(y - previousY);
                    dotMotion.shift(y - previousY);
                }
                previousY = y;
            }
            onHeaderHeightChanged: {
                if (positioned && lines.initialized && !pooled)
                    scrollMotion.shift(headerHeight - previousHeader);
                previousHeader = headerHeight;
                dotsPositioned = false;
            }
            LyricSpring {
                id: dotMotion
            }
            LyricSpring {
                id: scrollMotion
            }
            Component.onCompleted: {
                scrollMotion.snap(lines.contentY);
                dotMotion.snap(lines.contentY);
                previousY = y;
                previousHeader = headerHeight;
                positioned = true;
            }
            FrameAnimation {
                running: (scrollMotion.running || dotMotion.running || scaleMotion.running) && viewport.resourceActive && !line.pooled
                onTriggered: {
                    scrollMotion.advance();
                    dotMotion.advance();
                    scaleMotion.advance();
                }
            }
            Connections {
                target: lines
                function onFollowingChanged(): void {
                    if (!lines.following) {
                        line.animatePosition = false;
                        scrollMotion.snap(lines.contentY);
                        dotMotion.snap(lines.contentY);
                    }
                }
            }
            Connections {
                target: viewport
                function onScrollRequested(from: real, destination: real, animate: bool): void {
                    if (line.headerHeight > 0) {
                        if (!animate || !line.dotsPositioned)
                            dotMotion.snap(destination);
                        else
                            dotMotion.retarget(destination, 0, 0, viewport.springEnabled);
                        line.dotsPositioned = true;
                    } else {
                        dotMotion.snap(destination);
                    }
                    if (!animate || line.pooled || !line.inViewport || line.freshPosition) {
                        line.animatePosition = false;
                        scrollMotion.snap(destination);
                    } else {
                        line.animatePosition = true;
                        const previous = timeline.previousRow(viewport.focusLine);
                        const interval = previous >= 0 ? timeline.startAt(viewport.focusLine) - timeline.startAt(previous) : 0;
                        scrollMotion.retarget(destination, viewport.waiting || viewport.seekingLayout ? 0 : interval, viewport.seekingLayout ? 0 : Math.round(viewport.delayFor(line.index)), viewport.springEnabled);
                    }
                    line.freshPosition = false;
                }
            }
            readonly property real distance: {
                const revision = timeline.revision;
                const row = timeline.ordinal(index), focus = timeline.ordinal(viewport.focusLine);
                return row < focus ? focus - row + 1 : Math.abs(row - timeline.ordinal(timeline.latestIndex));
            }
            readonly property real lineOpacity: {
                const revision = timeline.revision;
                const passed = viewport.waiting ? timeline.nextRow(timeline.gapAnchor) : viewport.focusLine;
                // Resting ink already carries its opacity, including plain LRC.
                // A second row fade would reduce the 20% default to 4%.
                return viewport.hidePassed && viewport.playback.playing && timeline.ordinal(index) < timeline.ordinal(passed) && lines.following ? 0 : timeline.highlighted(index) ? .85 : 1;
            }
            readonly property string secondaryText: [viewport.playback.lyricText.wantTranslation ? translation : "", viewport.playback.lyricText.wantRomanization && !ruby ? romanization : ""].filter(value => value.length > 0).join("\n")
            width: lines.width
            height: viewport.filterLyrics && filtered ? 0 : textRowHeight + headerHeight
            visible: !viewport.filterLyrics || !filtered
            ListView.onPooled: {
                pooled = true;
                freshPosition = true;
                animatePosition = false;
                scrollMotion.snap(lines.contentY);
                dotMotion.snap(lines.contentY);
                dotsPositioned = false;
                scaleMotion.snap(visual.targetScale);
            }
            ListView.onReused: {
                freshPosition = true;
                animatePosition = false;
                scrollMotion.snap(lines.contentY);
                dotMotion.snap(lines.contentY);
                dotsPositioned = false;
                pooled = false;
                scaleMotion.snap(visual.targetScale);
            }
            Loader {
                active: line.headerHeight > 0 && viewport.resourceActive
                width: line.width
                height: line.headerHeight
                transform: Translate {
                    y: lines.following ? lines.contentY - dotMotion.value : 0
                }
                sourceComponent: Component {
                    Item {
                        objectName: "lyricInterludeRow"
                        InterludeDots {
                            objectName: "lyricInterludeDots"
                            x: viewport.horizontalAlignment === Qt.AlignHCenter ? (parent.width - width) / 2 : viewport.horizontalAlignment === Qt.AlignRight ? parent.width - width : 0
                            y: viewport.fontSize * .4
                            width: implicitWidth
                            height: implicitHeight
                            fontSize: viewport.fontSize
                            pageHeight: viewport.pageHeight
                            animation: interludeAnimation
                            color: viewport.textColor
                            motionEnabled: Theme.store.motionEnabled
                        }
                    }
                }
            }
            Item {
                objectName: "lyricTextRow" + line.index
                y: line.headerHeight
                width: line.width
                height: line.textRowHeight
                transform: Translate {
                    y: lines.following ? lines.contentY - line.scrollY : 0
                }
                Rectangle {
                    objectName: "lyricRowBackground"
                    anchors.fill: parent
                    anchors.leftMargin: 0
                    anchors.rightMargin: viewport.overlayRightReserve
                    radius: viewport.fontSize * .25
                    color: Theme.withAlpha(viewport.textColor, tap.pressed ? .03 : hover.hovered ? .07 : 0)
                }
                Item {
                    id: visual
                    x: viewport.fontSize + lyricPart.value("translateX", 0)
                    y: viewport.fontSize * .4 + lyricPart.value("translateY", 0)
                    width: parent.width - viewport.fontSize * 2
                    height: parent.height - viewport.fontSize * .8
                    opacity: lyricPart.value("opacity", line.lineOpacity)
                    property real targetScale: viewport.scaleEnabled && viewport.playback.playing && !line.active ? 97 : 100
                    scale: lyricPart.value("scale", scaleMotion.value / 100)
                    onTargetScaleChanged: scaleMotion.retargetScale(targetScale, line.inViewport && !line.freshPosition && viewport.resourceActive && viewport.springEnabled && Theme.store.motionEnabled)
                    Component.onCompleted: scaleMotion.snap(targetScale)
                    LyricSpring {
                        id: scaleMotion
                    }
                    transformOrigin: Item.Left
                    property real blurAmount: viewport.softFocus && viewport.appearance.enableBlur && lines.following && !line.active ? Math.min(5, (1 + line.distance) * (viewport.width <= 1024 ? .8 : 1)) : 0
                    // Line activation precedes word timing, allowing the native
                    // transition to clear the incoming row before its first word.
                    Behavior on blurAmount {
                        enabled: line.inViewport && viewport.resourceActive && Theme.store.motionEnabled
                        NumberAnimation {
                            duration: 400
                            easing.type: Easing.BezierSpline
                            easing.bezierCurve: [.25, .1, .25, 1, 1, 1]
                        }
                    }
                    layer.enabled: viewport.resourceActive && line.inViewport && blurAmount > .01
                    layer.effect: MultiEffect {
                        blurEnabled: true
                        blurMax: 12
                        blur: visual.blurAmount / 12
                        autoPaddingEnabled: true
                    }
                    Behavior on opacity {
                        enabled: line.inViewport && viewport.resourceActive && Theme.store.motionEnabled
                        NumberAnimation {
                            duration: 400
                            easing.type: Easing.BezierSpline
                            easing.bezierCurve: [.25, .1, .25, 1, 1, 1]
                        }
                    }
                    LyricLine {
                        id: lyric
                        EchoUiPart {
                            id: lyricPart
                            target: visual
                            role: "lyrics.line"
                            key: viewport.playback.trackId + ":" + String(line.startMs) + ":" + String(line.index)
                            surface: "playback"
                            active: viewport.resourceActive && !line.pooled
                            properties: ["foreground", "secondaryForeground", "font", "fontSize", "fontWeight", "letterSpacing", "opacity", "scale", "translateX", "translateY"]
                            baseValues: observed ? ({
                                    foreground: viewport.textColor,
                                    secondaryForeground: viewport.unplayedColor,
                                    font: Theme.store.styledFont(viewport.baseFont, viewport.fontSize, viewport.textWeight, 0, false, false),
                                    opacity: line.lineOpacity,
                                    scale: scaleMotion.value / 100,
                                    translateX: 0,
                                    translateY: 0
                                }) : ({})
                            state: observed ? ({
                                    scope: "page",
                                    index: line.index,
                                    text: line.lineText,
                                    words: line.words,
                                    glyphs: lyric.visible ? lyric.glyphs : [],
                                    rubyUnits: line.rubyUnits,
                                    timelineMs: viewport.renderTime,
                                    startMs: line.startMs,
                                    durationMs: line.durationMs,
                                    current: line.active,
                                    distance: line.distance,
                                    browsing: !lines.following,
                                    reducedMotion: !Theme.store.motionEnabled
                                }) : ({})
                            actions: ["seek"]
                            onActionRequested: (action, arguments) => {
                                if (action === "seek")
                                    viewport.playback.seek(Math.max(0, Math.round(line.startMs)));
                            }
                        }
                        x: -paintPadding
                        y: -paintPadding
                        width: parent.width + 2 * viewport.fontSize
                        height: implicitHeight
                        onLayoutChanged: if (timeline.ordinal(line.index) === lines.currentIndex)
                            reflow.restart()
                        visible: !line.ruby
                        alignment: viewport.horizontalAlignment
                        text: line.lineText
                        words: line.words
                        startMs: line.startMs - viewport.playback.lyrics.offsetMs
                        durationMs: line.durationMs
                        highlightAmount: line.active ? 1 : 0
                        // Row activation uses the advance schedule; word reveal
                        // remains on the original immutable source timestamps.
                        Behavior on highlightAmount {
                            id: inkTransition
                            enabled: viewport.resourceActive && line.inViewport && !line.freshPosition && Theme.store.motionEnabled
                            NumberAnimation {
                                duration: inkTransition.targetValue > 0 ? 300 : 450
                                easing.type: Easing.BezierSpline
                                easing.bezierCurve: [0, 0, .58, 1, 1, 1]
                            }
                        }
                        current: line.active || highlightAmount > .001
                        timeMs: (line.active || highlightAmount > .001) && line.inViewport && viewport.resourceActive ? viewport.renderTime - viewport.playback.lyrics.offsetMs : -1
                        font: lyricPart.styledFont(Theme.store.styledFont(viewport.baseFont, viewport.fontSize, viewport.textWeight, 0, false, false))
                        playedColor: lyricPart.value("foreground", viewport.textColor)
                        unplayedColor: lyricPart.value("secondaryForeground", viewport.unplayedColor)
                        fadeWidth: viewport.wordFade
                        animateWords: viewport.scaleEnabled && Theme.store.motionEnabled
                    }
                    Loader {
                        id: rubyLine
                        active: line.ruby && !line.pooled && viewport.resourceActive
                        width: parent.width
                        sourceComponent: LyricRubyLine {
                            effectSurface: "playback"
                            alignment: viewport.horizontalAlignment
                            units: line.rubyUnits
                            font: lyricPart.styledFont(Theme.store.styledFont(viewport.baseFont, viewport.fontSize, viewport.textWeight, 0, false, false))
                            timeMs: line.inViewport ? viewport.renderTime - viewport.playback.lyrics.offsetMs : -1
                            current: line.active
                            playedColor: lyricPart.value("foreground", viewport.textColor)
                            unplayedColor: lyricPart.value("secondaryForeground", viewport.unplayedColor)
                            animateWords: Theme.store.motionEnabled
                        }
                    }
                    Label {
                        id: secondary
                        onImplicitHeightChanged: if (timeline.ordinal(line.index) === lines.currentIndex)
                            reflow.restart()
                        y: line.mainHeight + viewport.secondaryGap
                        width: parent.width
                        visible: text.length > 0
                        text: line.secondaryText
                        textFormat: Text.PlainText
                        wrapMode: Text.Wrap
                        lineHeightMode: Text.FixedHeight
                        lineHeight: viewport.secondaryFontSize * 1.5
                        font: Theme.store.styledFont(viewport.baseFont, viewport.secondaryFontSize, Math.min(650, viewport.textWeight), 0, false, false)
                        horizontalAlignment: viewport.horizontalAlignment
                        color: viewport.textColor
                        opacity: .3
                    }
                }
                HoverHandler {
                    id: hover
                }
                TapHandler {
                    id: tap
                    enabled: viewport.playback.lyrics.scrollable && line.lineOpacity > 0
                    acceptedButtons: Qt.LeftButton
                    gesturePolicy: TapHandler.DragThreshold
                    onDoubleTapped: line.jumpToLine()
                }
            }
            // Model row starts include display offsets; the presentation schedule
            // also advances activation. Seek the immutable singing timestamp.
            function jumpToLine(): void {
                if (!viewport.resourceActive || line.pooled || !tap.enabled)
                    return;
                const sourceTime = line.wordTimed && line.words.length > 0 ? line.words[0].start_ms : line.startMs - viewport.playback.lyrics.offsetMs;
                if (!Number.isFinite(sourceTime))
                    return;
                lines.cancelFlick();
                lines.following = true;
                resume.stop();
                viewport.playback.seek(Math.max(0, Math.round(sourceTime)));
                if (!viewport.playback.playing)
                    viewport.playback.play();
                viewport.follow(false);
            }
            Accessible.role: viewport.playback.lyrics.scrollable ? Accessible.Button : Accessible.StaticText
            Accessible.name: line.lineText
            Accessible.onPressAction: line.jumpToLine()
        }
    }
    Connections {
        target: lines.currentItem
        function onYChanged() {
            reconcile.restart();
        }
    }
    Timer {
        id: reconcile
        interval: 0
        onTriggered: viewport.follow(!lines.animateScroll)
    }
    Timer {
        id: reflow
        interval: 0
        onTriggered: viewport.follow(true)
    }
    Timer {
        id: resume
        objectName: "lyricFollowResume"
        interval: 3000
        onTriggered: {
            if (!viewport.playback.lyrics.scrollable)
                return;
            lines.following = true;
            viewport.follow(false);
        }
    }
    Label {
        objectName: "playbackLyricEmptyState"
        anchors.centerIn: parent
        visible: viewport.playback.lyrics.count === 0
        text: viewport.playback.lyricState === "loading" ? qsTr("正在加载歌词…") : viewport.playback.lyricState === "error" ? qsTr("歌词加载失败，点击右侧重新选择") : qsTr("暂无歌词")
        color: viewport.colors.secondaryText
        font: Theme.font(16, Theme.mediumWeight)
    }
}
