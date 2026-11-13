pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import EchoMusic.Theme

TextField {
    id: control
    EchoUiPart {
        id: formPart
        target: control
        role: "control.textField"
        key: control.objectName
        properties: ["background", "borderColor", "borderWidth", "radius"]
    }
    property int size: EchoControlSize.Standard
    property bool clearable: false
    property ThemeColors colors: Theme.colors
    signal cleared
    // Size roles support large login fields and compact settings.
    implicitHeight: EchoControlSize.heightFor(size)
    leftPadding: EchoControlSize.fieldPaddingFor(size)
    rightPadding: leftPadding + (clearable ? (size === EchoControlSize.Small ? 20 : 24) : 0)
    font: Theme.font(size === EchoControlSize.Large ? 15 : size === EchoControlSize.Standard ? 13 : 12, Theme.mediumWeight)
    color: colors.text
    EchoInputFocus.boundary: control
    EchoInputFocus.menuContent: editingMenu.item ? editingMenu.item.contentItem : null
    ContextMenu.menu: null
    ContextMenu.onRequested: position => editingMenu.popupAt(position)
    EchoTextEditingMenu {
        id: editingMenu
        editor: control
    }
    cursorDelegate: EchoTextCursor {
        editor: control
    }
    placeholderTextColor: colors.secondaryText
    selectionColor: colors.accent
    selectedTextColor: "#000000"
    EchoIconButton {
        objectName: "clearTextAction"
        visible: control.clearable && control.enabled && !control.readOnly && control.text.length > 0
        x: control.width - width - (control.size === EchoControlSize.Large ? 12 : control.size === EchoControlSize.Small ? 4 : 8)
        anchors.verticalCenter: parent.verticalCenter
        width: control.size === EchoControlSize.Small ? 20 : 24
        height: width
        padding: 5
        focusPolicy: Qt.NoFocus
        glyph: "x"
        text: qsTr("清空输入")
        colors: control.colors
        foreground: Theme.withAlpha(control.color, 0.6)
        onClicked: {
            const previousText = control.text;
            // Preserve Qt's IME cancellation and undo behavior. Clicking the
            // clear action is a user edit; calling clear() from code is not.
            control.clear();
            if (control.text !== previousText)
                control.textEdited();
            control.cleared();
            control.forceActiveFocus();
        }
    }
    background: EchoInputBackground {
        colors: control.colors
        focused: control.activeFocus
        color: formPart.value("background", Theme.withAlpha(colors.tone, focused ? 0.07 : 0.05))
        radius: formPart.value("radius", Theme.controlRadius)
        border.width: formPart.value("borderWidth", 0)
        border.color: formPart.value("borderColor", "transparent")
    }
}
