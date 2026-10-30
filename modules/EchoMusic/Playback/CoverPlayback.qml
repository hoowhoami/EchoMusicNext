pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import EchoMusic.Theme
import EchoMusic.Controls
import EchoMusic.Playback

GridLayout {
    id: body
    required property PlaybackService playback
    required property ThemeColors colors
    required property PlaybackPageMotion pageMotion
    required property bool resourceActive
    required property real pageWidth
    required property real pageHeight
    required property real renderTime
    property real controlsTopReserve: 0
    property real overlayRightReserve: 0
    readonly property alias cover: albumCover
    readonly property bool narrow: body.pageWidth <= 768
    columns: narrow ? 1 : 2
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
            opacity: body.pageMotion.hostOpacity
            artworkOpacity: body.pageMotion.coverFlight ? 0 : 1
            Layout.topMargin: body.narrow ? 16 : 24
            objectName: "playbackPageCover"
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: body.narrow ? 160 : Math.min(380, Math.max(220, parent.width * 0.8))
            Layout.preferredHeight: Layout.preferredWidth
            playback: body.playback
            colors: body.colors
            placeholderColor: body.colors.mutedSurface
            placeholderGlyphSize: 32
            resourceActive: body.resourceActive
            EchoSurfaceShadow {
                objectName: "playbackCoverShadow"
                // One shadow follows the flight and remains in place
                // on landing, without appearing as a new final layer.
                x: body.pageMotion.coverFlight ? body.pageMotion.currentRect.x - body.pageMotion.largeRect.x : 0
                y: body.pageMotion.coverFlight ? body.pageMotion.currentRect.y - body.pageMotion.largeRect.y : 0
                width: parent.width
                height: parent.height
                surface: parent
                z: -1
                opacity: body.pageMotion.coverFlight ? body.pageMotion.cornerBlend : 1
                radius: body.pageMotion.coverFlight ? body.pageMotion.smallRadius + (Theme.mediaRadius - body.pageMotion.smallRadius) * body.pageMotion.cornerBlend : Theme.mediaRadius
                blur: 64
                offset: Qt.vector2d(0, 24)
                color: Qt.rgba(0, 0, 0, 0.3)
                transform: Scale {
                    xScale: body.pageMotion.coverFlight ? body.pageMotion.currentRect.width / Math.max(1, body.pageMotion.largeRect.width) : 1
                    yScale: body.pageMotion.coverFlight ? body.pageMotion.currentRect.height / Math.max(1, body.pageMotion.largeRect.height) : 1
                }
            }
        }
        EchoTextTagRow {
            objectName: "playbackPageTitle"
            // Single-line metadata keeps its final line box even when
            // empty text has no Flow width during the first layout.
            Layout.preferredHeight: lineHeight
            opacity: body.pageMotion.hostOpacity * body.pageMotion.contentOpacity
            transform: Translate {
                y: body.pageMotion.contentOffset
            }
            fillTextWidth: true
            Layout.topMargin: 24
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            Layout.maximumWidth: 380 - 32
            Layout.alignment: Qt.AlignHCenter
            text: body.playback.title
            horizontalAlignment: Text.AlignHCenter
            color: body.colors.text
            lineHeight: 33
            font: Theme.font(22, Theme.boldWeight)
        }
        EchoTextTagRow {
            objectName: "playbackPageArtist"
            Layout.preferredHeight: lineHeight
            opacity: body.pageMotion.hostOpacity * body.pageMotion.contentOpacity
            transform: Translate {
                y: body.pageMotion.contentOffset
            }
            fillTextWidth: true
            Layout.topMargin: 6
            Layout.bottomMargin: body.narrow ? 0 : 24
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            Layout.maximumWidth: 380 - 32
            Layout.alignment: Qt.AlignHCenter
            text: body.playback.artist
            horizontalAlignment: Text.AlignHCenter
            color: body.colors.secondaryText
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
            anchors.bottomMargin: Math.max(96, body.controlsTopReserve + 16)
            opacity: body.pageMotion.hostOpacity * body.pageMotion.contentOpacity
            transform: Translate {
                y: body.pageMotion.contentOffset
            }
            // Destroy the native delegate pool on close/minimize.
            active: body.resourceActive
            sourceComponent: PlaybackLyricViewport {
                objectName: "playbackLyricViewport"
                playback: body.playback
                colors: body.colors
                renderTime: body.renderTime
                resourceActive: body.resourceActive
                pageWidth: body.pageWidth
                pageHeight: body.pageHeight
                overlayRightReserve: body.overlayRightReserve
            }
        }
    }
}
