pragma ComponentBehavior: Bound
import QtQuick
import EchoMusic.Theme

// One bounded image texture and a native Qt scene-graph quad. The UV crop is
// CSS object-fit:cover / object-position:center 35%, independent of the mask.
Item {
    id: artwork
    required property url imageUrl
    required property real materialOpacity
    required property vector3d scrim
    signal ready
    signal failed
    Image {
        id: image
        objectName: "discoverFlowArtwork"
        source: artwork.imageUrl
        sourceSize: Qt.size(800, 800)
        asynchronous: true
        visible: false
        onStatusChanged: {
            if (status === Image.Ready)
                artwork.ready();
            else if (status === Image.Error)
                artwork.failed();
        }
    }
    ShaderEffect {
        objectName: "discoverFlowArtworkEffect"
        anchors.fill: parent
        visible: image.status === Image.Ready
        property var source: image
        readonly property real imageRatio: image.implicitHeight > 0 ? image.implicitWidth / image.implicitHeight : 1
        readonly property real pageRatio: height > 0 ? width / height : 1
        readonly property real cropWidth: Math.min(1, pageRatio / imageRatio)
        readonly property real cropHeight: Math.min(1, imageRatio / pageRatio)
        property vector4d sourceRect: Qt.vector4d((1 - cropWidth) / 2, (1 - cropHeight) * 0.35, cropWidth, cropHeight)
        // This route meets the shell's bottom edge. Clip the actual media in
        // its existing quad; no page-sized offscreen layer is needed.
        property vector3d viewport: Qt.vector3d(width, height, Math.min(Theme.shellRadius, width / 2, height / 2))
        property real materialOpacity: artwork.materialOpacity
        property vector3d scrim: artwork.scrim
        fragmentShader: "qrc:/qt/qml/EchoMusic/Discovery/shaders/discover-artwork.frag.qsb"
    }
    Component.onCompleted: {
        if (image.status === Image.Ready)
            artwork.ready();
    }
}
