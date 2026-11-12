pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import EchoMusic.Controls
import EchoMusic.Theme
import EchoMusic.Shell

Control {
    id: control
    required property ShortcutService service
    required property int row
    property bool global: false
    property string shortcutLabel: ""
    property string errorText: ""
    readonly property bool recording: service.recordingRow === row && service.recordingGlobal === global
    implicitWidth: 140
    implicitHeight: 36
    font: Theme.spacedFont(13, Theme.mediumWeight, 0.325)
    hoverEnabled: true
    activeFocusOnTab: true
    enabled: !global || service.globalEnabled
    opacity: enabled ? 1 : 0.5
    padding: 8
    rightPadding: shortcutLabel.length ? 24 : 8
    Accessible.role: Accessible.EditableText
    Accessible.name: (global ? qsTr("全局快捷键") : qsTr("快捷键")) + " " + shortcutLabel
    Accessible.description: errorText.length ? errorText : qsTr("点击录制，Escape、Delete 或 Backspace 清除")
    onActiveFocusChanged: {
        if (activeFocus)
            service.beginRecording(row, global);
        else if (recording)
            service.stopRecording();
    }
    Component.onDestruction: {
        if (recording)
            service.stopRecording();
    }
    background: Rectangle {
        radius: Theme.controlRadius
        color: control.recording ? Theme.withAlpha(Theme.floating.accent, 0.05) : Theme.floating.mutedSurface
        border.color: control.recording ? Theme.floating.accent : control.hovered ? Theme.withAlpha(Theme.floating.accent, 0.5) : Theme.floating.controlBorder
    }
    contentItem: EchoButtonContent {
        text: control.recording ? qsTr("按键盘输入快捷键") : control.shortcutLabel.length ? control.shortcutLabel : control.enabled ? qsTr("点击录制") : qsTr("开启后可录制")
        font: control.font
        color: control.recording ? Theme.floating.accentText : !control.shortcutLabel.length ? Theme.floatingSecondaryText : Theme.floatingText
    }
    EchoActionCursor {}
    TapHandler {
        onTapped: eventPoint => {
            if (control.shortcutLabel.length && eventPoint.position.x >= control.width - 26)
                return;
            control.forceActiveFocus();
            control.service.beginRecording(control.row, control.global);
        }
    }
    Connections {
        target: control.service
        function onRecordingChanged(): void {
            if (!control.recording && control.activeFocus)
                control.focus = false;
        }
    }
    EchoIconButton {
        x: control.width - width - 4
        anchors.verticalCenter: parent.verticalCenter
        visible: control.enabled && control.shortcutLabel.length > 0 && !control.recording
        implicitWidth: 18
        implicitHeight: 18
        padding: 3
        glyph: "x"
        text: control.global ? qsTr("清除全局快捷键") : qsTr("清除快捷键")
        colors: Theme.floating
        onClicked: control.service.clearBinding(control.row, control.global)
    }
}
