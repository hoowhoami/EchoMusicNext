pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Shapes
import EchoMusic.Controls
import EchoMusic.Theme
import EchoMusic.Playback

// Mini-only visual exception requested by the user; placement, native lifetime
// and dismissal still belong to the same shared anchored popup as other menus.
EchoAnchoredPopup {
    id: popup
    required property PlaybackService playback
    property real contentScale: 1
    readonly property real logicalWidth: width / contentScale
    readonly property real bodyHeight: 48
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
    preferredWidth: 176 * contentScale
    implicitHeight: (bodyHeight + tipHeight) * contentScale
    // Flipping swaps top/bottom padding. The fixed single-row height must not
    // depend on those paddings through the shared scrolling content limit.
    height: implicitHeight
    preferAbove: true
    contentAlign: "center"
    sideOffset: 0
    leftPadding: 12 * contentScale
    rightPadding: 16 * contentScale
    topPadding: (placedAbove ? 8 : 8 + tipHeight) * contentScale
    bottomPadding: (placedAbove ? 8 + tipHeight : 8) * contentScale
    // Reserve native window pixels for the bounded shadow without expanding the
    // host Mini window or adding a texture-backed blur of its contents.
    leftInset: -12 * contentScale
    rightInset: -12 * contentScale
    topInset: -12 * contentScale
    bottomInset: -12 * contentScale
    contentItem: Item {
        RowLayout {
            width: parent.width / popup.contentScale
            height: parent.height / popup.contentScale
            scale: popup.contentScale
            transformOrigin: Item.TopLeft
            spacing: 8
            EchoIconButton {
                objectName: "playbackVolumeMute"
                colors: Theme.floating
                Layout.preferredWidth: 24
                Layout.preferredHeight: 24
                padding: 4
                glyph: popup.playback.volume > 0 ? "volume" : "volume-off"
                text: popup.playback.volume > 0 ? qsTr("静音") : qsTr("取消静音")
                onClicked: popup.playback.toggleMute()
            }
            EchoSlider {
                id: slider
                objectName: "playbackVolumeSlider"
                colors: Theme.floating
                Layout.fillWidth: true
                Layout.fillHeight: true
                padding: 0
                from: 0
                to: 100
                stepSize: 1
                value: popup.playback.volume
                onMoved: popup.playback.setVolume(value)
                Accessible.name: qsTr("音量")
                handle: Rectangle {
                    x: slider.visualPosition * (slider.availableWidth - width)
                    y: slider.availableHeight / 2 - height / 2
                    width: 10
                    height: 10
                    radius: 5
                    color: slider.colors.accent
                    border.width: slider.visualFocus ? 1 : 0
                    border.color: slider.colors.text
                }
            }
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
                    strokeColor: Theme.floating.subtleBorder
                    strokeWidth: .5
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
