pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Controls
import EchoMusic.Theme
import EchoMusic.Library

ColumnLayout {
    id: field
    required property PlaylistEditModel editor
    property var values: []
    function tags(text: string): var {
        return text.split(/[,，\r\n]+/).map(value => value.trim()).filter(value => value.length > 0);
    }
    function publish(): void {
        editor.tags = values.concat(tags(input.text)).filter((value, index, all) => all.indexOf(value) === index).join(",");
    }
    function commit(): void {
        if (input.inputMethodComposing)
            return;
        values = values.concat(tags(input.text)).filter((value, index, all) => all.indexOf(value) === index);
        input.clear();
        publish();
    }
    Connections {
        target: field.editor
        function onChanged(): void {
            if (!field.editor.loaded) {
                field.values = [];
                input.clear();
            } else if (!input.activeFocus && field.values.length === 0 && input.text.length === 0) {
                field.values = field.tags(field.editor.tags);
            }
        }
    }
    spacing: 5
    Rectangle {
        Layout.fillWidth: true
        implicitHeight: Math.max(36, flow.implicitHeight + 10)
        color: Theme.floating.mutedSurface
        radius: Theme.controlRadius
        border.width: 1
        border.color: input.activeFocus ? Theme.withAlpha(Theme.floating.accent, 0.35) : "transparent"
        Flow {
            id: flow
            x: 8
            y: 5
            width: Math.max(0, parent.width - 16)
            spacing: 5
            readonly property Item lastChip: chips.count > 0 ? chips.itemAt(chips.count - 1) : null
            readonly property real remaining: width - (lastChip ? lastChip.x + lastChip.width + spacing : 0)
            Repeater {
                id: chips
                model: field.values
                delegate: Rectangle {
                    id: chip
                    required property string modelData
                    required property int index
                    width: Math.min(flow.width, row.implicitWidth + 12)
                    height: 24
                    radius: Theme.controlRadius
                    color: Theme.floating.hover
                    RowLayout {
                        id: row
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        anchors.rightMargin: 4
                        spacing: 4
                        EchoTextTagRow {
                            Layout.fillWidth: true
                            text: chip.modelData
                            color: Theme.floatingText
                            font: Theme.font(12, Theme.regularWeight)
                        }
                        EchoIconButton {
                            Layout.preferredWidth: 12
                            Layout.preferredHeight: 12
                            padding: 0
                            glyph: "x"
                            hoverBackground: false
                            colors: Theme.floating
                            text: qsTr("删除标签 %1").arg(chip.modelData)
                            onClicked: {
                                field.values = field.values.filter((value, at) => at !== chip.index);
                                field.publish();
                                input.forceActiveFocus();
                            }
                        }
                    }
                }
            }
            EchoTextField {
                id: input
                objectName: "playlistEditTagsInput"
                width: flow.remaining >= 100 ? flow.remaining : flow.width
                height: 24
                implicitHeight: 24
                padding: 0
                leftPadding: 0
                rightPadding: 0
                colors: Theme.floating
                background: null
                placeholderText: qsTr("输入标签，按回车添加")
                Accessible.name: qsTr("标签")
                onTextEdited: {
                    if (!inputMethodComposing && /[,，\r\n]/.test(text))
                        field.commit();
                    else
                        field.publish();
                }
                onAccepted: field.commit()
                onActiveFocusChanged: if (!activeFocus)
                    field.commit()
                Keys.onPressed: event => {
                    if (event.key === Qt.Key_Backspace && !text.length && !inputMethodComposing && field.values.length) {
                        field.values = field.values.slice(0, -1);
                        field.publish();
                        event.accepted = true;
                    }
                }
            }
        }
    }
    Label {
        text: qsTr("回车或逗号添加，可粘贴多个标签")
        color: Theme.floatingSecondaryText
        font: Theme.font(12, Theme.regularWeight)
    }
}
