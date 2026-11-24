pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Shapes
import EchoMusic.Controls
import EchoMusic.Theme
import EchoMusic.Playback

// The native Mini wrapper keeps its soft pointer and screen-bounded placement;
// its volume content is shared with the main-window surfaces.
EchoAnchoredPopup {
    id: popup
    required property PlaybackService playback
    property real contentScale: 1
    readonly property real logicalWidth: width / contentScale
    readonly property real naturalBodyHeight: volumePanel.implicitHeight + 32
    readonly property real bodyHeight: Math.max(0, height / contentScale - tipHeight)
    readonly property real tipHeight: 9
    readonly property real tipX: Math.max(28, Math.min((anchorRect.x + anchorRect.width / 2 - x) / contentScale, logicalWidth - 28))
    popupType: Popup.Window
    // An application-modal window blocks the Mini underneath and can let macOS
    // hand activation to another app while the pinned Mini is nonactivating.
    // Shared popup grabs still close this surface on outside presses.
    modal: false
    screenBounded: true
    // Popup.Window uses actual width/height for its native bounds. Scale the
    // bounded content inside those bounds instead of clipping a Popup.scale.
    preferredWidth: 320 * contentScale
    // Native popup windows have integer pixel bounds; round up so fractional
    // Mini scaling cannot create a subpixel overflow and an unnecessary scrollbar.
    implicitHeight: Math.ceil((naturalBodyHeight + tipHeight) * contentScale)
    // Keep natural content height independent of placement and its flipped
    // paddings; a short screen scrolls the same production panel.
    height: Math.min(implicitHeight, placementHeight)
    preferAbove: true
    contentAlign: "center"
    sideOffset: 0
    leftPadding: 20 * contentScale
    rightPadding: 20 * contentScale
    topPadding: (placedAbove ? 16 : 16 + tipHeight) * contentScale
    bottomPadding: (placedAbove ? 16 + tipHeight : 16) * contentScale
    // Reserve native window pixels for the bounded shadow without expanding the
    // host Mini window or adding a texture-backed blur of its contents.
    leftInset: -12 * contentScale
    rightInset: -12 * contentScale
    topInset: -12 * contentScale
    bottomInset: -12 * contentScale
    contentItem: Flickable {
        id: viewport
        clip: true
        contentWidth: width
        contentHeight: volumePanel.implicitHeight * popup.contentScale
        boundsBehavior: Flickable.StopAtBounds
        flickableDirection: Flickable.VerticalFlick
        ScrollBar.vertical: EchoPopupScrollBar {
            surface: popup
            view: viewport
            resourceActive: popup.visible
        }
        PlaybackVolumePanel {
            id: volumePanel
            width: viewport.width / popup.contentScale
            height: implicitHeight
            scale: popup.contentScale
            transformOrigin: Item.TopLeft
            playback: popup.playback
        }
    }
    background: Item {
        Item {
            x: 12 * popup.contentScale
            y: 12 * popup.contentScale
            width: popup.logicalWidth
            height: popup.bodyHeight + popup.tipHeight
            scale: popup.contentScale
            transformOrigin: Item.TopLeft
            Item {
                id: body
                x: 0
                y: popup.placedAbove ? 0 : popup.tipHeight
                width: popup.logicalWidth
                height: popup.bodyHeight
                EchoSurfaceShadow {
                    anchors.fill: parent
                    surface: body
                    radius: 14
                    blur: 16
                    offset: Qt.vector2d(0, 3)
                    color: Qt.rgba(0, 0, 0, Theme.dark ? .35 : .2)
                }
            }
            Shape {
                x: 0
                y: 0
                width: popup.logicalWidth
                height: popup.bodyHeight + popup.tipHeight
                preferredRendererType: Shape.CurveRenderer
                transform: Scale {
                    origin.y: (popup.bodyHeight + popup.tipHeight) / 2
                    yScale: popup.placedAbove ? 1 : -1
                }
                ShapePath {
                    fillColor: Theme.floating.elevated
                    strokeColor: "transparent"
                    strokeWidth: 0
                    startX: 14
                    startY: 0
                    PathLine {
                        x: popup.logicalWidth - 14
                        y: 0
                    }
                    PathQuad {
                        x: popup.logicalWidth
                        y: 14
                        controlX: popup.logicalWidth
                        controlY: 0
                    }
                    PathLine {
                        x: popup.logicalWidth
                        y: popup.bodyHeight - 14
                    }
                    PathQuad {
                        x: popup.logicalWidth - 14
                        y: popup.bodyHeight
                        controlX: popup.logicalWidth
                        controlY: popup.bodyHeight
                    }
                    PathLine {
                        x: popup.tipX + 14
                        y: popup.bodyHeight
                    }
                    PathCubic {
                        x: popup.tipX
                        y: popup.bodyHeight + popup.tipHeight
                        control1X: popup.tipX + 7
                        control1Y: popup.bodyHeight
                        control2X: popup.tipX + 7
                        control2Y: popup.bodyHeight + popup.tipHeight
                    }
                    PathCubic {
                        x: popup.tipX - 14
                        y: popup.bodyHeight
                        control1X: popup.tipX - 7
                        control1Y: popup.bodyHeight + popup.tipHeight
                        control2X: popup.tipX - 7
                        control2Y: popup.bodyHeight
                    }
                    PathLine {
                        x: 14
                        y: popup.bodyHeight
                    }
                    PathQuad {
                        x: 0
                        y: popup.bodyHeight - 14
                        controlX: 0
                        controlY: popup.bodyHeight
                    }
                    PathLine {
                        x: 0
                        y: 14
                    }
                    PathQuad {
                        x: 14
                        y: 0
                        controlX: 0
                        controlY: 0
                    }
                }
            }
        }
    }
}
