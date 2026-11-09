pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Window
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Theme
import EchoMusic.Playback
import EchoMusic.Shell

Window {
    id: window
    objectName: "desktopLyricWindow"
    required property FloatingPlayerController service
    required property PlaybackService playback
    readonly property FloatingLyricPreferences style: service.desktop
    readonly property bool vertical: style.layout === "vertical"
    readonly property bool controlsVisible: !service.locked && chrome.hovered
    readonly property real sideExtent: (width - 48 - transport.implicitWidth) / 2
    readonly property bool fullActions: vertical || sideExtent >= 146
    color: "transparent"
    title: qsTr("EchoMusicNext 桌面歌词")
    visible: true
    width: 720
    height: 200
    flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowDoesNotAcceptFocus
    Component.onCompleted: service.attachDesktop(window)
    Component.onDestruction: service.attachDesktop(null)
    Rectangle {
        id: panel
        anchors.fill: parent
        objectName: "desktopLyricPanel"
        radius: Theme.cardRadius
        color: window.controlsVisible ? "#99000000" : "transparent"
        Behavior on color {
            ColorAnimation {
                duration: Theme.store.motionEnabled ? 300 : 0
            }
        }
        HoverHandler {
            id: chrome
            enabled: !window.service.locked
        }
        MouseArea {
            anchors.fill: parent
            enabled: !window.service.locked
            onPressed: window.service.moveDesktop()
        }
        GridLayout {
            id: toolbar
            objectName: "desktopLyricToolbar"
            x: window.vertical ? parent.width - 44 : 12
            y: window.vertical ? 10 : 12
            width: window.vertical ? 36 : parent.width - 24
            height: window.vertical ? parent.height - 20 : 36
            columns: window.vertical ? 1 : 3
            rowSpacing: window.vertical ? 8 : 0
            columnSpacing: 12
            opacity: window.controlsVisible ? 1 : 0
            enabled: opacity > .05
            Behavior on opacity {
                NumberAnimation {
                    duration: Theme.store.motionEnabled ? 300 : 0
                }
            }
            // Equal side columns keep playback centered on the window,
            // independent of title length and trailing action visibility.
            Item {
                Layout.fillWidth: !window.vertical
                Layout.preferredWidth: window.vertical ? 36 : 1
                Layout.minimumWidth: 0
                Layout.preferredHeight: 1
                Layout.minimumHeight: 0
                Layout.fillHeight: true
                GridLayout {
                    anchors.fill: parent
                    columns: window.vertical ? 1 : 2
                    columnSpacing: 6
                    rowSpacing: 6
                    Label {
                        objectName: "desktopLyricTitle"
                        visible: !window.vertical
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.minimumWidth: 0
                        text: [window.playback.title, window.playback.artist].filter(value => value.length > 0).join(" - ")
                        textFormat: Text.PlainText
                        elide: Text.ElideRight
                        verticalAlignment: Text.AlignVCenter
                        leftPadding: 8
                        rightPadding: 8
                        font: Theme.font(14, 400)
                        color: "white"
                    }
                    GridLayout {
                        objectName: "desktopLyricOffsets"
                        visible: window.playback.lyrics.count > 0 && (window.vertical || window.sideExtent >= implicitWidth)
                        Layout.alignment: window.vertical ? Qt.AlignTop | Qt.AlignHCenter : Qt.AlignVCenter
                        columns: window.vertical ? 1 : 3
                        columnSpacing: 6
                        rowSpacing: 6
                        Repeater {
                            model: [-1, 1, 0]
                            delegate: DesktopLyricButton {
                                required property int modelData
                                compact: window.vertical
                                objectName: "desktopLyricOffset-" + modelData
                                visible: modelData !== 0 || window.playback.lyricText.trackOffsetMs !== 0
                                implicitWidth: window.vertical ? 30 : modelData === 0 ? 29 : 30
                                implicitHeight: implicitWidth
                                padding: 6
                                glyph: modelData < 0 ? "rotate-2" : modelData > 0 ? "rotate-clockwise-2" : "refresh"
                                text: modelData < 0 ? qsTr("歌词后退 %1 秒").arg(window.playback.lyricText.offsetStep) : modelData > 0 ? qsTr("歌词前进 %1 秒").arg(window.playback.lyricText.offsetStep) : qsTr("重置单曲偏移")
                                tooltipText: text
                                onClicked: modelData === 0 ? window.playback.lyricText.resetTrackOffset() : window.playback.lyricText.adjustTrackOffset(modelData)
                            }
                        }
                    }
                }
            }

            GridLayout {
                id: transport
                objectName: "desktopLyricTransport"
                Layout.fillWidth: false
                visible: window.vertical || window.width >= 240
                Layout.alignment: Qt.AlignCenter
                columns: window.vertical ? 1 : 3
                rowSpacing: 6
                columnSpacing: 6
                DesktopLyricButton {
                    compact: window.vertical
                    visible: window.vertical || window.width >= 360
                    glyph: "player-skip-back"
                    text: qsTr("上一首")
                    onClicked: window.playback.previous()
                }
                DesktopLyricButton {
                    compact: window.vertical
                    objectName: "desktopLyricPlay"
                    glyph: window.playback.playing ? "pause" : "play"
                    animatePlayPause: true
                    text: window.playback.playing ? qsTr("暂停") : qsTr("播放")
                    onClicked: window.playback.toggle()
                }
                DesktopLyricButton {
                    compact: window.vertical
                    visible: window.vertical || window.width >= 360
                    glyph: "player-skip-forward"
                    text: qsTr("下一首")
                    onClicked: window.playback.next()
                }
            }
            Item {
                Layout.fillWidth: !window.vertical
                Layout.preferredWidth: window.vertical ? 36 : 1
                Layout.minimumWidth: 0
                Layout.preferredHeight: 1
                Layout.minimumHeight: 0
                Layout.fillHeight: true
                GridLayout {
                    objectName: "desktopLyricWindowActions"
                    x: window.vertical ? (parent.width - width) / 2 : parent.width - width
                    y: window.vertical ? parent.height - height : (parent.height - height) / 2
                    columns: window.vertical ? 1 : 5
                    rowSpacing: 6
                    columnSpacing: 6
                    DesktopLyricButton {
                        id: sourceAction
                        compact: window.vertical
                        objectName: "desktopLyricSource"
                        visible: window.fullActions
                        glyph: "list"
                        text: qsTr("选择歌词")
                        tooltipText: text
                        onClicked: window.service.selectLyrics()
                    }
                    DesktopLyricTextToggles {
                        objectName: "desktopLyricTextToggles"
                        playback: window.playback
                        vertical: window.vertical
                        visible: count > 0 && (window.vertical || window.sideExtent >= 152 + count * 32)
                    }
                    DesktopLyricButton {
                        compact: window.vertical
                        objectName: "desktopLyricSettings"
                        visible: window.fullActions
                        glyph: "settings"
                        padding: window.vertical ? 5.5 : 6.5
                        text: qsTr("桌面歌词设置")
                        tooltipText: text
                        onClicked: window.service.openSettings()
                    }
                    DesktopLyricButton {
                        id: lockAction
                        compact: window.vertical
                        objectName: "desktopLyricLock"
                        Component.onCompleted: window.service.attachUnlockButton(lockAction)
                        Component.onDestruction: window.service.attachUnlockButton(null)
                        visible: window.vertical || window.sideExtent >= 70
                        glyph: "lock"
                        text: qsTr("锁定桌面歌词")
                        tooltipText: qsTr("锁定后鼠标穿透，从解锁按钮或托盘解锁")
                        onClicked: window.service.locked = true
                    }
                    DesktopLyricButton {
                        compact: window.vertical
                        objectName: "desktopLyricClose"
                        glyph: "x"
                        text: qsTr("关闭桌面歌词")
                        onClicked: window.service.desktopEnabled = false
                    }
                }
            }
        }
        FloatingLyricViewport {
            objectName: "desktopLyricViewport"
            x: window.vertical ? 16 : 24
            y: window.vertical ? 18 : 64
            width: Math.max(0, parent.width - x - (window.vertical ? 62 : 24))
            height: Math.max(0, parent.height - y - (window.vertical ? 18 : 12))
            contentInset: 0
            sourceSpacing: true
            clip: true
            playback: window.playback
            windowSize: Qt.size(window.width, window.height)
            resourceActive: window.visible && window.visibility !== Window.Minimized
            vertical: window.vertical
            alignment: window.style.alignment
            nextLinePreview: window.style.showNextLinePreview
            bold: window.style.bold
            shadowStrength: window.style.shadowStrength
            playedColor: window.style.followCoverColor ? Theme.store.lyricAccent(window.playback.coverColor) : window.style.playedColor
            secondaryColor: LyricColors.secondary(window.style, Theme.store.lyricAccent(window.playback.coverColor), window.style.unplayedFollowCoverColor ? Theme.store.lyricAccent(window.playback.coverColor) : window.style.unplayedColor)
            unplayedColor: window.style.unplayedFollowCoverColor ? Theme.store.lyricAccent(window.playback.coverColor) : window.style.unplayedColor
        }
        // Native edge resize retains the platform's drag/scale behavior on Wayland too.
        Repeater {
            model: [Qt.LeftEdge | Qt.TopEdge, Qt.TopEdge, Qt.RightEdge | Qt.TopEdge, Qt.LeftEdge, Qt.RightEdge, Qt.LeftEdge | Qt.BottomEdge, Qt.BottomEdge, Qt.RightEdge | Qt.BottomEdge]
            delegate: MouseArea {
                id: edge
                required property int modelData
                readonly property bool edgeLeft: (modelData & Qt.LeftEdge) !== 0
                readonly property bool edgeRight: (modelData & Qt.RightEdge) !== 0
                readonly property bool edgeTop: (modelData & Qt.TopEdge) !== 0
                readonly property bool edgeBottom: (modelData & Qt.BottomEdge) !== 0
                x: edgeRight ? panel.width - 6 : 0
                y: edgeBottom ? panel.height - 6 : 0
                width: edgeLeft || edgeRight ? 6 : panel.width
                height: edgeTop || edgeBottom ? 6 : panel.height
                enabled: !window.service.locked
                cursorShape: edgeLeft && edgeTop || edgeRight && edgeBottom ? Qt.SizeFDiagCursor : edgeRight && edgeTop || edgeLeft && edgeBottom ? Qt.SizeBDiagCursor : edgeLeft || edgeRight ? Qt.SizeHorCursor : Qt.SizeVerCursor
                onPressed: window.service.resizeDesktop(modelData)
            }
        }
    }
}
