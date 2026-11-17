pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import EchoMusic.Controls
import EchoMusic.Theme
import EchoMusic.Playback

RowLayout {
    id: control
    required property var preferences
    property string colorProperty: "secondaryColor"
    property string followProperty: "secondaryFollowCoverColor"
    required property color resolvedColor
    property color coverColor: Theme.accent
    property string resetColor: ""
    property real uiScale: 1
    spacing: 8
    EchoSoftButton {
        text: qsTr("恢复默认")
        colors: Theme.floating
        onClicked: {
            control.preferences[control.colorProperty] = control.resetColor;
            control.preferences[control.followProperty] = false;
        }
    }
    EchoSoftButton {
        objectName: "lyricSecondaryColorAction"
        text: qsTr("更改")
        Accessible.name: qsTr("更改副行颜色")
        colors: Theme.floating
        rightPadding: 40
        onClicked: picker.open()
        Rectangle {
            anchors.right: parent.right
            anchors.rightMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            width: 24
            height: 24
            radius: Theme.controlRadius
            color: control.resolvedColor
            EchoDashedFrame {
                anchors.fill: parent
                solid: true
                strokeColor: Theme.floating.strongBorder
                radius: Theme.controlRadius
            }
        }
    }
    EchoPopupLoader {
        id: picker
        hostItem: control
        sourceComponent: EchoColorPickerDialog {
            objectName: "lyricSecondaryColorPicker"
            title: qsTr("选择副行颜色")
            uiScale: control.uiScale
            value: LyricColors.opaqueHex(control.resolvedColor)
            presets: ["#31cfa1", "#b9b9b9", "#ffffff", "#ffd166", "#ef476f", "#118ab2", "#8b5cf6", "#f97316", "#22c55e", "#60a5fa", "#f43f5e", "#0f172a"]
            dynamicLabel: qsTr("跟随封面取色")
            dynamicColor: control.coverColor
            dynamicSelected: control.preferences[control.followProperty]
            onConfirmed: value => {
                control.preferences[control.followProperty] = false;
                control.preferences[control.colorProperty] = value;
            }
            onDynamicConfirmed: {
                control.preferences[control.colorProperty] = "";
                control.preferences[control.followProperty] = true;
            }
        }
    }
}
