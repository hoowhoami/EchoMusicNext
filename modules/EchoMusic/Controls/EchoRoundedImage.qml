pragma ComponentBehavior: Bound
import QtQuick

// A texture consumer, not an image loader/cache. The Image owner controls its
// decode size and lifetime; crop and rounded coverage use the existing texture.
ShaderEffect {
    required property Image image
    property real cornerRadius: 0
    property size cropSize: Qt.size(width, height)
    property var source: image
    property size viewSize: Qt.size(width, height)
    property vector2d cropScale: {
        const imageAspect = (image?.implicitWidth ?? 1) / Math.max(1, image?.implicitHeight ?? 1);
        const viewAspect = cropSize.width / Math.max(1, cropSize.height);
        return imageAspect > viewAspect ? Qt.vector2d(viewAspect / imageAspect, 1) : Qt.vector2d(1, imageAspect / Math.max(.0001, viewAspect));
    }
    fragmentShader: "qrc:/qt/qml/EchoMusic/Controls/shaders/cover-rounded.frag.qsb"
}
