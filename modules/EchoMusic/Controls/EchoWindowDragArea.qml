pragma ComponentBehavior: Bound
import QtQuick

MouseArea {
    id: area
    signal moveRequested
    signal doubleClickRequested
    // Disabled controls and the gaps inside a control group remain client input.
    property list<Item> excludedItems: []
    property point pressPosition
    property bool moveStarted: false
    // Fullscreen is a fixed presentation. Do not generate system move or
    // double-click restore requests from any host caption surface.
    enabled: area.Window.window !== null && area.Window.window.visibility !== Window.FullScreen
    acceptedButtons: Qt.LeftButton
    onPressed: mouse => {
        for (const item of excludedItems) {
            if (!item || !item.visible)
                continue;
            const point = mapToItem(item, mouse.x, mouse.y);
            if (point.x >= 0 && point.y >= 0 && point.x < item.width && point.y < item.height) {
                mouse.accepted = false;
                return;
            }
        }
        pressPosition = mapToItem(null, mouse.x, mouse.y);
        moveStarted = false;
        // Windows' modal system-move loop consumes the click sequence if it
        // starts on press. Wait for a real drag so double clicks reach QML.
        if (Qt.platform.os !== "windows")
            moveRequested();
    }
    onPositionChanged: mouse => {
        if (Qt.platform.os !== "windows" || !pressed || moveStarted)
            return;
        const position = mapToItem(null, mouse.x, mouse.y);
        const distance = Math.hypot(position.x - pressPosition.x, position.y - pressPosition.y);
        if (distance >= Qt.styleHints.startDragDistance) {
            moveStarted = true;
            moveRequested();
        }
    }
    onDoubleClicked: doubleClickRequested()
}
