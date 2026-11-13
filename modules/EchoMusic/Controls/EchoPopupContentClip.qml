pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Effects
import QtQuick.Templates as T

// A background radius does not clip a Control's contentItem. Capture on the
// device-pixel grid and mask only content reaching the rounded frame/border.
// Padded Dialog/Menu/Select content already inside the frame needs no capture.
QtObject {
    id: clipping
    objectName: "popupContentClip"
    required property T.Popup surface
    // Popup's background/contentItem are deferred. Reading them while the
    // derived recipe is still constructing can instantiate the style default
    // before its replacement. Wait for show, after the full recipe is ready.
    readonly property Item content: surface.visible ? surface.contentItem : null
    readonly property EchoFloatingSurface frame: surface.visible ? surface.background as EchoFloatingSurface : null
    readonly property real inset: frame ? frame.border.width : 0
    readonly property rect frameRect: frame && content ? Qt.rect(frame.x - content.x + inset, frame.y - content.y + inset, Math.max(0, frame.width - inset * 2), Math.max(0, frame.height - inset * 2)) : Qt.rect(0, 0, 0, 0)
    readonly property real radius: frame ? Math.min(Math.max(0, (frame.dialogShadow ? frame.paintedRadius : frame.radius) - inset), frameRect.width / 2, frameRect.height / 2) : 0
    readonly property rect paintedRect: Qt.rect(0, 0, content ? content.width : 0, content ? content.height : 0)
    function contains(x: real, y: real): bool {
        const r = frameRect;
        if (x < r.x || y < r.y || x > r.x + r.width || y > r.y + r.height)
            return false;
        const cx = Math.max(r.x + radius, Math.min(x, r.x + r.width - radius));
        const cy = Math.max(r.y + radius, Math.min(y, r.y + r.height - radius));
        return (x - cx) * (x - cx) + (y - cy) * (y - cy) <= radius * radius;
    }
    readonly property bool needed: frame !== null && content !== null && paintedRect.width > 0 && paintedRect.height > 0 && (!contains(paintedRect.x, paintedRect.y) || !contains(paintedRect.x + paintedRect.width, paintedRect.y) || !contains(paintedRect.x, paintedRect.y + paintedRect.height) || !contains(paintedRect.x + paintedRect.width, paintedRect.y + paintedRect.height))
    readonly property bool active: needed && frame.resourceActive && frame.shaderEffectsSupported
    property EchoPopupAnchor geometry: EchoPopupAnchor {
        item: clipping.active ? clipping.content : null
        coordinateItem: clipping.active && clipping.content && clipping.content.Window.window ? clipping.content.Window.window.contentItem : null
    }
    readonly property real dpr: content ? content.Screen.devicePixelRatio : 1
    readonly property real scaleX: geometry.available && geometry.rect.width > 0 && paintedRect.width > 0 ? geometry.rect.width / paintedRect.width : 1
    readonly property real scaleY: geometry.available && geometry.rect.height > 0 && paintedRect.height > 0 ? geometry.rect.height / paintedRect.height : 1
    // Align BOTH ends of the capture with the window's device-pixel grid.
    // Merely rounding textureSize still stretches a fractional extent and
    // loses the direct-rendered text's subpixel origin during compositing.
    readonly property real pixelLeft: Math.floor(geometry.rect.x * dpr + 0.000001)
    readonly property real pixelTop: Math.floor(geometry.rect.y * dpr + 0.000001)
    readonly property size textureSize: Qt.size(Math.max(1, Math.ceil((geometry.rect.x + geometry.rect.width) * dpr - 0.000001) - pixelLeft), Math.max(1, Math.ceil((geometry.rect.y + geometry.rect.height) * dpr - 0.000001) - pixelTop))
    readonly property rect captureRect: Qt.rect((pixelLeft / dpr - geometry.rect.x) / scaleX, (pixelTop / dpr - geometry.rect.y) / scaleY, textureSize.width / (scaleX * dpr), textureSize.height / (scaleY * dpr))
    property Loader renderOwner: Loader {
        // Keep the replacement a sibling, never a Menu/Flickable content child.
        // hideSource retains native input/focus on the original contentItem.
        parent: clipping.active ? clipping.content.parent : null
        active: clipping.active && clipping.geometry.available && clipping.geometry.rect.width > 0 && clipping.geometry.rect.height > 0
        width: clipping.content ? clipping.content.width : 0
        height: clipping.content ? clipping.content.height : 0
        x: clipping.content ? clipping.content.x : 0
        y: clipping.content ? clipping.content.y : 0
        z: clipping.content ? clipping.content.z : 0
        scale: clipping.content ? clipping.content.scale : 1
        rotation: clipping.content ? clipping.content.rotation : 0
        transformOrigin: clipping.content ? clipping.content.transformOrigin : Item.Center
        opacity: clipping.content ? clipping.content.opacity : 1
        sourceComponent: Item {
            ShaderEffectSource {
                id: capture
                objectName: "popupContentCapture"
                sourceItem: clipping.content
                sourceRect: clipping.captureRect
                textureSize: clipping.textureSize
                hideSource: true
                visible: false
                // This capture is already at final device resolution. Do not
                // blend neighbouring content texels; the separate AA mask
                // remains smoothly sampled at the rounded boundary.
                smooth: false
                live: true
                recursive: false
            }
            Item {
                id: mask
                objectName: "popupContentRoundedMask"
                visible: false
                width: clipping.captureRect.width
                height: clipping.captureRect.height
                layer.enabled: true
                layer.smooth: true
                layer.textureSize: clipping.textureSize
                Rectangle {
                    x: clipping.frameRect.x - clipping.captureRect.x
                    y: clipping.frameRect.y - clipping.captureRect.y
                    width: clipping.frameRect.width
                    height: clipping.frameRect.height
                    radius: clipping.radius
                    visible: !clipping.frame || !clipping.frame.smoothCorners
                    antialiasing: true
                    color: "white"
                }
                Loader {
                    x: clipping.frameRect.x - clipping.captureRect.x
                    y: clipping.frameRect.y - clipping.captureRect.y
                    width: clipping.frameRect.width
                    height: clipping.frameRect.height
                    active: clipping.active && clipping.frame.smoothCorners
                    sourceComponent: EchoRoundedRect {
                        radius: clipping.radius
                    }
                }
            }
            MultiEffect {
                objectName: "popupContentMaskEffect"
                x: clipping.captureRect.x
                y: clipping.captureRect.y
                width: clipping.captureRect.width
                height: clipping.captureRect.height
                source: capture
                maskEnabled: true
                maskSource: mask
                maskThresholdMin: 0.5
                maskSpreadAtMin: 1
                autoPaddingEnabled: false
            }
        }
    }
}
