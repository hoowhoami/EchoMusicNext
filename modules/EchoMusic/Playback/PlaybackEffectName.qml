pragma ComponentBehavior: Bound
import QtQuick
import EchoMusic.Controls
import EchoMusic.Theme

// All effect lists share bounded names and measure only their visible glyphs.
EchoTextTagRow {
    font: Theme.font(12, Theme.semiboldWeight)
    lineHeight: font.pixelSize * 1.25
    wrap: true
    maximumLineCount: 2
}
