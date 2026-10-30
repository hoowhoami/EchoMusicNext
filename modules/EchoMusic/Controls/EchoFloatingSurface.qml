pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Effects
import EchoMusic.Theme

Item {
    id: floatingSurface
    EchoUiPart {
        id: publicSurface
        target: floatingSurface
        role: "panel.floating"
        key: floatingSurface.objectName
        active: floatingSurface.resourceActive
        properties: ["background", "radius"]
        baseValues: ({
                background: floatingSurface.themedColor,
                radius: floatingSurface.radius
            })
    }
    readonly property var material: Theme.store.extensionAppearance.material || ({})
    readonly property var surfaces: Theme.store.extensionAppearance.surface || ({})
    readonly property real recipeOpacity: surfaces[dialogShadow ? "dialog" : "floating"] === undefined ? 1 : surfaces[dialogShadow ? "dialog" : "floating"]
    readonly property color themedColor: Theme.withAlpha(color, color.a * recipeOpacity)
    readonly property color paintedColor: publicSurface.value("background", themedColor)
    readonly property real paintedRadius: publicSurface.value("radius", radius)
    property real radius: Theme.popoverRadius
    property color color: Theme.floating.elevated
    property bool dialogShadow: false
    property bool shadowEnabled: true
    property bool frostedEnabled: Theme.store.floatingFrosted
    property real blurSigma: material.blurSigma === undefined ? 24 : material.blurSigma
    property real backdropFillOpacity: material.backdropFillOpacity === undefined ? 0.82 : material.backdropFillOpacity
    property alias border: outline.border
    readonly property bool resourceActive: visible && width > 0 && height > 0 && Window.window !== null && Window.window.visible && Window.window.visibility !== Window.Minimized
    // Software/OpenVG cannot render the QShader-based effect graph. Unknown
    // also uses the plain fill until GraphicsInfo knows this window's backend.
    readonly property bool shaderEffectsSupported: GraphicsInfo.shaderType === GraphicsInfo.RhiShader && GraphicsInfo.api !== GraphicsInfo.Null
    readonly property bool frosted: frostedEnabled && resourceActive && shaderEffectsSupported
    readonly property bool hasBackdrop: frostLoader.body !== null && frostLoader.body.hasBackdrop
    antialiasing: true
    // Capture the local area plus the blur kernel, at quarter resolution.
    // Qt MultiEffect owns the blur implementation; no custom Gaussian passes.
    // The Loader owns the entire effect graph: hidden
    // surfaces and non-frosted hints must not retain samplers, shaders or textures.
    readonly property real blurPadding: blurSigma * 3
    // Isolate backdrop/fill/outline from the overlay's scene ordering. Keep
    // the effect graph and its layer together for the visible lifetime (Metal).
    // Shadows stay outside the body layer so their blur is not cropped into
    // a rectangular frame; their shader already excludes the rounded interior.
    Item {
        objectName: "floatingSurfaceBody"
        anchors.fill: parent
        layer.enabled: floatingSurface.frosted
        // Popups scale with the application zoom. Nearest sampling of this
        // isolated layer turns an otherwise antialiased corner into steps.
        layer.smooth: true
        Loader {
            id: frostLoader
            anchors.fill: parent
            z: -0.6
            active: floatingSurface.frosted
            readonly property FrostedBody body: item as FrostedBody
            sourceComponent: FrostedBody {}
        }
        // Qt Rectangle replaces its fill with the border, unlike a CSS background
        // painted under the border box. Separate the layers so translucent strokes
        // blend over the surface instead of exposing cards behind the popup.
        Rectangle {
            anchors.fill: parent
            z: -0.5
            color: floatingSurface.hasBackdrop ? Theme.withAlpha(floatingSurface.paintedColor, floatingSurface.paintedColor.a * floatingSurface.backdropFillOpacity) : floatingSurface.paintedColor
            radius: floatingSurface.paintedRadius
            antialiasing: floatingSurface.antialiasing
        }
        Rectangle {
            id: outline
            anchors.fill: parent
            z: -0.4
            color: "transparent"
            radius: floatingSurface.paintedRadius
            antialiasing: floatingSurface.antialiasing
            border.width: 1
            border.color: Theme.floating.surfaceOutline
            border.pixelAligned: false
        }
    }
    component FrostedBody: Item {
        readonly property bool hasBackdrop: sampler.source !== null
        EchoBackdropSampler {
            id: sampler
            surface: floatingSurface
            padding: floatingSurface.blurPadding
            enabled: floatingSurface.frosted
        }
        ShaderEffectSource {
            id: backdrop
            objectName: "floatingBackdropCapture"
            visible: false
            width: (floatingSurface.width + floatingSurface.blurPadding * 2) / 4
            height: (floatingSurface.height + floatingSurface.blurPadding * 2) / 4
            sourceItem: floatingSurface.hasBackdrop ? sampler.source : null
            sourceRect: sampler.sourceRect
            textureSize: Qt.size(Math.ceil(width), Math.ceil(height))
            smooth: true
            recursive: false
            live: true
        }
        Item {
            id: clipMask
            visible: false
            width: floatingSurface.width + floatingSurface.blurPadding * 2
            height: floatingSurface.height + floatingSurface.blurPadding * 2
            Rectangle {
                x: floatingSurface.blurPadding
                y: floatingSurface.blurPadding
                width: floatingSurface.width
                height: floatingSurface.height
                radius: floatingSurface.paintedRadius
                color: "white"
                antialiasing: true
            }
        }
        ShaderEffectSource {
            id: maskTexture
            visible: false
            width: clipMask.width
            height: clipMask.height
            sourceItem: floatingSurface.hasBackdrop ? clipMask : null
            smooth: true
        }
        MultiEffect {
            id: blurred
            objectName: "floatingBackdropBlur"
            visible: false
            width: backdrop.width
            height: backdrop.height
            source: floatingSurface.hasBackdrop ? backdrop : null
            blurEnabled: true
            blurMax: 32
            blur: Math.min(1, floatingSurface.blurSigma / 24)
            autoPaddingEnabled: false
        }
        ShaderEffectSource {
            id: blurredTexture
            objectName: "floatingBackdropTexture"
            visible: false
            width: backdrop.width
            height: backdrop.height
            textureSize: backdrop.textureSize
            sourceItem: floatingSurface.hasBackdrop ? blurred : null
            smooth: true
        }
        MultiEffect {
            x: -floatingSurface.blurPadding
            y: -floatingSurface.blurPadding
            width: floatingSurface.width + floatingSurface.blurPadding * 2
            height: floatingSurface.height + floatingSurface.blurPadding * 2
            visible: floatingSurface.hasBackdrop
            source: floatingSurface.hasBackdrop ? blurredTexture : null
            blurEnabled: false
            saturation: 0.4
            maskEnabled: true
            maskSource: floatingSurface.hasBackdrop ? maskTexture : null
            // MultiEffect's default zero spread turns every nonzero AA sample
            // opaque, expanding the backdrop beyond the fill into a jagged rim.
            // Map the full coverage range instead of applying a binary cutout.
            maskThresholdMin: 0.5
            maskSpreadAtMin: 1
            autoPaddingEnabled: false
        }
    }
    Loader {
        anchors.fill: parent
        z: -1
        active: floatingSurface.resourceActive && floatingSurface.shadowEnabled && floatingSurface.shaderEffectsSupported
        sourceComponent: Item {
            EchoSurfaceShadow {
                anchors.fill: parent
                visible: floatingSurface.shadowEnabled
                surface: floatingSurface
                z: -1
                radius: floatingSurface.paintedRadius
                blur: floatingSurface.dialogShadow ? 72 : 38.4
                offset: Qt.vector2d(0, floatingSurface.dialogShadow ? 24 : 12)
                color: Qt.rgba(0, 0, 0, floatingSurface.dialogShadow ? Theme.dark ? 0.42 : 0.22 : Theme.dark ? 0.36 : 0.12)
            }
            EchoSurfaceShadow {
                anchors.fill: parent
                surface: floatingSurface
                z: -1
                radius: floatingSurface.paintedRadius
                visible: floatingSurface.shadowEnabled && !floatingSurface.dialogShadow
                blur: 7.2
                offset: Qt.vector2d(0, 2)
                color: Qt.rgba(0, 0, 0, Theme.dark ? 0.18 : 0.06)
            }
        }
    }
}
