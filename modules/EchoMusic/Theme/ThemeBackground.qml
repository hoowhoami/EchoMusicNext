pragma ComponentBehavior: Bound
import QtQuick

Item {
    id: background
    property Component surfaceEffects: null
    property color baseColor: Theme.shell
    clip: true
    // CSS layout-skin opacity composites the finished skin once. Qt Item
    // opacity alone attenuates each overlapping child independently.
    layer.enabled: visible && opacity > 0 && opacity < 1
    Rectangle {
        objectName: "themeBaseSurface"
        anchors.fill: parent
        color: background.baseColor
    }
    Item {
        anchors.fill: parent
        visible: Theme.store.extensions.theme === null && Theme.store.themeKey === "custom" && Theme.store.backgroundImage.toString() !== ""
        Image {
            id: artwork
            objectName: "themeBackgroundImage"
            readonly property bool cropped: Theme.store.backgroundCrop.width > 0 && Theme.store.backgroundCrop.height > 0
            readonly property size dimensions: Theme.store.backgroundSize
            readonly property real factor: dimensions.width > 0 && dimensions.height > 0 ? Math.max((background.width * 1.1 + 2) / dimensions.width, (background.height * 1.1 + 2) / dimensions.height) : 1
            width: cropped ? background.width : dimensions.width * factor
            height: cropped ? background.height : dimensions.height * factor
            x: cropped ? 0 : (background.width - width) / 2
            y: cropped ? 0 : (background.height - height) / 2
            onStatusChanged: {
                if (status === Image.Error)
                    Theme.store.reportBackgroundUnavailable(source);
            }
            source: Theme.store.backgroundImage
            sourceSize: dimensions
            sourceClipRect: cropped ? Theme.store.backgroundCrop : Qt.rect(0, 0, 0, 0)
            fillMode: Image.Stretch
            asynchronous: true
            cache: true
        }
        Rectangle {
            anchors.fill: parent
            color: Theme.shell
            opacity: Theme.store.backgroundShade / 100
        }
    }
    Loader {
        anchors.fill: parent
        active: background.visible && background.opacity > 0 && background.Window.window !== null && background.Window.window.visible && background.Window.window.visibility !== Window.Minimized && Object.keys(Theme.store.extensionAppearance.background || {}).length > 0
        sourceComponent: ExtensionBackground {
            entry: Theme.store.extensions.theme
            recipe: Theme.store.extensionAppearance.background || {}
        }
    }
    AppearanceHost {
        anchors.fill: parent
        entry: Theme.store.extensions.theme
        placement: "background"
        resourceActive: background.opacity > 0
    }
    Loader {
        id: extensionGradient
        width: parent.width
        height: parent.height * ((Theme.store.extensionAppearance.accentGradient || {}).height || 70) / 100
        opacity: (Theme.store.extensionAppearance.accentGradient || {}).opacity === undefined ? 1 : Theme.store.extensionAppearance.accentGradient.opacity
        active: background.visible && background.opacity > 0 && background.Window.window !== null && background.Window.window.visible && background.Window.window.visibility !== Window.Minimized && Object.keys(Theme.store.extensionAppearance.accentGradient || {}).length > 0 && extensionGradient.opacity > 0
        sourceComponent: ExtensionGradient {
            recipe: Theme.store.extensionAppearance.accentGradient || {}
        }
    }
    Loader {
        anchors.fill: parent
        sourceComponent: background.surfaceEffects
    }
    Loader {
        id: atmosphere
        readonly property real gain: Math.max(1, Theme.store.atmosphereStrength / 100)
        readonly property real peak: (Theme.dark ? 0.30 : 0.32) * gain
        readonly property real mid: (Theme.dark ? 0.123 : 0.127) * gain
        width: parent.width
        height: parent.height * Theme.store.atmosphereHeight / 100
        visible: Theme.store.coverAtmosphere
        opacity: Math.min(1, Theme.store.atmosphereStrength / 100)
        active: visible && background.opacity > 0 && width > 0 && height > 0 && (!background.Window.window || background.Window.window.visible && background.Window.window.visibility !== Window.Minimized)
        // Evaluate the original five-stop fade directly in the final quad.
        // No atmosphere texture, blur pyramid, frame clock or added skin layer.
        sourceComponent: GraphicsInfo.api === GraphicsInfo.Software ? softwareAtmosphere : preciseAtmosphere
    }
    Component {
        id: preciseAtmosphere
        ShaderEffect {
            objectName: "themeAtmosphereComposite"
            property color tintColor: Theme.store.coverAtmosphereColor
            property real peakAlpha: atmosphere.peak
            property real midAlpha: atmosphere.mid
            // The existing skin layer applies overall transparency afterwards.
            // Compensate only that attenuation, keeping the final perturbation
            // within a single SDR code value without a higher-bit-depth FBO.
            property real ditherGain: background.layer.enabled ? 1 / Math.max(.125, background.opacity) : 1
            fragmentShader: "qrc:/qt/qml/EchoMusic/Theme/shaders/atmosphere.frag.qsb"
        }
    }
    // ShaderEffect is unavailable with Qt's software renderer. Keep the
    // original native gradient on that capability path; never lose the skin.
    Component {
        id: softwareAtmosphere
        Rectangle {
            gradient: Gradient {
                GradientStop {
                    position: 0
                    color: Theme.withAlpha(Theme.store.coverAtmosphereColor, atmosphere.peak)
                }
                GradientStop {
                    position: 0.22
                    color: Theme.withAlpha(Theme.store.coverAtmosphereColor, atmosphere.peak * 0.94)
                }
                GradientStop {
                    position: 0.55
                    color: Theme.withAlpha(Theme.store.coverAtmosphereColor, atmosphere.mid)
                }
                GradientStop {
                    position: 0.802
                    color: Theme.withAlpha(Theme.store.coverAtmosphereColor, atmosphere.mid * 0.3)
                }
                GradientStop {
                    position: 1
                    color: "transparent"
                }
            }
        }
    }
}
