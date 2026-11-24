pragma ComponentBehavior: Bound
import QtQuick
import EchoMusic.Controls
import EchoMusic.Playback

// One cover owns a 32x32 preprocessing graph and one cropped mesh render target.
// The nine input stages only update on image load, never on animation frames.
Item {
    id: artwork
    required property url coverSource
    required property real phase
    required property size targetSize
    required property real renderDpr
    property alias image: cover
    readonly property bool imageReady: cover.status === Image.Ready
    readonly property bool ready: imageReady && effects.status === Loader.Ready
    readonly property bool failed: cover.status === Image.Error
    readonly property var texture: (effects.item as Effects)?.texture ?? null
    readonly property BackgroundMesh mesh: (effects.item as Effects)?.mesh ?? null
    signal rendered
    signal prepared
    function refresh(): void {
        (effects.item as Effects)?.requestFrame();
    }
    onPhaseChanged: refresh()
    onTargetSizeChanged: refresh()
    EchoImageMetrics {
        id: metrics
        item: cover
        source: artwork.coverSource
        maximumDecodeSize: 32
        deferRequests: true
    }
    Image {
        id: cover
        objectName: "playbackBackgroundCover"
        width: 32 / artwork.renderDpr
        height: width
        source: metrics.resolvedSource
        sourceSize: metrics.decodeSize
        cache: false
        asynchronous: true
        visible: false
    }
    Loader {
        id: effects
        active: artwork.imageReady
        sourceComponent: Effects {}
        onLoaded: (item as Effects).prepare()
    }
    component BlurPass: Item {
        id: pass
        required property int index
        required property var input
        required property real dpr
        readonly property alias texture: capture
        function refresh(): void {
            capture.scheduleUpdate();
        }
        ShaderEffect {
            id: blur
            width: 32 / pass.dpr
            height: width
            visible: false
            property var source: pass.input
            property vector2d step: pass.index % 2 === 0 ? Qt.vector2d(1 / 32, 0) : Qt.vector2d(0, 1 / 32)
            property real grade: 0
            fragmentShader: "qrc:/qt/qml/EchoMusic/Playback/shaders/background-preprocess.frag.qsb"
        }
        ShaderEffectSource {
            id: capture
            sourceItem: blur
            textureSize: Qt.size(32, 32)
            // Horizontal sums retain precision; each completed 2D iteration
            // quantizes just once, matching the upstream ImageData filter.
            format: pass.index % 2 === 0 ? ShaderEffectSource.RGBA16F : ShaderEffectSource.RGBA8
            live: false
            visible: false
        }
    }
    component Effects: Item {
        id: stage
        readonly property alias texture: result
        readonly property alias mesh: mesh
        function requestFrame(): void {
            updateFrame.restart();
        }
        function prepare(): void {
            prepareFrame.restart();
        }
        Timer {
            id: prepareFrame
            interval: 0
            onTriggered: {
                gradingTexture.scheduleUpdate();
                for (let i = 0; i < passes.count; ++i)
                    (passes.itemAt(i) as BlurPass).refresh();
                stage.requestFrame();
                artwork.prepared();
            }
        }
        ShaderEffect {
            id: grading
            width: cover.width
            height: cover.height
            visible: false
            property var source: cover
            property vector2d step: Qt.vector2d(0, 0)
            property real grade: 1
            fragmentShader: "qrc:/qt/qml/EchoMusic/Playback/shaders/background-preprocess.frag.qsb"
        }
        ShaderEffectSource {
            id: gradingTexture
            objectName: "playbackBackgroundGradingTexture"
            sourceItem: grading
            textureSize: Qt.size(32, 32)
            live: false
            visible: false
        }
        Repeater {
            id: passes
            model: artwork.imageReady ? 8 : 0
            delegate: BlurPass {
                input: index === 0 ? gradingTexture : (passes.itemAt(index - 1) as BlurPass)?.texture
                dpr: artwork.renderDpr
            }
        }
        BackgroundMesh {
            id: mesh
            objectName: "playbackBackgroundMesh"
            width: artwork.targetSize.width / artwork.renderDpr
            height: artwork.targetSize.height / artwork.renderDpr
            source: passes.count === 8 ? (passes.itemAt(7) as BlurPass)?.texture ?? null : null
            phase: artwork.phase
            visible: false
            // Deterministic per-cover geometry makes resizing/reopening stable.
            seed: {
                let hash = 2166136261;
                const url = artwork.coverSource.toString();
                for (let i = 0; i < url.length; ++i)
                    hash = Math.imul(hash ^ url.charCodeAt(i), 16777619);
                return hash >>> 0 || 1;
            }
        }
        ShaderEffectSource {
            id: result
            objectName: "playbackBackgroundTexture"
            sourceItem: mesh
            textureSize: artwork.targetSize
            format: ShaderEffectSource.RGBA16F
            live: false
            visible: false
        }
        Timer {
            id: updateFrame
            interval: 0
            onTriggered: if (artwork.ready) {
                result.scheduleUpdate();
                artwork.rendered();
            }
        }
    }
}
