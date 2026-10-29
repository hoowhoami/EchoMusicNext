import QtQuick

QtObject {
    id: output
    property real transfer: 1
    property real whiteLevel: 100
    property matrix4x4 gamutTransform: Qt.matrix4x4(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1)
    property Component effect: ShaderEffect {
        property var source
        property real hdrActive: output.transfer
        property real whiteLevel: output.whiteLevel
        property matrix4x4 gamutTransform: output.gamutTransform
        fragmentShader: "qrc:/echo/graphics/graphics/shaders/window-output.frag.qsb"
    }
}
