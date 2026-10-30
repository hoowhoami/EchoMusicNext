pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Effects
import EchoMusic.Theme
import EchoMusic.Controls
import EchoMusic.Playback

Item {
    id: content
    property string effectHostId: ""
    property real followOffset: -1
    signal followRequested(real position, real maximum)
    required property PlaybackService playback
    property bool resourceActive: true
    property string shadowStrength: "normal"
    property real arrival: 0
    property real baseOpacity: 1
    property bool departing: false
    property bool animateChange: false
    property real initialHighlight: 1
    property real initialBlur: 0
    property real initialScale: 1
    property bool initialized: false
    readonly property real highlightAmount: motion.highlight
    readonly property real blurAmount: motion.blur
    readonly property real presentationScale: motion.scale
    function settleMotion(): void {
        motion.settle();
    }
    onLineChanged: if (initialized) {
        if (animateChange && !departing)
            motion.startFrom(0, 1.6, .97);
        else
            motion.settle();
    }
    Component.onCompleted: {
        initialized = true;
        if (departing)
            motion.startFrom(initialHighlight, initialBlur, initialScale);
    }
    LyricLineMotion {
        id: motion
        objectName: "appleMusicLineMotion"
        activeLine: !content.departing
        resourceActive: content.resourceActive
        animationEnabled: content.playback.playing
        shrinkInactive: true
        blurInactive: true
    }
    property int textAlignment: Qt.AlignLeft
    property bool vertical: false
    property real fontSize: 40
    property font baseFont: Theme.store.desktopLyricBaseFont
    readonly property font textFont: Theme.store.styledFont(baseFont, fontSize, bold ? 700 : 400, 0, false, false)
    property var line: ({})
    property var nextLine: ({})
    property var rubyUnits: []
    property var verticalUnits: []
    property var nextVerticalUnits: []
    property bool ruby: false
    property bool preview: false
    property bool bold: false
    property real renderTime: 0
    property color playedColor: "#31cfa1"
    property color unplayedColor: "#7a7a7a"
    property string secondary: ""
    property string alignment: "left"
    property real contentInset: 16
    property bool sourceSpacing: false
    // Desktop secondary lines use the configured resting ink, independently
    // of the primary line's sung color and its word-by-word progress.
    readonly property color secondaryColor: linePart.value("secondaryForeground", content.unplayedColor)
    readonly property real secondaryOpacity: sourceSpacing ? 1 : .72

    anchors.fill: parent
    anchors.margins: Math.min(contentInset, parent ? parent.height * .12 : contentInset)

    EchoUiPart {
        id: linePart
        target: content
        role: "lyrics.line"
        key: content.playback.trackId + ":" + String(content.line.startMs || 0)
        surface: "desktop"
        active: content.resourceActive && content.effectHostId !== ""
        properties: ["foreground", "secondaryForeground", "font", "fontSize", "fontWeight", "letterSpacing", "opacity", "scale", "translateX", "translateY"]
        baseValues: observed ? ({
                foreground: content.playedColor,
                secondaryForeground: content.unplayedColor,
                font: content.textFont,
                opacity: 1,
                scale: motion.scale,
                translateX: content.vertical ? content.arrival * content.fontSize * 1.24 : 0,
                translateY: content.vertical ? 0 : content.arrival * content.fontSize * 1.24
            }) : ({})
        state: observed ? ({
                scope: "desktop",
                text: content.line.lineText || content.playback.title,
                words: content.line.words || [],
                rubyUnits: content.rubyUnits,
                verticalUnits: content.verticalUnits,
                timelineMs: content.renderTime,
                startMs: content.line.startMs || 0,
                durationMs: content.line.durationMs || 0,
                current: true,
                reducedMotion: !Theme.store.motionEnabled,
                glyphs: main.item && main.item.glyphSnapshot !== undefined ? main.item.glyphSnapshot : []
            }) : ({})
    }
    opacity: linePart.value("opacity", 1) * baseOpacity
    scale: linePart.value("scale", motion.scale)
    transformOrigin: content.textAlignment === Qt.AlignRight ? Item.Right : content.textAlignment === Qt.AlignHCenter ? Item.Center : Item.Left
    transform: Translate {
        x: linePart.value("translateX", content.vertical ? content.arrival * content.fontSize * 1.24 : 0)
        y: linePart.value("translateY", content.vertical ? 0 : content.arrival * content.fontSize * 1.24)
    }
    layer.enabled: content.resourceActive && (content.shadowStrength !== "none" || motion.blur > .01)
    layer.effect: MultiEffect {
        blurEnabled: motion.blur > .01
        blurMax: 12
        blur: motion.blur / 12
        shadowEnabled: content.shadowStrength !== "none"
        shadowColor: "#000000"
        shadowOpacity: content.shadowStrength === "soft" ? .4 : content.shadowStrength === "strong" ? .95 : .7
        shadowBlur: content.shadowStrength === "soft" ? .7 : .35
        shadowVerticalOffset: content.shadowStrength === "strong" ? 2 : 1
        autoPaddingEnabled: true
    }
    Connections {
        target: content.playback
        function onTimelineReset(): void {
            if (!main.item)
                return;
            if (content.vertical)
                (main.item as LyricVerticalLine).resetFollow();
            else if (content.ruby)
                (main.item as LyricRubyLine).syncFocus();
        }
    }
    Loader {
        id: main
        x: content.vertical ? parent.width - width : 0
        width: content.vertical && (content.preview || content.secondary.length > 0) ? parent.width / 2 - 6 : parent.width
        height: content.vertical ? parent.height : implicitHeight
        sourceComponent: content.vertical ? verticalText : content.ruby ? rubyText : horizontalText
    }
    Component {
        id: horizontalText
        Item {
            id: horizontalViewport
            readonly property var glyphSnapshot: mainInk.glyphs
            implicitHeight: mainInk.textHeight
            clip: true
            readonly property real defaultScrollOffset: Math.max(0, Math.min(mainInk.textWidth - width, mainInk.readingPosition(content.renderTime - content.playback.lyrics.offsetMs) - content.textFont.pixelSize - width * .6))
            readonly property real scrollOffset: content.followOffset >= 0 ? content.followOffset : defaultScrollOffset
            onDefaultScrollOffsetChanged: if (content.effectHostId !== "")
                content.followRequested(defaultScrollOffset, Math.max(0, mainInk.textWidth - width))
            LyricLine {
                id: mainInk
                objectName: "desktopLyricInk"
                EchoLyricInkPart {
                    runKey: content.playback.trackId + ":" + String(content.line.startMs || 0)
                    renderer: mainInk
                    surface: "desktop"
                    active: content.effectHostId !== "" && content.resourceActive
                }
                x: -content.textFont.pixelSize - horizontalViewport.scrollOffset
                wrap: false
                y: -content.textFont.pixelSize
                width: parent.width + 2 * content.textFont.pixelSize
                height: implicitHeight
                text: content.line.lineText || content.playback.title
                words: content.line.words || []
                startMs: (content.line.startMs || 0) - content.playback.lyrics.offsetMs
                durationMs: content.line.durationMs || 0
                current: content.line.lineText !== undefined && (!content.departing || motion.highlight > .001)
                highlightAmount: motion.highlight
                timeMs: content.renderTime - content.playback.lyrics.offsetMs
                font: linePart.styledFont(content.textFont)
                playedColor: linePart.value("foreground", content.playedColor)
                unplayedColor: content.line.lineText === undefined ? content.playedColor : content.unplayedColor
                alignment: content.textAlignment
                animateWords: content.resourceActive && content.playback.playing && Theme.store.motionEnabled
            }
        }
    }
    Component {
        id: rubyText
        LyricRubyLine {
            effectSurface: content.effectHostId !== "" ? "desktop" : ""
            followOverride: content.followOffset
            onFollowRequested: (position, maximum) => content.followRequested(position, maximum)
            wrap: false
            alignment: content.textAlignment
            units: content.rubyUnits
            font: linePart.styledFont(content.textFont)
            timeMs: content.renderTime - content.playback.lyrics.offsetMs
            current: !content.departing || motion.highlight > .001
            highlightAmount: motion.highlight
            playedColor: linePart.value("foreground", content.playedColor)
            unplayedColor: linePart.value("secondaryForeground", content.unplayedColor)
            animateWords: content.resourceActive && content.playback.playing && Theme.store.motionEnabled
        }
    }
    Component {
        id: verticalText
        LyricVerticalLine {
            objectName: "desktopVerticalLyricInk"
            effectSurface: content.effectHostId !== "" ? "desktop" : ""
            followOverride: content.followOffset
            onFollowRequested: (position, maximum) => content.followRequested(position, maximum)
            alignment: content.textAlignment === Qt.AlignRight ? Qt.AlignBottom : content.textAlignment === Qt.AlignHCenter ? Qt.AlignVCenter : Qt.AlignTop
            units: content.line.lineText !== undefined ? content.verticalUnits : content.playback.lyrics.verticalText(content.playback.title)
            animateWords: content.resourceActive && content.playback.playing && Theme.store.motionEnabled
            font: linePart.styledFont(content.textFont)
            timeMs: content.renderTime - content.playback.lyrics.offsetMs
            current: !content.departing || motion.highlight > .001
            highlightAmount: motion.highlight
            playedColor: linePart.value("foreground", content.playedColor)
            unplayedColor: content.line.lineText !== undefined ? content.unplayedColor : content.playedColor
        }
    }
    Loader {
        active: content.vertical && content.preview
        x: 0
        width: parent.width / 2
        height: parent.height
        sourceComponent: LyricVerticalLine {
            alignment: content.textAlignment === Qt.AlignLeft && content.alignment === "both" ? Qt.AlignBottom : Qt.AlignTop
            units: content.nextVerticalUnits
            font: Theme.store.styledFont(content.baseFont, content.fontSize * .8, content.bold ? 700 : 400, 0, false, false)
            playedColor: linePart.value("foreground", content.playedColor)
            unplayedColor: linePart.value("secondaryForeground", content.unplayedColor)
        }
    }
    Column {
        y: content.sourceSpacing ? Math.max(main.height, content.fontSize * 1.24) + 8 + Math.min(8, Math.max(3, Math.round(content.fontSize * .1))) : main.height + content.fontSize * .3
        width: parent.width
        visible: !content.vertical && (content.secondary.length > 0 || content.preview || content.line.lineText === undefined)
        Repeater {
            model: content.secondary.length ? content.secondary.split("\n") : content.preview ? [content.nextLine.lineText || ""] : [content.playback.artist]
            delegate: Item {
                id: secondaryRow
                required property string modelData
                width: parent.width
                height: content.sourceSpacing ? content.fontSize * (content.preview ? .8 : .72) * 1.24 + 8 + Math.min(8, Math.max(3, Math.round(content.fontSize * .1))) : secondaryText.implicitHeight
                clip: true
                readonly property real progress: content.line.durationMs > 0 ? Math.max(0, Math.min(1, (content.renderTime - content.line.startMs) / content.line.durationMs)) : 0
                Label {
                    id: secondaryText
                    objectName: "desktopLyricSecondaryText"
                    x: content.preview ? 0 : -Math.max(0, implicitWidth - secondaryRow.width) * secondaryRow.progress
                    width: content.preview ? secondaryRow.width : Math.max(secondaryRow.width, implicitWidth)
                    horizontalAlignment: content.preview && content.alignment === "both" ? (content.textAlignment === Qt.AlignLeft ? Qt.AlignRight : Qt.AlignLeft) : content.textAlignment
                    text: secondaryRow.modelData
                    textFormat: Text.PlainText
                    wrapMode: Text.NoWrap
                    elide: content.preview ? Text.ElideRight : Text.ElideNone
                    font: Theme.store.styledFont(content.baseFont, content.fontSize * (content.preview ? .8 : content.sourceSpacing ? .72 : .64), content.bold ? 700 : 400, 0, false, false)
                    color: content.secondary.length > 0 || content.preview ? content.secondaryColor : content.playedColor
                    opacity: content.secondaryOpacity
                }
            }
        }
    }
    Loader {
        active: content.vertical && content.secondary.length > 0
        x: 0
        width: parent.width / 2 - 6
        height: parent.height
        sourceComponent: LyricVerticalLine {
            objectName: "desktopVerticalSecondaryInk"
            units: content.playback.lyrics.verticalText(content.secondary)
            font: Theme.store.styledFont(content.baseFont, content.fontSize * .64, content.bold ? 700 : 400, 0, false, false)
            alignment: content.textAlignment === Qt.AlignRight ? Qt.AlignBottom : content.textAlignment === Qt.AlignHCenter ? Qt.AlignVCenter : Qt.AlignTop
            playedColor: content.secondaryColor
            unplayedColor: content.secondaryColor
            opacity: content.secondaryOpacity
        }
    }
}
