pragma ComponentBehavior: Bound
import QtQuick

// Qt owns cursor placement/height and IME. Match its Material cursor's system
// flash interval while keeping a single logical-pixel caret for every editor.
Item {
    id: cursor
    objectName: "textCursor"
    required property var editor
    // Empty TextEdit uses the base font for its caret, while a translated
    // placeholder can use a fallback font with a different ascent.
    property real emptyLineBaseline: NaN
    width: 1
    FontMetrics {
        id: metrics
        font: cursor.editor.font
    }
    Rectangle {
        id: glyph
        objectName: "textCursorGlyph"
        width: cursor.width
        height: cursor.height
        color: cursor.editor.color
        y: !cursor.editor.length && !cursor.editor.preeditText && Number.isFinite(cursor.emptyLineBaseline) ? cursor.emptyLineBaseline - cursor.editor.cursorRectangle.y - metrics.ascent : 0
        // Qt writes visibility on the delegate root. Keep the editing-state
        // gate on the painted child so a native update cannot bypass it.
        visible: cursor.editor.activeFocus && cursor.editor.cursorVisible && cursor.editor.enabled && cursor.editor.visible && cursor.editor.Window.active && !cursor.editor.readOnly && cursor.editor.selectionStart === cursor.editor.selectionEnd
    }
    Connections {
        target: cursor.editor
        function onCursorPositionChanged() {
            glyph.opacity = 1;
            if (blink.running)
                blink.restart();
        }
    }
    Timer {
        id: blink
        interval: Application.styleHints.cursorFlashTime / 2
        repeat: true
        running: cursor.visible && glyph.visible && interval > 0
        onTriggered: glyph.opacity = glyph.opacity ? 0 : 1
        onRunningChanged: glyph.opacity = 1
    }
}
