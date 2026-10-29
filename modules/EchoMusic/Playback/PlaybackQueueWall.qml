// Shared virtual view of the real playback queue. No mirrored song state.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Playback
import EchoMusic.Controls
import EchoMusic.Theme

Popup {
    id: wall
    objectName: "playbackQueueWall"
    required property PlaybackService playback
    property real uiScale: 1
    property bool _closing: false
    property bool _refreshingForReopen: false
    onAboutToHide: _closing = true
    onAboutToShow: {
        // Exit owns opacity; a canceled or completed exit cannot dim a new open.
        opacity = 1;
        if (_closing && skinHost.available) {
            _refreshingForReopen = true;
            freshScope.restart();
        }
        _closing = false;
    }
    onClosed: {
        freshScope.stop();
        _closing = false;
        _refreshingForReopen = false;
    }
    Timer {
        id: freshScope
        interval: 0
        onTriggered: if (!wall._closing) {
            wall.opacity = 1;
            wall._refreshingForReopen = false;
        }
    }
    readonly property bool coversPlayback: opened && skinHost.loaded
    parent: Overlay.overlay
    width: parent ? parent.width / uiScale : 0
    height: parent ? parent.height / uiScale : 0
    scale: uiScale
    transformOrigin: Popup.TopLeft
    padding: skinHost.available ? 0 : 32
    modal: true
    dim: true
    focus: true
    closePolicy: skinHost.loaded ? Popup.NoAutoClose : Popup.CloseOnEscape
    exit: Transition {
        enabled: skinHost.available && Theme.store.motionEnabled
        NumberAnimation {
            property: "opacity"
            from: 1
            to: 0
            duration: 620
            easing.type: Easing.InCubic
        }
    }
    onOpened: Qt.callLater(function () {
        if (wall && grid.count && wall.playback.queue.currentIndex >= 0)
            grid.positionViewAtIndex(wall.playback.queue.currentIndex, GridView.Center);
    })
    EchoPopupLifecycle {
        surface: wall
    }
    background: Rectangle {
        color: Theme.main
    }
    contentItem: Item {
        AppearanceHost {
            id: skinHost
            objectName: "pluginQueueWallHost"
            anchors.fill: parent
            entry: Theme.store.extensions.skin
            placement: "queue-wall"
            resourceActive: wall.visible && !wall._refreshingForReopen
            onActionRequested: (action, arguments) => {
                if (action === "close")
                    wall.close();
            }
        }
        Label {
            anchors.centerIn: parent
            visible: skinHost.available && !skinHost.loaded
            text: qsTr("正在加载歌曲墙…")
            color: Theme.text
        }
        ColumnLayout {
            anchors.fill: parent
            visible: !skinHost.available
            spacing: 24
            RowLayout {
                Layout.fillWidth: true
                Label {
                    text: qsTr("歌曲墙") + " · " + wall.playback.queueTitle
                    textFormat: Text.PlainText
                    color: Theme.text
                    font: Theme.font(24, Theme.boldWeight)
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
                EchoIconButton {
                    objectName: "queueWallLocate"
                    colors: Theme.colors
                    glyph: "current-location"
                    text: qsTr("定位正在播放")
                    enabled: wall.playback.queue.currentIndex >= 0
                    onClicked: grid.positionViewAtIndex(wall.playback.queue.currentIndex, GridView.Center)
                }
                EchoIconButton {
                    objectName: "queueWallClose"
                    colors: Theme.colors
                    glyph: "x"
                    text: qsTr("关闭歌曲墙")
                    onClicked: wall.close()
                }
            }
            GridView {
                id: grid
                objectName: "queueWallGrid"
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                cellWidth: width / Math.max(1, Math.floor(width / 180))
                cellHeight: cellWidth + 64
                model: wall.visible && !skinHost.available ? wall.playback.queue : null
                reuseItems: true
                cacheBuffer: cellHeight
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: EchoScrollBar {
                    hoverTarget: grid
                    wheelView: grid
                    resourceActive: wall.visible && !wall._refreshingForReopen
                }
                delegate: Item {
                    id: tile
                    required property int index
                    required property string title
                    required property string artistText
                    required property string coverUrl
                    required property bool isCurrent
                    property bool pooled: false
                    width: grid.cellWidth
                    height: grid.cellHeight
                    GridView.onPooled: pooled = true
                    GridView.onReused: pooled = false
                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: 6
                        radius: 12
                        color: tile.isCurrent ? Theme.withAlpha(Theme.accent, .16) : "transparent"
                        border.color: tile.isCurrent ? Theme.accent : "transparent"
                    }
                    Image {
                        id: cover
                        x: 14
                        y: 14
                        width: parent.width - 28
                        height: width
                        source: wall.visible && !tile.pooled ? tile.coverUrl : ""
                        sourceSize.width: 512
                        sourceSize.height: 512
                        asynchronous: true
                        cache: false
                        fillMode: Image.PreserveAspectCrop
                        Rectangle {
                            anchors.fill: parent
                            color: Theme.elevated
                            z: -1
                        }
                    }
                    Label {
                        x: 14
                        y: cover.y + cover.height + 10
                        width: parent.width - 28
                        text: tile.title
                        textFormat: Text.PlainText
                        color: Theme.text
                        elide: Text.ElideRight
                        font: Theme.font(14, Theme.semiboldWeight)
                    }
                    Label {
                        x: 14
                        y: cover.y + cover.height + 32
                        width: parent.width - 28
                        text: tile.artistText
                        textFormat: Text.PlainText
                        color: Theme.secondaryText
                        elide: Text.ElideRight
                        font: Theme.font(12, Theme.regularWeight)
                    }
                    TapHandler {
                        enabled: wall.visible && !tile.pooled
                        onTapped: if (tile.isCurrent)
                            wall.playback.toggle()
                        else
                            wall.playback.playIndex(tile.index)
                    }
                }
            }
            Label {
                visible: grid.count === 0
                text: qsTr("当前暂无队列")
                color: Theme.secondaryText
            }
        }
    }
}
