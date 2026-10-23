pragma ComponentBehavior: Bound
import QtQuick
import EchoMusic.Theme

// 任务栏歌词单行：逐字歌词按时间着色（已唱=强调色，未唱=次要色），
// 长行按播放进度横向滚动。对应参考实现 EchoMusic
// src/renderer/taskbarPlayer/TaskbarLyricLine.vue。
Item {
    id: root
    property string text: ""
    // 逐字（逐 grapheme）时间片：[{text, start_ms, duration_ms, from, to}]
    property var units: []
    property bool wordByWord: false
    property real timeMs: 0
    property real startMs: 0
    property real endTimeMs: 0
    // Lyric offset: the line times are raw, the clock is in playback time.
    property real timeOffset: 0
    property bool anchorRight: false
    property color accentColor: Theme.accent
    property color baseColor: Theme.text
    property real fontSize: 14
    signal naturalWidth(width: real)

    // Word-by-word only when the data carries REAL per-tranche timings:
    // line.words only exist for char-level sources (YRC/QRC…); a plain LRC
    // line yields synthesized units whose windows just span the whole line
    // (the "估算" sweep EchoMusic guards with hasWordByWordTiming).
    readonly property bool timed: wordByWord && units && units.length > 0 && units[0].wordTimed === true
    // Prefix up to (and including) the character that is currently sung.
    readonly property string sungPrefix: {
        if (!timed)
            return "";
        let prefix = "";
        for (let i = 0; i < units.length; ++i) {
            const unit = units[i];
            if (timeMs < (unit.start_ms + timeOffset + unit.duration_ms))
                return prefix;
            prefix += unit.text;
        }
        return prefix;
    }
    readonly property string sungPrefixWithActive: {
        if (!timed)
            return "";
        let prefix = "";
        for (let i = 0; i < units.length; ++i) {
            const unit = units[i];
            prefix += unit.text;
            if (timeMs < (unit.start_ms + timeOffset + unit.duration_ms))
                return prefix;
        }
        return prefix;
    }
    readonly property real viewportWidth: Math.max(0, width)
    // The natural width of the text. TextMetrics measures the string through
    // generic metrics and lands a couple of percent short of what the real
    // shaping renders (probe: 63.0 measured vs 64.375 rendered), which kept
    // the line tail outside the clip forever. The rendered Text item is the
    // truth; the metrics stay the fallback for the frame it is not laid out.
    readonly property real contentWidth: Math.max(metrics.width, baseText.width)
    readonly property real overflow: Math.max(0, contentWidth - viewportWidth)
    // Width of the already sung part: the prefix up to the active character
    // (plus its played fraction), measured as one string. Non-word-timed
    // lines render plain (the primary reads bright fg, the secondary muted):
    // no invented sweep, mirroring EchoMusic's plain-text branch.
    readonly property real sungWidth: {
        if (!timed)
            return 0;
        let prefix = "";
        let active = null;
        for (let i = 0; i < units.length; ++i) {
            const unit = units[i];
            if (timeMs < (unit.start_ms + timeOffset + unit.duration_ms)) {
                active = unit;
                break;
            }
            prefix += unit.text;
        }
        // The roll freezes the leaving pair right after the line end, so
        // every unit is consumed and active is null: fully sung, both probes
        // then measure the whole text and the ratio reads 1.
        const ratio = !active ? 1 : active.duration_ms > 0 ? Math.min(1, Math.max(0, (timeMs - (active.start_ms + timeOffset)) / active.duration_ms)) : 0;
        const played = playedProbe.width;
        const withActive = playedProbeActive.width;
        // Whole-pixel boundary: the two layers clip at the same device pixel
        // and the unsung copy never re-rasterizes at a new sub pixel phase.
        return Math.round(played + (withActive - played) * ratio);
    }
    readonly property real scrollDistance: {
        if (overflow <= 0)
            return 0;
        // The reference keys the scroll off the character index on word-timed
        // lines and off the playback progress on plain ones; both keep the text
        // readable at either end. This port uses the time formula for both: the
        // word timing granularity is not the character count (one unit can span
        // a latin word), so the index ratio would move the line a whole unit at
        // a time while the clock stays smooth.
        const duration = endTimeMs - startMs;
        if (duration <= 0)
            return 0;
        const scrollDuration = duration - Math.min(2000, duration * 0.25);
        const progress = Math.min(1, Math.max(0, (timeMs - startMs) / scrollDuration));
        return overflow * progress;
    }

    implicitHeight: metrics.height
    clip: true

    // Item has no font property: the children build theirs from these.
    readonly property string fontFamily: Theme.store.taskbarLyricBaseFont.family
    readonly property int fontPixelSize: root.fontSize

    onContentWidthChanged: naturalWidth(contentWidth)
    onTextChanged: naturalWidth(contentWidth)
    onFontPixelSizeChanged: naturalWidth(contentWidth)

    TextMetrics {
        id: metrics
        text: root.text
        font.family: root.fontFamily
        font.pixelSize: root.fontPixelSize
        font.weight: Font.Normal
    }
    // Same-shape probes for the sung prefix: measured by the text engine that
    // renders the line itself. TextMetrics stays a couple percent short of the
    // real shaping, which put the highlight edge visibly inside the wrong
    // character. Hidden - they only report widths.
    Text {
        id: playedProbe
        visible: false
        text: root.sungPrefix
        font.family: root.fontFamily
        font.pixelSize: root.fontPixelSize
        font.weight: Font.Normal
        elide: Text.ElideNone
    }
    Text {
        id: playedProbeActive
        visible: false
        text: root.sungPrefixWithActive
        font.family: root.fontFamily
        font.pixelSize: root.fontPixelSize
        font.weight: Font.Normal
        elide: Text.ElideNone
    }

    Item {
        id: content
        // The reference rests the line flush to its end side (flex-end) and
        // translates by (+overflow) - scrollDistance: short lines end up right
        // aligned, long lines keep their head fully visible on line start and
        // their tail fully visible on the strip edge at the line's end.
        x: (root.anchorRight ? root.viewportWidth - root.contentWidth + root.overflow : 0) - root.scrollDistance
        y: 0
        height: root.height

        // Each pixel is painted by exactly one layer: the accent copy owns
        // [0, sungWidth], the base copy owns the rest. Drawing both on top of
        // each other composited the base colour into the accent glyphs'
        // anti-aliased edge pixels and left a muddy fringe around the
        // highlight (the reference uses a single background-clip:text draw).
        Item {
            id: baseClip
            x: root.sungWidth
            width: root.contentWidth > 0 ? Math.max(0, root.contentWidth - root.sungWidth + 6) : 0
            height: baseText.height
            clip: true
            Text {
                id: baseText
                // Shifted back so the glyphs keep their absolute position.
                x: -root.sungWidth
                text: root.text
                color: root.baseColor
                font.family: root.fontFamily
                font.pixelSize: root.fontPixelSize
                font.weight: Font.Normal
                elide: Text.ElideNone
            }
        }
        Item {
            id: sungLayer
            // Six extra pixels once the line is fully sung cover the last
            // glyph's overhanging anti-aliased tail in accent colour too.
            width: root.sungWidth + (root.sungWidth >= root.contentWidth ? 6 : 0)
            height: baseText.height
            clip: true
            Text {
                text: root.text
                color: root.accentColor
                font.family: root.fontFamily
                font.pixelSize: root.fontPixelSize
                font.weight: Font.Normal
                elide: Text.ElideNone
            }
        }
    }
}
