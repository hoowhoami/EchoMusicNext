pragma ComponentBehavior: Bound
import QtQuick
import EchoMusic.Theme

// In-app RGB picker. Only its editing draft lives here; callers decide when
// to persist. Grayscale/black edits retain hue so the next drag is predictable.
Item {
    id: picker
    property string value: "#0071e3"
    property real hue: 0
    property real saturation: 1
    property real brightness: 1
    signal colorEdited(string color)
    implicitWidth: 240
    implicitHeight: 190
    function sync(): void {
        const components = EchoColorValue.hsv(value);
        if (components.hue >= 0)
            hue = components.hue;
        saturation = components.saturation;
        brightness = components.value;
    }
    function publish(): void {
        colorEdited(EchoColorValue.fromHsv(hue, saturation, brightness));
    }
    onValueChanged: if (!svInput.pressed && !hueInput.pressed)
        sync()
    Component.onCompleted: sync()
    Rectangle {
        id: spectrum
        objectName: "colorSaturationValue"
        width: parent.width
        height: Math.max(36, parent.height - 30)
        radius: Theme.controlRadius
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop {
                position: 0
                color: "white"
            }
            GradientStop {
                position: 1
                color: EchoColorValue.fromHsv(picker.hue, 1, 1)
            }
        }
        Rectangle {
            anchors.fill: parent
            radius: parent.radius
            gradient: Gradient {
                GradientStop {
                    position: 0
                    color: "transparent"
                }
                GradientStop {
                    position: 1
                    color: "black"
                }
            }
        }
        Rectangle {
            x: picker.saturation * (parent.width - 1) - width / 2
            y: (1 - picker.brightness) * (parent.height - 1) - height / 2
            width: 12
            height: 12
            radius: 6
            color: "transparent"
            border.color: "white"
            border.width: 2
            Rectangle {
                anchors.fill: parent
                anchors.margins: -1
                radius: 7
                color: "transparent"
                border.color: "#99000000"
            }
        }
        MouseArea {
            id: svInput
            anchors.fill: parent
            activeFocusOnTab: true
            Accessible.role: Accessible.Slider
            Accessible.name: qsTr("饱和度与明度")
            function move(x: real, y: real): void {
                picker.saturation = Math.max(0, Math.min(1, x / Math.max(1, width - 1)));
                picker.brightness = 1 - Math.max(0, Math.min(1, y / Math.max(1, height - 1)));
                picker.publish();
            }
            onPressed: mouse => {
                forceActiveFocus(Qt.MouseFocusReason);
                move(mouse.x, mouse.y);
            }
            onPositionChanged: mouse => {
                if (pressed)
                    move(mouse.x, mouse.y);
            }
            Keys.onPressed: event => {
                const step = event.modifiers & Qt.ShiftModifier ? 0.1 : 0.01;
                if (event.key === Qt.Key_Left || event.key === Qt.Key_Right)
                    picker.saturation = Math.max(0, Math.min(1, picker.saturation + (event.key === Qt.Key_Right ? step : -step)));
                else if (event.key === Qt.Key_Up || event.key === Qt.Key_Down)
                    picker.brightness = Math.max(0, Math.min(1, picker.brightness + (event.key === Qt.Key_Up ? step : -step)));
                else
                    return;
                picker.publish();
                event.accepted = true;
            }
        }
        Rectangle {
            anchors.fill: parent
            color: "transparent"
            radius: parent.radius
            border.width: svInput.activeFocus ? 2 : 0
            border.color: Theme.floating.accentText
        }
    }
    Rectangle {
        id: hueStrip
        objectName: "colorHueStrip"
        width: parent.width
        height: 16
        y: parent.height - height
        radius: Theme.controlRadius
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop {
                position: 0
                color: "#ff0000"
            }
            GradientStop {
                position: 1 / 6
                color: "#ffff00"
            }
            GradientStop {
                position: 2 / 6
                color: "#00ff00"
            }
            GradientStop {
                position: 3 / 6
                color: "#00ffff"
            }
            GradientStop {
                position: 4 / 6
                color: "#0000ff"
            }
            GradientStop {
                position: 5 / 6
                color: "#ff00ff"
            }
            GradientStop {
                position: 1
                color: "#ff0000"
            }
        }
        Rectangle {
            x: picker.hue * (parent.width - 1) - width / 2
            anchors.verticalCenter: parent.verticalCenter
            width: 8
            height: parent.height + 4
            radius: 3
            color: "transparent"
            border.width: 2
            border.color: "white"
        }
        MouseArea {
            id: hueInput
            anchors.fill: parent
            activeFocusOnTab: true
            Accessible.role: Accessible.Slider
            Accessible.name: qsTr("色相")
            function move(x: real): void {
                picker.hue = Math.max(0, Math.min(1, x / Math.max(1, width - 1)));
                picker.publish();
            }
            onPressed: mouse => {
                forceActiveFocus(Qt.MouseFocusReason);
                move(mouse.x);
            }
            onPositionChanged: mouse => {
                if (pressed)
                    move(mouse.x);
            }
            Keys.onPressed: event => {
                if (event.key === Qt.Key_Left || event.key === Qt.Key_Right)
                    picker.hue = Math.max(0, Math.min(1, picker.hue + (event.key === Qt.Key_Right ? 1 : -1) / 360));
                else if (event.key === Qt.Key_Home)
                    picker.hue = 0;
                else if (event.key === Qt.Key_End)
                    picker.hue = 1;
                else
                    return;
                picker.publish();
                event.accepted = true;
            }
        }
        Rectangle {
            anchors.fill: parent
            color: "transparent"
            radius: parent.radius
            border.width: hueInput.activeFocus ? 2 : 0
            border.color: Theme.floating.accentText
        }
    }
}
