pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Controls
import EchoMusic.Theme

ColumnLayout {
    id: cropper
    required property size viewport
    readonly property size dimensions: Theme.store.backgroundSize
    readonly property real imageScale: dimensions.width > 0 && dimensions.height > 0 ? Math.max(0, Math.min((canvas.width - 32) / dimensions.width, (canvas.height - 32) / dimensions.height)) : 0
    readonly property rect selection: {
        const stored = Theme.store.backgroundCrop;
        if (stored.width > 0 && stored.height > 0)
            return stored;
        const factor = dimensions.width > 0 && dimensions.height > 0 ? Math.max((viewport.width * 1.1 + 2) / dimensions.width, (viewport.height * 1.1 + 2) / dimensions.height) : 1;
        const w = Math.min(dimensions.width, viewport.width / factor);
        const h = Math.min(dimensions.height, viewport.height / factor);
        return Qt.rect((dimensions.width - w) / 2, (dimensions.height - h) / 2, w, h);
    }
    function commitSelection(): void {
        Theme.store.setBackgroundCrop(selection);
    }
    function selectWholeImage(): void {
        Theme.store.setBackgroundCrop(Qt.rect(0, 0, dimensions.width, dimensions.height));
    }
    function changeSelection(original: rect, corner: int, dx: real, dy: real): void {
        if (imageScale <= 0)
            return;
        dx /= imageScale;
        dy /= imageScale;
        let x = original.x;
        let y = original.y;
        let r = x + original.width;
        let b = y + original.height;
        if (corner < 0) {
            x = Math.max(0, Math.min(dimensions.width - original.width, x + dx));
            y = Math.max(0, Math.min(dimensions.height - original.height, y + dy));
            r = x + original.width;
            b = y + original.height;
        } else {
            const minW = Math.min(dimensions.width, 24 / imageScale);
            const minH = Math.min(dimensions.height, 24 / imageScale);
            if (corner === 1 || corner === 2)
                r = Math.max(x + Math.min(minW, dimensions.width - x), Math.min(dimensions.width, r + dx));
            else
                x = Math.max(0, Math.min(r - Math.min(minW, r), x + dx));
            if (corner === 2 || corner === 3)
                b = Math.max(y + Math.min(minH, dimensions.height - y), Math.min(dimensions.height, b + dy));
            else
                y = Math.max(0, Math.min(b - Math.min(minH, b), y + dy));
        }
        Theme.store.setBackgroundCrop(Qt.rect(x, y, r - x, b - y));
    }
    function keyboard(event: KeyEvent, corner: int): void {
        const step = event.modifiers & Qt.ShiftModifier ? 24 : 6;
        if (event.key === Qt.Key_Left)
            changeSelection(selection, corner, -step, 0);
        else if (event.key === Qt.Key_Right)
            changeSelection(selection, corner, step, 0);
        else if (event.key === Qt.Key_Up)
            changeSelection(selection, corner, 0, -step);
        else if (event.key === Qt.Key_Down)
            changeSelection(selection, corner, 0, step);
        else
            return;
        event.accepted = true;
    }
    spacing: 8
    Item {
        id: canvas
        Layout.fillWidth: true
        Layout.fillHeight: true
        Image {
            id: artwork
            objectName: "backgroundCropArtwork"
            x: (canvas.width - width) / 2
            y: (canvas.height - height) / 2
            width: cropper.dimensions.width * cropper.imageScale
            height: cropper.dimensions.height * cropper.imageScale
            source: Theme.store.backgroundImage
            sourceSize: cropper.dimensions
            asynchronous: true
            fillMode: Image.Stretch
            Rectangle {
                anchors.fill: parent
                color: Theme.shell
                opacity: Theme.store.backgroundShade / 100
            }
            // Four masks preserve a stationary source image while the selection
            // moves. No expensive full-window blur or offscreen copies on drag.
            Rectangle {
                width: parent.width
                height: selectionBox.y
                color: "#7a000000"
            }
            Rectangle {
                y: selectionBox.y + selectionBox.height
                width: parent.width
                height: Math.max(0, parent.height - y)
                color: "#7a000000"
            }
            Rectangle {
                y: selectionBox.y
                width: selectionBox.x
                height: selectionBox.height
                color: "#7a000000"
            }
            Rectangle {
                x: selectionBox.x + selectionBox.width
                y: selectionBox.y
                width: Math.max(0, parent.width - x)
                height: selectionBox.height
                color: "#7a000000"
            }
            Rectangle {
                id: selectionBox
                objectName: "backgroundCropSelection"
                x: cropper.selection.x * cropper.imageScale
                y: cropper.selection.y * cropper.imageScale
                width: cropper.selection.width * cropper.imageScale
                height: cropper.selection.height * cropper.imageScale
                color: "transparent"
                EchoDashedFrame {
                    anchors.fill: parent
                    solid: true
                    strokeColor: selectionBox.activeFocus ? Theme.text : Theme.accent
                    thickness: 2
                }
                enabled: !Theme.store.imageImporting && artwork.status === Image.Ready
                activeFocusOnTab: true
                Accessible.name: qsTr("背景裁切框，方向键调整，Shift 加快移动")
                Keys.onPressed: event => cropper.keyboard(event, -1)
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.ArrowCursor
                    property point origin
                    property rect original
                    onPressed: mouse => {
                        parent.forceActiveFocus();
                        origin = mapToItem(canvas, mouse.x, mouse.y);
                        original = cropper.selection;
                    }
                    onPositionChanged: mouse => {
                        if (!pressed)
                            return;
                        const point = mapToItem(canvas, mouse.x, mouse.y);
                        cropper.changeSelection(original, -1, point.x - origin.x, point.y - origin.y);
                    }
                }
                Repeater {
                    model: 4
                    delegate: Item {
                        id: corner
                        required property int index
                        width: 24
                        height: 24
                        x: (index === 1 || index === 2 ? selectionBox.width : 0) - 12
                        y: (index >= 2 ? selectionBox.height : 0) - 12
                        activeFocusOnTab: true
                        Accessible.name: [qsTr("调整裁切框左上角"), qsTr("调整裁切框右上角"), qsTr("调整裁切框右下角"), qsTr("调整裁切框左下角")][index]
                        Keys.onPressed: event => cropper.keyboard(event, index)
                        Rectangle {
                            anchors.centerIn: parent
                            width: 10
                            height: 10
                            radius: 2
                            color: "white"
                            EchoDashedFrame {
                                anchors.fill: parent
                                solid: true
                                strokeColor: Theme.accent
                                radius: 2
                                thickness: corner.activeFocus ? 2 : 1
                            }
                        }
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: corner.index % 2 === 0 ? Qt.SizeFDiagCursor : Qt.SizeBDiagCursor
                            property point origin
                            property rect original
                            onPressed: mouse => {
                                corner.forceActiveFocus();
                                origin = mapToItem(canvas, mouse.x, mouse.y);
                                original = cropper.selection;
                            }
                            onPositionChanged: mouse => {
                                if (!pressed)
                                    return;
                                const point = mapToItem(canvas, mouse.x, mouse.y);
                                cropper.changeSelection(original, corner.index, point.x - origin.x, point.y - origin.y);
                            }
                        }
                    }
                }
            }
        }
    }
    Label {
        text: qsTr("拖动裁切框选择区域，拖动四角调整大小")
        font: Theme.font(12, Theme.regularWeight)
        color: Theme.secondaryText
        wrapMode: Text.Wrap
        Layout.fillWidth: true
    }
}
