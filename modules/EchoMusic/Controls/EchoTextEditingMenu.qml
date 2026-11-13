pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Controls.impl as ControlsImpl
import QtQuick.Templates as T

// Qt owns context-menu gestures and edit semantics. The shared popup owner
// retains only the recipe between requests, just like the song menu.
EchoPopupLoader {
    id: owner
    required property Item editor
    hostItem: editor

    function popupAt(position: point): void {
        // Overlay popup dimensions do not inherit the editor's ancestor scale.
        // Read the real transform only when opening, without idle observers.
        const origin = editor.mapToItem(null, 0, 0);
        const unit = editor.mapToItem(null, 1, 0);
        const density = Math.hypot(unit.x - origin.x, unit.y - origin.y);
        const menu = create({
            parent: editor,
            uiScale: density
        }) as T.Menu;
        if (menu)
            menu.popup(editor, position.x, position.y);
    }

    sourceComponent: Component {
        EchoMenu {
            id: menu
            objectName: "textEditingMenu"
            // Match song menus in the window overlay. A separate native popup
            // adds a rectangular window/shadow outside our rounded surface.
            popupType: Popup.Item
            ControlsImpl.UndoAction {
                objectName: "textMenuUndo"
                editor: owner.editor
                text: qsTr("撤销")
            }
            ControlsImpl.RedoAction {
                objectName: "textMenuRedo"
                editor: owner.editor
                text: qsTr("重做")
            }
            EchoMenuSeparator {
                uiScale: menu.uiScale
            }
            ControlsImpl.CutAction {
                objectName: "textMenuCut"
                editor: owner.editor
                text: qsTr("剪切")
            }
            ControlsImpl.CopyAction {
                objectName: "textMenuCopy"
                editor: owner.editor
                text: qsTr("复制")
            }
            ControlsImpl.PasteAction {
                objectName: "textMenuPaste"
                editor: owner.editor
                text: qsTr("粘贴")
            }
            ControlsImpl.DeleteAction {
                objectName: "textMenuDelete"
                editor: owner.editor
                text: qsTr("删除")
            }
            EchoMenuSeparator {
                uiScale: menu.uiScale
            }
            ControlsImpl.SelectAllAction {
                objectName: "textMenuSelectAll"
                editor: owner.editor
                text: qsTr("全选")
            }
        }
    }
}
