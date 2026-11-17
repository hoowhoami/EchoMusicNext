pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Shapes
import EchoMusic.Controls
import EchoMusic.Theme

Item {
    id: orb
    property bool recording: false
    property bool recognizing: false
    property bool resourceActive: true
    property string glyph: "device-speaker"
    signal clicked
    implicitWidth: 140
    implicitHeight: 140
    Loader {
        anchors.centerIn: parent
        active: orb.recording && orb.resourceActive && Theme.store.motionEnabled
        sourceComponent: Item {
            width: 140
            height: 140
            Shape {
                x: -28
                y: -28
                width: 196
                height: 196
                ShapePath {
                    strokeWidth: 0
                    fillGradient: RadialGradient {
                        centerX: 98
                        centerY: 98
                        centerRadius: 98
                        focalX: 98
                        focalY: 98
                        GradientStop {
                            position: 0
                            color: Theme.withAlpha(Theme.accent, 0.22)
                        }
                        GradientStop {
                            position: 0.7
                            color: "transparent"
                        }
                    }
                    PathSvg {
                        path: "M 0 98 A 98 98 0 1 0 196 98 A 98 98 0 1 0 0 98 Z"
                    }
                }
                SequentialAnimation on scale {
                    loops: Animation.Infinite
                    NumberAnimation {
                        from: 1
                        to: 1.25
                        duration: 800
                        easing.type: Easing.InOutSine
                    }
                    NumberAnimation {
                        to: 1
                        duration: 800
                        easing.type: Easing.InOutSine
                    }
                }
                SequentialAnimation on opacity {
                    loops: Animation.Infinite
                    NumberAnimation {
                        from: 0.7
                        to: 1
                        duration: 800
                        easing.type: Easing.InOutSine
                    }
                    NumberAnimation {
                        to: 0.7
                        duration: 800
                        easing.type: Easing.InOutSine
                    }
                }
            }
            Repeater {
                model: 3
                Rectangle {
                    id: ripple
                    required property int index
                    width: 140
                    height: 140
                    radius: 70
                    color: "transparent"
                    border.width: 2
                    border.color: Theme.accent
                    opacity: 0
                    SequentialAnimation {
                        running: true
                        PauseAnimation {
                            duration: ripple.index * 800
                        }
                        ParallelAnimation {
                            loops: Animation.Infinite
                            NumberAnimation {
                                target: ripple
                                property: "scale"
                                from: 1
                                to: 2.2
                                duration: 2400
                                easing.type: Easing.BezierSpline
                                easing.bezierCurve: [0, 0.4, 0.6, 1, 1, 1]
                            }
                            NumberAnimation {
                                target: ripple
                                property: "opacity"
                                from: 0.5
                                to: 0
                                duration: 2400
                                easing.type: Easing.BezierSpline
                                easing.bezierCurve: [0, 0.4, 0.6, 1, 1, 1]
                            }
                        }
                    }
                }
            }
        }
    }
    EchoActionButton {
        id: button
        objectName: "recognitionMainButton"
        anchors.fill: parent
        hoverEnabled: true
        enabled: !orb.recognizing
        Accessible.name: orb.recording ? qsTr("结束录音并识别") : qsTr("开始听歌识曲")
        opacity: orb.recognizing ? 0.6 : 1
        property real recordingScale: 1.05
        scale: down ? 0.96 : orb.recording ? recordingScale : hovered ? 1.04 : 1
        SequentialAnimation on recordingScale {
            running: orb.recording && orb.resourceActive && Theme.store.motionEnabled
            loops: Animation.Infinite
            NumberAnimation {
                from: 1.05
                to: 1.12
                duration: 500
                easing.type: Easing.InOutSine
            }
            NumberAnimation {
                to: 1.05
                duration: 500
                easing.type: Easing.InOutSine
            }
        }
        Behavior on scale {
            enabled: !orb.recording && Theme.store.motionEnabled
            NumberAnimation {
                duration: 300
                easing.type: Easing.OutCubic
            }
        }
        background: Rectangle {
            id: circle
            radius: 70
            color: "transparent"
            EchoDashedFrame {
                anchors.fill: parent
                solid: true
                strokeColor: Theme.text
                radius: 70
                thickness: button.visualFocus ? 2 : 0
                visible: button.visualFocus
            }
            Shape {
                anchors.fill: parent
                ShapePath {
                    strokeWidth: 0
                    fillGradient: LinearGradient {
                        x1: 0
                        y1: 0
                        x2: 140
                        y2: 140
                        GradientStop {
                            position: 0
                            color: orb.recording ? "#ef4444" : Theme.accent
                        }
                        GradientStop {
                            position: 1
                            color: orb.recording ? "#f97316" : Theme.mix(Theme.accent, "#6366f1", 0.2)
                        }
                    }
                    PathSvg {
                        path: "M 0 70 A 70 70 0 1 0 140 70 A 70 70 0 1 0 0 70 Z"
                    }
                }
            }
            Loader {
                anchors.fill: parent
                active: orb.resourceActive
                sourceComponent: EchoSurfaceShadow {
                    surface: circle
                    radius: 70
                    blur: 32
                    offset: Qt.vector2d(0, 8)
                    color: Theme.withAlpha(orb.recording ? "#ef4444" : Theme.accent, 0.35)
                }
            }
        }
        contentItem: EchoIcon {
            id: statusGlyph
            SequentialAnimation on opacity {
                running: orb.recognizing && orb.resourceActive && Theme.store.motionEnabled
                loops: Animation.Infinite
                onRunningChanged: if (!running)
                    statusGlyph.opacity = 1
                NumberAnimation {
                    from: 1
                    to: 0.5
                    duration: 1000
                    easing.type: Easing.InOutSine
                }
                NumberAnimation {
                    to: 1
                    duration: 1000
                    easing.type: Easing.InOutSine
                }
            }
            name: orb.recording ? "x" : orb.glyph
            color: Theme.accentForeground
            width: orb.recording ? 36 : 44
            height: width
            // Icon content must keep its source size inside the 140px control.
        }
        padding: orb.recording ? 52 : 48
        onClicked: orb.clicked()
    }
}
