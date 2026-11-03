pragma ComponentBehavior: Bound
import QtQuick

EchoButton {
    id: control
    property string actionLabel: qsTr("发送消息")
    property int cooldownSeconds: 0
    enabled: !loading && cooldownSeconds <= 0
    accent: false
    glyph: "send"
    implicitWidth: implicitHeight
    leftPadding: 0
    rightPadding: 0
    Accessible.name: loading ? qsTr("发送中…") : cooldownSeconds > 0 ? actionLabel + qsTr("（%1 秒后可用）").arg(cooldownSeconds) : actionLabel
    contentItem: EchoButtonContent {
        visible: !control.publicPart.replaced
        glyph: control.loading || control.cooldownSeconds > 0 ? "" : control.glyph
        text: !control.loading && control.cooldownSeconds > 0 ? qsTr("%1s").arg(control.cooldownSeconds) : ""
        glyphSize: 17
        loading: control.loading
        color: control.publicPart.value("foreground", control.foreground)
    }
    EchoToolTip {
        parent: control
        requestedVisible: control.hovered && control.enabled
        text: control.actionLabel
    }
}
