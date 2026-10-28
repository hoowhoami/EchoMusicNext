pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import EchoMusic.Controls
import EchoMusic.Theme

Item {
    id: quote
    property string author: ""
    property string content: ""
    property bool resourceActive: true
    implicitHeight: Math.min(96, label.contentHeight + 12)
    onResourceActiveChanged: if (!resourceActive)
        viewport.cancelFlick()
    Flickable {
        id: viewport
        anchors.fill: parent
        clip: true
        contentWidth: width
        contentHeight: label.height + 12
        interactive: quote.resourceActive && contentHeight > height
        boundsBehavior: Flickable.StopAtBounds
        TextEdit {
            id: label
            objectName: "commentQuoteText"
            x: 12
            y: 6
            width: Math.max(0, viewport.width - 22)
            height: contentHeight
            readOnly: true
            selectByMouse: true
            textFormat: TextEdit.PlainText
            text: quote.author + "：" + quote.content
            wrapMode: TextEdit.Wrap
            color: Theme.secondaryText
            font: Theme.font(12, Theme.regularWeight)
            CommentTextStyle {
                document: label.textDocument
                lineHeight: 19.2
            }
        }
        ScrollBar.vertical: EchoScrollBar {
            resourceActive: quote.resourceActive && quote.visible
        }
    }
    Rectangle {
        width: 2
        height: parent.height
        // A quote marker needs contrast against the inset reply surface;
        // the general divider color can blend into that surface.
        color: Theme.withAlpha(Theme.text, .24)
    }
}
