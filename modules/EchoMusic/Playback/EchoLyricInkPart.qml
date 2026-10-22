pragma ComponentBehavior: Bound
import QtQuick
import EchoMusic.Controls
import EchoMusic.Playback

EchoUiPart {
    required property LyricLine renderer
    property string runKey: "glyph-run"
    target: renderer
    role: "lyrics.run"
    key: runKey
    state: observed ? ({
            text: renderer.text,
            words: renderer.words,
            glyphs: renderer.glyphs,
            timelineMs: renderer.timeMs,
            startMs: renderer.startMs,
            durationMs: renderer.durationMs,
            current: renderer.current,
            font: renderer.font,
            playedColor: renderer.playedColor,
            unplayedColor: renderer.unplayedColor
        }) : ({})
}
