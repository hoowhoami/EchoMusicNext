pragma ComponentBehavior: Bound
import QtQuick

// Qt owns cursor placement/height and IME. The nominal one-DIP caret is
// quantized onto the window's device-pixel grid before painting.
Item {
    id: cursor
    objectName: "textCursor"
    required property var editor
    // Empty TextEdit uses the base font for its caret, while a translated
    // placeholder can use a fallback font with a different ascent.
    property real emptyLineBaseline: NaN
    width: 1
    // Observe the delegate, not the painted child: snapping must not feed back
    // into the native cursor geometry. Retire observers when editing stops.
    EchoPopupAnchor {
        id: geometry
        item: glyph.visible ? cursor : null
        coordinateItem: item && item.Window.window ? item.Window.window.contentItem : null
    }
    readonly property real dpr: Screen.devicePixelRatio > 0 ? Screen.devicePixelRatio : 1
    readonly property real scaleX: geometry.available && width > 0 ? geometry.rect.width / width : 1
    readonly property real scaleY: geometry.available && height > 0 ? geometry.rect.height / height : 1
    readonly property real densityX: Math.max(0.001, dpr * scaleX)
    readonly property real densityY: Math.max(0.001, dpr * scaleY)
    readonly property real lineTop: !editor.length && !editor.preeditText && Number.isFinite(emptyLineBaseline) ? emptyLineBaseline - editor.cursorRectangle.y - metrics.ascent : 0
    FontMetrics {
        id: metrics
        font: cursor.editor.font
    }
    Rectangle {
        id: glyph
        objectName: "textCursorGlyph"
        // A one-DIP caret becomes 1.25 device pixels at Windows 125%. Quantize
        // its width as well as its origin so different character positions do
        // not distribute the same stroke across different pixel columns.
        x: geometry.available ? (Math.round(geometry.rect.x * cursor.dpr) - geometry.rect.x * cursor.dpr) / cursor.densityX : 0
        y: geometry.available ? cursor.lineTop + (Math.round((geometry.rect.y + cursor.lineTop * cursor.scaleY) * cursor.dpr) - (geometry.rect.y + cursor.lineTop * cursor.scaleY) * cursor.dpr) / cursor.densityY : cursor.lineTop
        width: Math.max(1, Math.round(cursor.width * cursor.densityX)) / cursor.densityX
        height: Math.max(1, Math.round(cursor.height * cursor.densityY)) / cursor.densityY
        color: cursor.editor.color
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
