pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import EchoMusic.Controls
import EchoMusic.Theme

Rectangle {
    id: row
    required property var file
    property bool editing: false
    property string draft: ""
    signal renameRequested
    signal saveRequested
    signal cancelRequested
    signal removeRequested
    signal draftEdited(string text)
    objectName: "settingsEffectFileRow"
    height: 52
    radius: Theme.itemRadius
    color: "transparent"
    EchoDashedFrame {
        anchors.fill: parent
        solid: true
        strokeColor: Theme.withAlpha(Theme.floating.text, .10)
        radius: Theme.itemRadius
    }
    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 11
        anchors.rightMargin: 11
        spacing: 8
        EchoTextTagRow {
            objectName: "settingsEffectFileName"
            visible: !row.editing
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            text: row.file.name
            font: Theme.bodyFont
            color: Theme.floating.text
        }
        EchoTextField {
            id: editor
            objectName: "settingsEffectFileNameInput"
            visible: row.editing
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            implicitHeight: 28
            leftPadding: 7
            rightPadding: 7
            topPadding: 0
            bottomPadding: 0
            colors: Theme.floating
            font: Theme.bodyFont
            maximumLength: 80
            text: row.draft
            onTextEdited: row.draftEdited(text)
            onVisibleChanged: if (visible)
                Qt.callLater(() => {
                    forceActiveFocus();
                    selectAll();
                })
            background: Rectangle {
                radius: Theme.controlRadius
                color: "transparent"
                EchoDashedFrame {
                    anchors.fill: parent
                    solid: true
                    strokeColor: Theme.withAlpha(Theme.floating.accent, .5)
                    radius: Theme.controlRadius
                }
            }
            Keys.onReturnPressed: row.saveRequested()
            Keys.onEnterPressed: row.saveRequested()
            Keys.onEscapePressed: event => {
                row.cancelRequested();
                event.accepted = true;
            }
        }
        EchoIconButton {
            objectName: "settingsEffectSaveName"
            visible: row.editing
            glyph: "check"
            text: qsTr("保存名称")
            implicitWidth: 14
            implicitHeight: 14
            padding: 0
            colors: Theme.floating
            hoverBackground: false
            foreground: hovered ? Theme.floating.accentText : Theme.floating.secondaryText
            onClicked: row.saveRequested()
        }
        EchoIconButton {
            objectName: "settingsEffectRename"
            glyph: row.editing ? "x" : "pencil"
            text: row.editing ? qsTr("取消") : qsTr("重命名")
            implicitWidth: 14
            implicitHeight: 14
            padding: 0
            colors: Theme.floating
            hoverBackground: false
            foreground: hovered ? Theme.floating.accentText : Theme.floating.secondaryText
            onClicked: row.editing ? row.cancelRequested() : row.renameRequested()
        }
        EchoIconButton {
            objectName: "settingsEffectRemove"
            glyph: "trash"
            text: qsTr("移除")
            implicitWidth: 14
            implicitHeight: 14
            padding: 0
            colors: Theme.floating
            hoverBackground: false
            foreground: hovered ? Theme.floating.dangerText : Theme.floating.secondaryText
            onClicked: row.removeRequested()
        }
    }
}
