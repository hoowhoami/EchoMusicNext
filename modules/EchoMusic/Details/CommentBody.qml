pragma ComponentBehavior: Bound
import QtQuick
import EchoMusic.Theme

TextEdit {
    id: body
    property string content: ""
    property bool expanded: false
    readonly property bool longContent: Array.from(content).length > 120
    signal toggleRequested
    function escaped(value: string): string {
        return value.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;").replace(/"/g, "&quot;");
    }
    readOnly: true
    selectByMouse: true
    wrapMode: TextEdit.Wrap
    textFormat: TextEdit.RichText
    color: Theme.text
    // Only the fixed action is markup. Comment content stays escaped, selectable
    // plain text; Qt lays out and wraps the inline action with the final line.
    text: {
        const shown = longContent && !expanded ? Array.from(content).slice(0, 120).join("") + "..." : content;
        const action = longContent ? "&#x2009;<a href='comment:toggle' style='text-decoration:none;font-weight:600;color:" + Theme.accentText + "'>" + (expanded ? qsTr("收起") : qsTr("展开")) + "</a>" : "";
        return "<span style='white-space:pre-wrap'>" + escaped(shown).replace(/\n/g, "<br>") + action + "</span>";
    }
    onLinkActivated: link => {
        if (link === "comment:toggle")
            toggleRequested();
    }
    CommentTextStyle {
        document: body.textDocument
        lineHeight: body.font.pixelSize * 1.75
    }
}
