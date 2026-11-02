pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Controls.Basic.impl as BasicImpl
import QtQuick.Templates as T
import EchoMusic.Theme

T.TextArea {
    id: control
    property ThemeColors colors: Theme.colors
    // A formatted document can place its first baseline below the plain
    // font line box. Its owner supplies the native document baseline here.
    property real placeholderBaseline: NaN
    EchoInputFocus.boundary: control
    cursorDelegate: EchoTextCursor {
        editor: control
        emptyLineBaseline: placeholder.y + placeholder.baselineOffset
    }
    implicitHeight: 120
    implicitWidth: Math.max(contentWidth + leftPadding + rightPadding, implicitBackgroundWidth + leftInset + rightInset, placeholder.implicitWidth + leftPadding + rightPadding)
    padding: 10
    leftPadding: 12
    rightPadding: 12
    wrapMode: TextEdit.Wrap
    font: Theme.font(13, Theme.mediumWeight)
    color: colors.text
    placeholderTextColor: colors.secondaryText
    selectionColor: colors.accent
    selectedTextColor: "#000000"
    ContextMenu.menu: BasicImpl.TextEditingContextMenu {
        editor: control
    }
    Text {
        id: placeholder
        objectName: "inputPlaceholder"
        x: control.leftPadding
        y: Number.isFinite(control.placeholderBaseline) ? control.topPadding + control.placeholderBaseline - baselineOffset : control.topPadding
        width: control.width - control.leftPadding - control.rightPadding
        height: control.height - control.topPadding - control.bottomPadding
        text: control.placeholderText
        textFormat: Text.PlainText
        font: control.font
        color: control.placeholderTextColor
        verticalAlignment: Number.isFinite(control.placeholderBaseline) ? Text.AlignTop : control.verticalAlignment
        visible: !control.length && !control.preeditText && (!control.activeFocus || control.horizontalAlignment !== Qt.AlignHCenter)
        wrapMode: control.wrapMode
        elide: Text.ElideRight
        renderType: control.renderType
    }
    background: EchoInputBackground {
        colors: control.colors
        focused: control.activeFocus
    }
}
