pragma ComponentBehavior: Bound
import QtQuick
import EchoMusic.Theme
import EchoMusic.Controls
import EchoMusic.Playback

Item {
    id: surface
    required property PlaybackService playback
    required property bool resourceActive
    required property color baseColor
    readonly property LyricPortraitAppearance settings: playback.lyricAppearance.portrait
    readonly property bool available: resourceActive && !Theme.store.windowFrosted && Theme.store.transparency < 100
    readonly property url requestedSource: playback.portraits.source.toString().length > 0 ? playback.portraits.source : settings.fallbackCover ? playback.cover : ""
    readonly property bool ready: available && currentImage !== null
    property Image currentImage: null
    property Image incomingImage: null
    property real blend: 0
    property bool initialized: false
    function clear(): void {
        fade.stop();
        const current = currentImage;
        const incoming = incomingImage;
        currentImage = null;
        incomingImage = null;
        current?.destroy();
        incoming?.destroy();
        blend = 0;
    }
    function commit(): void {
        if (!incomingImage)
            return;
        const old = currentImage;
        currentImage = incomingImage;
        incomingImage = null;
        blend = 0;
        old?.destroy();
    }
    function replace(): void {
        if (!initialized)
            return;
        fade.stop();
        if (incomingImage) {
            if (incomingImage.status === Image.Ready && blend >= .5)
                commit();
            else {
                incomingImage.destroy();
                incomingImage = null;
            }
        }
        blend = 0;
        if (!available || requestedSource.toString().length === 0) {
            clear();
            return;
        }
        incomingImage = imageRecipe.createObject(surface, {
            requestedUrl: requestedSource
        });
    }
    onRequestedSourceChanged: replace()
    onAvailableChanged: replace()
    Component.onCompleted: {
        initialized = true;
        replace();
    }
    Component.onDestruction: clear()
    Connections {
        target: surface.playback.portraits
        function onTrackChanged(): void {
            surface.clear();
        }
    }
    Component {
        id: imageRecipe
        Image {
            id: image
            objectName: "portraitImageTexture"
            required property url requestedUrl
            width: surface.width
            height: surface.height
            visible: GraphicsInfo.api === GraphicsInfo.Software && surface.currentImage === image
            asynchronous: true
            retainWhileLoading: true
            cache: false
            source: metrics.resolvedSource
            sourceSize: metrics.decodeSize
            fillMode: Image.PreserveAspectCrop
            opacity: surface.settings.backdropOpacity / 100 * (1 - Theme.store.transparency / 100)
            EchoImageMetrics {
                id: metrics
                item: surface
                active: surface.available
                source: image.requestedUrl
            }
            onStatusChanged: {
                if (surface.incomingImage !== image)
                    return;
                if (status === Image.Ready) {
                    if (!surface.currentImage || !Theme.store.motionEnabled)
                        surface.commit();
                    else
                        fade.restart();
                } else if (status === Image.Error) {
                    surface.playback.portraits.imageFailed(requestedUrl);
                }
            }
            Component.onCompleted: if (status === Image.Ready || status === Image.Error) {
                // A cached/local decoder can finish inside createObject, before
                // the owner has adopted the incoming pointer. Re-evaluate once.
                readyDelivery.start();
            }
            Timer {
                id: readyDelivery
                interval: 0
                onTriggered: if (surface.incomingImage === image) {
                    if (image.status === Image.Error)
                        surface.playback.portraits.imageFailed(image.requestedUrl);
                    else if (image.status === Image.Ready) {
                        if (!surface.currentImage || !Theme.store.motionEnabled)
                            surface.commit();
                        else
                            fade.restart();
                    }
                }
            }
        }
    }
    NumberAnimation {
        id: fade
        target: surface
        property: "blend"
        from: 0
        to: 1
        duration: Theme.pageDuration
        easing.type: Easing.BezierSpline
        easing.bezierCurve: Theme.easeStandard
        onFinished: surface.commit()
    }
    Connections {
        target: Theme.store
        function onChanged(): void {
            if (!Theme.store.motionEnabled && surface.incomingImage?.status === Image.Ready) {
                fade.stop();
                surface.commit();
            }
        }
    }
    function crop(image: Image): vector2d {
        if (!image || image.implicitWidth <= 0 || image.implicitHeight <= 0)
            return Qt.vector2d(1, 1);
        const scale = Math.max(width / image.implicitWidth, height / image.implicitHeight);
        return Qt.vector2d(width / (image.implicitWidth * scale), height / (image.implicitHeight * scale));
    }
    Loader {
        anchors.fill: parent
        active: surface.ready && GraphicsInfo.api !== GraphicsInfo.Software
        sourceComponent: ShaderEffect {
            objectName: "portraitBackgroundSurface"
            property var current: surface.currentImage
            property var incoming: surface.incomingImage?.status === Image.Ready ? surface.incomingImage : surface.currentImage
            property vector2d currentCrop: surface.crop(surface.currentImage)
            property vector2d incomingCrop: surface.crop(surface.incomingImage?.status === Image.Ready ? surface.incomingImage : surface.currentImage)
            property real progress: surface.blend
            property real brightness: surface.settings.backdropOpacity / 100
            property real materialOpacity: 1 - Theme.store.transparency / 100
            property color baseColor: surface.baseColor
            fragmentShader: "qrc:/qt/qml/EchoMusic/Playback/shaders/portrait-background.frag.qsb"
        }
    }
}
