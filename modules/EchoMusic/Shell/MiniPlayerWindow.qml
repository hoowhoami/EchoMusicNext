pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Window
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Controls
import EchoMusic.Theme
import EchoMusic.Playback
import EchoMusic.Shell

Window {
    id: window
    objectName: "miniPlayerWindow"
    required property FloatingPlayerController service
    property PluginSurfaceModel pluginSurfaces: null
    required property PlaybackService playback
    readonly property FloatingLyricPreferences style: service.mini
    readonly property bool showControls: hover.hovered || activeFocusItem !== null && activeFocusItem !== contentItem
    readonly property bool live: visible && visibility !== Window.Minimized
    onLiveChanged: if (!live)
        volumePopupOwner.close()
    color: "transparent"
    title: qsTr("EchoMusicNext Mini")
    width: 360
    height: 64
    visible: true
    flags: Qt.Tool | Qt.FramelessWindowHint
    Component.onCompleted: service.attachMini(window)
    Component.onDestruction: service.attachMini(null)
    Shortcut {
        sequence: "Space"
        onActivated: window.playback.toggle()
    }
    Shortcut {
        sequence: "Escape"
        onActivated: window.service.panel.length ? window.service.setPanel("") : window.service.showMain()
    }
    Rectangle {
        id: card
        objectName: "miniPlayerCard"
        property real expansion: window.service.panel.length ? 1 : 0
        x: 0
        y: window.service.expandsUp ? window.height - height : 0
        width: parent.width
        height: 64 + 296 * expansion
        radius: 16
        color: Theme.floating.elevated
        border.width: 1
        border.color: Theme.floatingBorder
        clip: true
        Behavior on expansion {
            NumberAnimation {
                duration: Theme.store.motionEnabled ? 260 : 0
                easing.type: Easing.OutCubic
            }
        }
        PluginSurfaceHost {
            anchors.left: parent.left
            anchors.right: parent.right
            height: 64
            y: window.service.expandsUp ? card.height - 64 : 0
            model: window.pluginSurfaces
            target: "mini.background"
        }
        HoverHandler {
            id: hover
        }
        MouseArea {
            anchors.fill: parent
            onPressed: window.service.moveMini()
        }
        Item {
            id: expanded
            objectName: "miniPlayerPanel"
            x: 1
            y: window.service.expandsUp ? 1 : 64
            width: parent.width - 2
            height: 295
            opacity: card.expansion
            visible: opacity > 0
            enabled: card.expansion > .99 && window.service.panel.length > 0
            Loader {
                id: backdrop
                objectName: "miniLyricBackdropLoader"
                anchors.fill: parent
                active: window.live && window.style.backgroundBlur && window.service.panel === "lyric"
                sourceComponent: MiniLyricBackdrop {
                    coverSource: window.playback.cover
                    baseColor: Theme.floating.elevated
                    cornerRadius: card.radius - 1
                    roundTop: window.service.expandsUp
                }
            }
            RowLayout {
                id: panelHeader
                x: 12
                y: 4
                width: parent.width - 24
                height: 32
                spacing: 4
                Label {
                    Layout.fillWidth: true
                    text: window.service.panel === "queue" ? window.playback.queueTitle : qsTr("歌词")
                    textFormat: Text.PlainText
                    font: Theme.font(12, 600)
                    color: Theme.floatingSecondaryText
                    elide: Text.ElideRight
                }
                EchoSoftButton {
                    visible: window.service.panel === "lyric"
                    size: EchoControlSize.Small
                    text: qsTr("译")
                    colors: Theme.floating
                    enabled: window.playback.lyrics.hasTranslation
                    accent: window.playback.lyricText.wantTranslation
                    onClicked: window.playback.lyricText.wantTranslation = !window.playback.lyricText.wantTranslation
                }
                EchoSoftButton {
                    visible: window.service.panel === "lyric"
                    size: EchoControlSize.Small
                    text: qsTr("音")
                    colors: Theme.floating
                    enabled: window.playback.lyrics.hasRomanization
                    accent: window.playback.lyricText.wantRomanization
                    onClicked: window.playback.lyricText.wantRomanization = !window.playback.lyricText.wantRomanization
                }
                EchoIconButton {
                    id: lyricMenuAction
                    objectName: "miniPlayerLyricMenuAction"
                    implicitWidth: 28
                    implicitHeight: 28
                    padding: 7
                    glyph: "dots"
                    text: qsTr("歌词操作")
                    colors: Theme.floating
                    onClicked: lyricMenuOwner.popup(lyricMenuAction, 0, lyricMenuAction.height)
                }
                EchoIconButton {
                    implicitWidth: 28
                    implicitHeight: 28
                    padding: 7
                    glyph: "settings"
                    text: qsTr("歌词设置")
                    colors: Theme.floating
                    onClicked: window.service.openSettings()
                }
                EchoIconButton {
                    implicitWidth: 28
                    implicitHeight: 28
                    padding: 7
                    glyph: "chevron-down"
                    text: qsTr("收起面板")
                    colors: Theme.floating
                    onClicked: window.service.setPanel("")
                }
            }
            Item {
                id: timeline
                x: 0
                y: 38
                width: parent.width
                height: parent.height - 42
                visible: window.service.panel === "lyric"
                enabled: visible
                onVisibleChanged: if (visible)
                    observe(true)
                property real renderTime: window.playback.positionMs
                LyricClock {
                    id: clock
                }
                function observe(reset: bool): void {
                    clock.observe(window.playback.positionMs, window.playback.playing, window.playback.speed, reset);
                    renderTime = clock.sample();
                }
                Component.onCompleted: observe(true)
                Connections {
                    target: window.playback
                    function onPositionChanged(): void {
                        if (timeline.visible)
                            timeline.observe(false);
                    }
                    function onChanged(): void {
                        clock.setPlaybackState(window.playback.playing, window.playback.speed);
                        timeline.renderTime = clock.sample();
                    }
                    function onTimelineReset(): void {
                        timeline.observe(true);
                    }
                }
                FrameAnimation {
                    running: timeline.visible && window.live && window.playback.playing
                    onTriggered: timeline.renderTime = clock.sample()
                }
                CoverLyricViewport {
                    objectName: "miniLyricViewport"
                    baseFont: Theme.store.miniLyricBaseFont
                    anchors.fill: parent
                    playback: window.playback
                    colors: Theme.floating
                    renderTime: timeline.renderTime
                    resourceActive: timeline.visible && window.live
                    fontSize: window.style.fontSize
                    textWeight: window.style.fontWeight
                    textColor: window.style.followCoverColor ? Theme.store.lyricAccent(window.playback.coverColor) : window.style.playedColor || Theme.accentText
                    unplayedColor: window.style.unplayedFollowCoverColor ? Theme.store.lyricAccent(window.playback.coverColor) : window.style.unplayedColor || Theme.floatingSecondaryText
                    secondaryFontSize: window.style.secondaryFontSize
                    secondaryGap: window.style.lineGap
                    horizontalAlignment: window.style.alignment === "left" ? Qt.AlignLeft : window.style.alignment === "right" ? Qt.AlignRight : Qt.AlignHCenter
                    softFocus: false
                    filterLyrics: false
                    springEnabled: true
                    scaleEnabled: true
                    hidePassed: false
                    focusPosition: .5
                    wordFade: .5
                }
            }
            Label {
                anchors.centerIn: parent
                visible: window.service.panel === "queue" && (!window.playback.queue || window.playback.queue.count === 0)
                text: qsTr("播放队列为空")
                color: Theme.floatingSecondaryText
                font: Theme.font(12, Theme.regularWeight)
            }
            ListView {
                id: queue
                objectName: "miniPlayerQueue"
                visible: window.service.panel === "queue"
                x: 0
                y: 38
                width: parent.width
                height: parent.height - 42
                model: visible && window.live ? window.playback.queue : null
                reuseItems: true
                cacheBuffer: 84
                clip: true
                spacing: 0
                onVisibleChanged: if (visible && window.playback.queue)
                    positionViewAtIndex(window.playback.queue.currentIndex, ListView.Contain)
                delegate: Item {
                    id: song
                    required property int index
                    required property string title
                    required property string artistText
                    required property string coverUrl
                    required property bool current
                    width: queue.width
                    height: 42
                    Button {
                        id: queueAction
                        x: 8
                        width: parent.width - 16
                        height: parent.height
                        Accessible.name: song.title + " - " + song.artistText
                        onClicked: window.playback.playIndex(song.index)
                        background: Rectangle {
                            radius: 8
                            color: song.current ? Theme.withAlpha(Theme.accent, .12) : queueAction.hovered ? Theme.floating.hover : "transparent"
                        }
                        contentItem: RowLayout {
                            spacing: 8
                            EchoCover {
                                source: song.coverUrl
                                Layout.preferredWidth: 30
                                Layout.preferredHeight: 30
                                resourceActive: window.live && queue.visible
                                colors: Theme.floating
                            }
                            Label {
                                Layout.fillWidth: true
                                text: song.title
                                textFormat: Text.PlainText
                                elide: Text.ElideRight
                                font: Theme.font(12, song.current ? 600 : 400)
                                color: song.current ? Theme.accentText : Theme.floatingText
                            }
                            EchoIcon {
                                visible: song.current
                                name: "music"
                                color: Theme.accentText
                                Layout.preferredWidth: 14
                                Layout.preferredHeight: 14
                            }
                        }
                    }
                }
                ScrollBar.vertical: EchoPageScrollBar {
                    view: queue
                    resourceActive: queue.visible && window.live
                }
            }
        }
        Item {
            id: controls
            objectName: "miniPlayerControls"
            y: window.service.expandsUp ? card.height - 64 : 0
            width: parent.width
            height: 64
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 9
                anchors.rightMargin: 24
                spacing: 10
                ColumnLayout {
                    spacing: 1
                    EchoIconButton {
                        implicitWidth: 18
                        implicitHeight: 20
                        padding: 3
                        colors: Theme.floating
                        glyph: "x"
                        text: qsTr("关闭 Mini")
                        onClicked: window.service.closeMini()
                    }
                    EchoIconButton {
                        implicitWidth: 18
                        implicitHeight: 20
                        padding: 3
                        colors: Theme.floating
                        glyph: "square"
                        text: qsTr("回到主窗口")
                        tooltipText: text
                        onClicked: window.service.showMain()
                    }
                }
                Button {
                    id: coverAction
                    objectName: "miniPlayerLyricAction"
                    implicitWidth: 44
                    implicitHeight: 44
                    Accessible.name: qsTr("显示歌词")
                    Accessible.checkable: true
                    Accessible.checked: window.service.panel === "lyric"
                    background: EchoCover {
                        objectName: "miniPlayerCover"
                        source: window.playback.cover
                        resourceActive: window.live
                        colors: Theme.floating
                        cornerRadius: Theme.mediaRadius
                    }
                    onClicked: window.service.setPanel(window.service.panel === "lyric" ? "" : "lyric")
                }
                Item {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    Layout.preferredHeight: 44
                    Layout.bottomMargin: 8
                    Column {
                        objectName: "miniPlayerMetadata"
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width
                        spacing: 1
                        opacity: window.showControls ? 0 : 1
                        Behavior on opacity {
                            NumberAnimation {
                                duration: Theme.store.motionEnabled ? 140 : 0
                            }
                        }
                        EchoTextTagRow {
                            objectName: "miniPlayerTitle"
                            width: parent.width
                            fillTextWidth: true
                            lineHeight: 18
                            text: window.playback.title
                            font: Theme.font(13, 700)
                            color: Theme.floatingText
                        }
                        EchoTextTagRow {
                            objectName: "miniPlayerArtist"
                            width: parent.width
                            fillTextWidth: true
                            lineHeight: 15
                            text: window.playback.artist || "EchoMusic"
                            font: Theme.font(11, 600)
                            color: Theme.floatingSecondaryText
                        }
                    }
                    RowLayout {
                        anchors.centerIn: parent
                        spacing: 0
                        opacity: window.showControls ? 1 : 0
                        enabled: window.playback.hasTrack
                        Behavior on opacity {
                            NumberAnimation {
                                duration: Theme.store.motionEnabled ? 140 : 0
                            }
                        }
                        EchoIconButton {
                            implicitWidth: 28
                            implicitHeight: 30
                            padding: 7
                            colors: Theme.floating
                            glyph: "player-skip-back"
                            text: qsTr("上一首")
                            onClicked: window.playback.previous()
                        }
                        EchoIconButton {
                            objectName: "miniPlayerPlay"
                            implicitWidth: 30
                            implicitHeight: 32
                            padding: 7
                            colors: Theme.floating
                            glyph: window.playback.playing ? "pause" : "play"
                            animatePlayPause: true
                            text: window.playback.playing ? qsTr("暂停") : qsTr("播放")
                            onClicked: window.playback.toggle()
                        }
                        EchoIconButton {
                            implicitWidth: 28
                            implicitHeight: 30
                            padding: 7
                            colors: Theme.floating
                            glyph: "player-skip-forward"
                            text: qsTr("下一首")
                            onClicked: window.playback.next()
                        }
                    }
                }
                RowLayout {
                    spacing: 0
                    EchoIconButton {
                        objectName: "miniPlayerPin"
                        implicitWidth: 24
                        implicitHeight: 30
                        padding: 4
                        colors: Theme.floating
                        glyph: "pinned"
                        text: window.style.alwaysOnTop ? qsTr("取消置顶") : qsTr("窗口置顶")
                        tooltipText: text
                        checkable: true
                        checked: window.style.alwaysOnTop
                        foreground: checked ? Theme.accentText : Theme.floatingSecondaryText
                        onClicked: window.style.alwaysOnTop = !window.style.alwaysOnTop
                    }
                    EchoIconButton {
                        objectName: "miniPlayerFavorite"
                        implicitWidth: 24
                        implicitHeight: 30
                        padding: 4
                        colors: Theme.floating
                        foreground: window.playback.libraryActions && window.playback.libraryActions.favorite ? "#ef4444" : hoverHighlighted ? Theme.floating.accentText : Theme.floating.iconMain
                        glyph: window.playback.libraryActions && window.playback.libraryActions.favorite ? "heart-filled" : "heart"
                        text: qsTr("收藏当前歌曲")
                        enabled: window.playback.hasTrack && window.playback.libraryActions !== null
                        onClicked: window.playback.libraryActions.toggleFavorite()
                    }
                    EchoIconButton {
                        objectName: "miniPlayerQueueAction"
                        implicitWidth: 24
                        implicitHeight: 30
                        padding: 4
                        colors: Theme.floating
                        glyph: "list"
                        text: qsTr("播放队列")
                        foreground: window.service.panel === "queue" ? Theme.accentText : Theme.floatingText
                        onClicked: window.service.setPanel(window.service.panel === "queue" ? "" : "queue")
                    }
                    EchoIconButton {
                        implicitWidth: 24
                        implicitHeight: 30
                        padding: 4
                        colors: Theme.floating
                        glyph: "typography"
                        objectName: "miniPlayerDesktopLyric"
                        text: qsTr("桌面歌词")
                        foreground: window.service.desktopEnabled ? Theme.accentText : Theme.floatingText
                        onClicked: window.service.toggleDesktop()
                    }
                    EchoIconButton {
                        id: volumeAction
                        objectName: "miniPlayerVolume"
                        implicitWidth: 24
                        implicitHeight: 30
                        padding: 4
                        colors: Theme.floating
                        glyph: window.playback.volume > 0 ? "volume" : "volume-off"
                        text: qsTr("音量")
                        onClicked: volumePopupOwner.visible ? volumePopupOwner.close() : volumePopupOwner.open()
                        WheelHandler {
                            onWheel: event => {
                                window.playback.volume += event.angleDelta.y / 120 * 5;
                            }
                        }
                    }
                }
            }
            PlaybackProgress {
                objectName: "miniPlayerProgress"
                x: 91
                y: parent.height - height - 5
                width: parent.width - x - 16
                height: 16
                playback: window.playback
                compact: true
                edgeToEdge: true
                colors: Theme.floating
            }
        }
    }
    EchoPopupLoader {
        id: lyricMenuOwner
        objectName: "miniPlayerLyricMenuOwner"
        hostItem: window.contentItem
        sourceComponent: Component {
            EchoMenu {
                id: lyricMenu
                objectName: "miniPlayerLyricMenu"
                implicitHeight: count * 36 + topPadding + bottomPadding
                popupType: Popup.Window
                Action {
                    text: qsTr("注音样式")
                    checkable: true
                    checked: window.playback.lyricText.romanizationAsRuby
                    onTriggered: window.playback.lyricText.romanizationAsRuby = checked
                }
                Action {
                    text: qsTr("选择歌词")
                    onTriggered: window.service.selectLyrics()
                }
                Action {
                    text: qsTr("歌词提前")
                    onTriggered: window.playback.lyricText.adjustTrackOffset(1)
                }
                Action {
                    text: qsTr("歌词延后")
                    onTriggered: window.playback.lyricText.adjustTrackOffset(-1)
                }
                Action {
                    text: qsTr("重置单曲偏移")
                    onTriggered: window.playback.lyricText.resetTrackOffset()
                }
            }
        }
    }
    Repeater {
        model: [Qt.LeftEdge, Qt.RightEdge]
        MouseArea {
            required property int modelData
            objectName: "miniPlayerResize-" + modelData
            x: modelData === Qt.LeftEdge ? 0 : window.width - width
            y: 12
            width: 5
            height: window.height - 24
            cursorShape: Qt.SizeHorCursor
            onPressed: mouse => window.service.resizeMiniWidth(modelData, mapToGlobal(mouse.x, mouse.y).x)
            onPositionChanged: mouse => {
                if (pressed)
                    window.service.updateMiniWidthResize(mapToGlobal(mouse.x, mouse.y).x);
            }
            onReleased: window.service.endMiniWidthResize()
            onCanceled: window.service.endMiniWidthResize()
        }
    }
    EchoPopupLoader {
        id: volumePopupOwner
        objectName: "miniPlayerVolumePopupOwner"
        hostItem: window.contentItem
        sourceComponent: Component {
            MiniVolumePopup {
                id: volumePopup
                objectName: "miniPlayerVolumePopup"
                playback: window.playback
                anchorItem: volumeAction
            }
        }
    }
}
