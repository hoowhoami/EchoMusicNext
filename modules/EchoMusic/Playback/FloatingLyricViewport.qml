pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Effects
import EchoMusic.Theme
import EchoMusic.Controls
import EchoMusic.Playback

Item {
    id: root
    property real effectFollowOffset: -1
    property real effectFollowMaximum: 0
    function requestEffectFollow(position: real, maximum: real): void {
        effectFollowMaximum = maximum;
        if (!EchoLyricEffectRegistry.hasHandlers("desktop") || !EchoLyricEffectRegistry.requestScroll(effectHost.identity, {
            scope: "desktop",
            index: currentIndex,
            position: position,
            maximum: maximum,
            smooth: false,
            browsing: false,
            reducedMotion: !Theme.store.motionEnabled
        }))
            effectFollowOffset = -1;
    }
    Connections {
        target: EchoLyricEffectRegistry
        function onChanged() {
            root.effectFollowOffset = -1;
        }
    }
    EchoUiPart {
        id: effectHost
        target: root
        role: "lyrics.viewport"
        key: "desktop-lyrics"
        surface: "desktop"
        active: root.resourceActive
        actions: ["scrollTo", "resumeFollow"]
        onActionRequested: (action, args) => {
            if (action === "resumeFollow")
                root.effectFollowOffset = -1;
            else if (action === "scrollTo" && Number.isFinite(args.position))
                root.effectFollowOffset = Math.max(0, Math.min(root.effectFollowMaximum, args.position));
        }
        state: observed ? ({
                scope: "desktop",
                timelineMs: root.renderTime,
                positionMs: root.playback.positionMs,
                durationMs: root.playback.durationMs,
                playbackRate: root.playback.speed,
                playing: root.playback.playing,
                appearance: {
                    font: root.baseFont,
                    playedColor: root.playedColor,
                    unplayedColor: root.unplayedColor
                },
                currentIndex: root.currentIndex,
                scrollIndex: root.currentIndex,
                line: root.line,
                nextLine: root.nextLine,
                lineCount: root.playback.lyrics.count,
                following: true,
                browsing: false,
                reducedMotion: !Theme.store.motionEnabled,
                vertical: root.vertical,
                offsetMs: root.playback.lyrics.offsetMs
            }) : ({})
    }
    required property PlaybackService playback
    property font baseFont: Theme.store.desktopLyricBaseFont
    property bool resourceActive: visible
    property size windowSize: Qt.size(width, height)
    property bool vertical: false
    property string alignment: "both"
    property bool nextLinePreview: true
    property bool bold: false
    property real contentInset: 16
    property bool sourceSpacing: false
    property string shadowStrength: "normal"
    property color playedColor: "#31cfa1"
    property color unplayedColor: "#7a7a7a"
    property real renderTime: playback ? playback.positionMs : 0
    property bool initialized: false
    property bool animateChange: false
    property int currentIndex: -1
    property var line: ({})
    property int nextIndex: -1
    property var nextLine: ({})
    property var rubyUnits: []
    property var verticalUnits: []
    property var nextVerticalUnits: []
    // Only refresh the bounded presentation slice when its row/model changes.
    // Frame samples update uniforms; they do not allocate new text/timing groups.
    function syncLines(force: bool): void {
        if (!playback)
            return;
        const index = playback.lyrics.scrollable ? playback.lyrics.visibleIndex(playback.lyrics.indexAt(renderTime)) : playback.lyrics.nextVisible(-1);
        if (!force && index === currentIndex)
            return;
        animateChange = initialized && index >= 0 && index !== currentIndex && Math.abs(index - currentIndex) === 1 && currentIndex >= 0 && resourceActive && playback.playing && Theme.store.motionEnabled;
        if (animateChange) {
            previous = {
                line: line,
                nextLine: nextLine,
                rubyUnits: rubyUnits,
                verticalUnits: verticalUnits,
                nextVerticalUnits: nextVerticalUnits,
                ruby: ruby,
                preview: preview,
                secondary: secondary,
                fontSize: fontSize,
                textAlignment: textAlignment,
                renderTime: renderTime
            };
        }
        if (!animateChange)
            arrivalMotion.snap(0);
        if (index < 0)
            previous = ({});
        effectFollowOffset = -1;
        currentIndex = index;
        line = playback.lyrics.lineAt(index);
        nextIndex = playback.lyrics.nextVisible(index);
        nextLine = playback.lyrics.lineAt(nextIndex);
        rubyUnits = playback.lyrics.rubyAt(index);
        verticalUnits = playback.lyrics.verticalAt(index);
        nextVerticalUnits = playback.lyrics.verticalAt(nextIndex);
    }
    onRenderTimeChanged: syncLines(false)
    readonly property string secondary: currentIndex < 0 ? playback.artist : [playback.lyricText.wantTranslation ? line.translation || "" : "", playback.lyricText.wantRomanization && !ruby ? line.romanization || "" : ""].filter(value => value.length).join("\n")
    readonly property bool ruby: line.wordTimed === true && !vertical && playback.lyricText.wantRomanization && playback.lyricText.romanizationAsRuby && rubyUnits.length > 0
    readonly property bool preview: secondary.length === 0 && !ruby && nextLinePreview && nextIndex >= 0
    readonly property int visualLineCount: 1 + (ruby ? 1 : 0) + (secondary.length ? secondary.split("\n").length : preview ? 1 : 0)
    // EchoMusic scales horizontal text by window height, vertical text by width.
    // This is deliberately one-way: text metrics never resize the native window.
    readonly property real fontSize: {
        const extent = vertical ? windowSize.width : windowSize.height;
        const minimum = vertical ? 120 : 140;
        const maximum = vertical ? 520 : 360;
        const largest = vertical ? 64 : 96;
        const progress = Math.max(0, Math.min(1, (extent - minimum) / (maximum - minimum)));
        const base = Math.round(20 + progress * (largest - 20));
        return !vertical && visualLineCount >= 3 ? Math.max(16, Math.round(base * .7)) : base;
    }
    readonly property int textAlignment: alignment === "center" ? Qt.AlignHCenter : alignment === "right" || alignment === "both" && currentIndex % 2 === 1 ? Qt.AlignRight : Qt.AlignLeft
    LyricClock {
        id: clock
    }
    function observe(reset: bool): void {
        if (!playback)
            return;
        clock.observe(playback.positionMs, playback.playing, playback.speed, reset);
        renderTime = clock.sample();
    }
    Component.onCompleted: {
        observe(true);
        syncLines(true);
        initialized = true;
    }
    onResourceActiveChanged: {
        observe(true);
        if (!resourceActive) {
            arrivalMotion.snap(0);
        }
    }
    Connections {
        target: root.playback
        function onPositionChanged(): void {
            root.observe(false);
        }
        function onChanged(): void {
            clock.setPlaybackState(root.playback.playing, root.playback.speed);
            root.renderTime = clock.sample();
            if (!root.playback.playing) {
                arrivalMotion.snap(0);
            }
        }
        function onTimelineReset(): void {
            root.observe(true);
            arrivalMotion.snap(0);
        }
    }
    Connections {
        target: root.playback.lyrics
        function onChanged(): void {
            root.syncLines(true);
        }
    }
    Connections {
        target: Theme.store
        function onChanged(): void {
            if (!Theme.store.motionEnabled) {
                arrivalMotion.snap(0);
            }
        }
    }
    FrameAnimation {
        running: root.resourceActive && (root.playback.playing || arrivalMotion.running)
        onTriggered: {
            if (root.playback.playing)
                root.renderTime = clock.sample();
            arrivalMotion.advance();
        }
    }
    readonly property real arrival: arrivalMotion.value
    onCurrentIndexChanged: {
        if (animateChange) {
            arrivalMotion.shift(1);
            arrivalMotion.retarget(0, 0, 0, true);
        }
    }
    LyricSpring {
        id: arrivalMotion
    }
    property var previous: ({})
    Loader {
        anchors.fill: parent
        active: arrivalMotion.running && root.resourceActive && root.previous.line !== undefined
        opacity: Math.max(0, Math.min(1, root.arrival))
        transform: Translate {
            y: -7 * (1 - root.arrival)
        }
        sourceComponent: FloatingLyricContent {
            baseFont: root.baseFont
            playback: root.playback
            resourceActive: root.resourceActive
            line: root.previous.line || ({})
            nextLine: root.previous.nextLine || ({})
            rubyUnits: root.previous.rubyUnits || []
            verticalUnits: root.previous.verticalUnits || []
            nextVerticalUnits: root.previous.nextVerticalUnits || []
            ruby: root.previous.ruby || false
            preview: root.previous.preview || false
            secondary: root.previous.secondary || ""
            fontSize: root.previous.fontSize || 40
            textAlignment: root.previous.textAlignment || Qt.AlignLeft
            renderTime: root.previous.renderTime || 0
            vertical: root.vertical
            bold: root.bold
            shadowStrength: root.shadowStrength
            contentInset: root.contentInset
            sourceSpacing: root.sourceSpacing
            alignment: root.alignment
            playedColor: root.playedColor
            unplayedColor: root.unplayedColor
        }
    }
    FloatingLyricContent {
        effectHostId: effectHost.identity
        followOffset: root.effectFollowOffset
        onFollowRequested: (position, maximum) => root.requestEffectFollow(position, maximum)
        baseFont: root.baseFont
        anchors.fill: parent
        playback: root.playback
        resourceActive: root.resourceActive
        baseOpacity: Math.max(0, Math.min(1, 1 - root.arrival))
        arrival: root.arrival
        line: root.line
        nextLine: root.nextLine
        rubyUnits: root.rubyUnits
        verticalUnits: root.verticalUnits
        nextVerticalUnits: root.nextVerticalUnits
        ruby: root.ruby
        preview: root.preview
        secondary: root.secondary
        fontSize: root.fontSize
        textAlignment: root.textAlignment
        renderTime: root.renderTime
        vertical: root.vertical
        bold: root.bold
        shadowStrength: root.shadowStrength
        contentInset: root.contentInset
        sourceSpacing: root.sourceSpacing
        alignment: root.alignment
        playedColor: root.playedColor
        unplayedColor: root.unplayedColor
    }
}
