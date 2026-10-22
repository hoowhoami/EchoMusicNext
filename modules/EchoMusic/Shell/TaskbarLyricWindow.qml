pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Window
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Theme
import EchoMusic.Controls
import EchoMusic.Playback
import EchoMusic.Shell

// 任务栏歌词条：结构、配色与交互对应参考实现
// D:\Code\EchoMusic\src\renderer\taskbarPlayer\TaskbarPlayerView.vue
// （封面 + 两行歌词 / 悬停展开控制与进度 / 内容宽度自适应 / 点击封面打开主窗口）。
Window {
    id: window
    objectName: "taskbarLyricWindow"
    required property TaskbarLyricController controller
    required property PlaybackService playback

    // ---- 主题（参考实现的 neutralThemePalette + 应用强调色） ----
    readonly property bool dark: controller.dark
    readonly property bool anchorRight: controller.anchorRight
    readonly property color fg: dark ? "#ffffff" : "#000000"
    readonly property color muted: dark ? "#bcbcbc" : "#666666"
    readonly property color accentText: Theme.store.lyricAccent(Theme.accent)
    readonly property int maxWidth: controller.maxWidth
    readonly property int fontSize: controller.fontSize
    readonly property bool showTranslation: controller.showTranslation
    readonly property bool wordByWord: controller.wordByWord
    // A narrow strip keeps the cover and the control buttons; the song info
    // and the progress bar are dropped together. The target width decides, not
    // the width the native side applied a frame later - otherwise the right
    // side shows up before it disappears.
    readonly property bool roomForDetails: (expanded ? maxWidth : contentWidth) >= 240
    // The hover state is tracked natively by the controller: the control
    // buttons grab the mouse, which makes a QML hover oscillate. Like the
    // reference, the strip only opens while something is playing; its keyboard
    // focus term has no equivalent here because the window is created with
    // Qt.WindowDoesNotAcceptFocus and can never hold focus.
    readonly property bool expanded: playback.hasTrack && (controller.hovered || seekArea.dragging)

    // ---- 歌词数据（同进程直读，无需 IPC 快照） ----
    readonly property int lineIndex: playback.lyrics.visibleIndexAt(playback.positionMs)
    readonly property var lineMap: playback.lyrics.lineAt(lineIndex)
    readonly property var nextMap: playback.lyrics.lineAt(playback.lyrics.nextVisible(lineIndex))
    // LyricModel::lineAt returns an empty map for an out-of-range index, and an
    // empty map is a truthy object in QML. A map check therefore never reports
    // "no line"; the index and the text decide whether a line really exists.
    readonly property bool hasLine: lineIndex >= 0 && !!(lineMap && lineMap.lineText)
    readonly property bool hasNextLine: !!(nextMap && nextMap.lineText)
    readonly property var units: wordByWord ? playback.lyrics.verticalAt(lineIndex) : []
    readonly property real lyricOffset: playback.lyrics.offsetMs
    readonly property real lineStartMs: hasLine ? (lineMap.startMs || 0) + lyricOffset : 0
    readonly property real lineEndTimeMs: {
        if (!hasLine)
            return 0;
        if (hasNextLine && nextMap.startMs)
            return nextMap.startMs + lyricOffset;
        // Last line: the reference closes the window at the track's end, also
        // shifted by the user's lyric offset (TaskbarPlayerView.vue:77).
        const total = playback.durationMs > 0 ? playback.durationMs : 0;
        return total > 0 ? total + lyricOffset : lineStartMs;
    }
    // The playback position signal is too coarse for a smooth word-by-word
    // highlight; sample the clock locally and interpolate (the reference does
    // the same with requestAnimationFrame).
    property real renderTime: 0
    property real lastPosition: 0
    property real lastStamp: 0
    Timer {
        interval: 16
        running: true
        repeat: true
        onTriggered: {
            const now = Date.now();
            const position = window.playback.positionMs;
            if (position !== window.lastPosition) {
                window.lastPosition = position;
                window.lastStamp = now;
            }
            window.renderTime = window.playback.playing ? window.lastPosition + (now - window.lastStamp) : window.lastPosition;
        }
    }
    // The service reports a tr() placeholder for both title/artist when no
    // track is loaded. The strip only wants real content: fall back to the
    // brand name instead of surfaces echoing "未在播放" while a restore is
    // still in flight (the track shows up the moment the queue restored).
    readonly property string songTitle: (playback.hasTrack ? playback.title : "") || "EchoMusicNext"
    // 歌手行仅在实际存在曲目时跟随服务取值；无曲目（首次安装/数据清除）
    // 恒为「未在播放」，与占位封面（软件图标）、品牌名标题配套。
    readonly property string artist: playback.hasTrack ? (playback.artist || "") : qsTr("未在播放")
    readonly property string primaryText: hasLine ? lineMap.lineText : songTitle
    readonly property string secondaryText: {
        if (!hasLine)
            return artist;
        if (showTranslation && lineMap.translation)
            return lineMap.translation;
        if (playback.lyricText.wantRomanization && lineMap.romanization)
            return lineMap.romanization;
        return hasNextLine ? nextMap.lineText : artist;
    }

    // ---- 内容宽度（对应 lyricWidth.ts：自然宽度受 maxWidth 约束） ----
    property real primaryWidth: 0
    property real secondaryWidth: 0
    readonly property real naturalWidth: Math.max(primaryWidth, secondaryWidth)
    // The strip chrome (cover, spacing and the four paddings) has to be part
    // of the width, otherwise the text is wider than its own area and the
    // karaoke scroll cuts it on both sides.
    readonly property real chromeWidth: coverWidth + 4 + 8 + 8 + 4
    // The reference sizes the four buttons to the cover height and caps them
    // on narrow strips (.controls --control-size: min(vh - 16, (vw-112)/5)).
    readonly property real controlSize: Math.max(22, Math.min(stripHeight - 16, (Math.max(112, controller.currentWidth) - 112) / 5))
    readonly property real contentWidth: Math.min(maxWidth, Math.max(160, naturalWidth + chromeWidth))
    readonly property real coverWidth: Math.max(24, stripHeight - 16)
    // 参考实现 useTaskbarLyrics：行键就是「主行 \0 副行」文本，键变化整块滚动。
    // 分隔符走运行期 fromCharCode：字面 NUL 会被 qmlcachegen 内联进生成的
    // C++ 字符串常量，直接编译失败（C2001）。
    // 键变化先排队再滚：positionMs 一帧推进里文本/时间片/行尾是分几步落定
    // 的，同步取快照会抓到中间态（副行还是旧行、行尾等于行头），整行会先
    // 被画成已唱完的强调色。Qt.callLater 在绑定刷新结束后才执行，两次键变化
    // 合并成一次落定快照。
    readonly property string lineKey: primaryText + String.fromCharCode(0) + secondaryText
    onLineKeyChanged: copy.requestRoll()
    // lyricWidth.resolveContentWidth：换行滚动期间窗口宽度只增不减——变宽立即
    // 放行（新行升入途中不被裁），收窄等旧行滑出结束；否则当前行长于下一行
    // 时，换行处会先被截掉一截旧行的尾巴。
    function syncWidth(): void {
        const target = expanded ? maxWidth : contentWidth;
        const resolved = copy.rolling ? Math.max(copy.appliedWidth, target) : target;
        copy.appliedWidth = resolved;
        controller.setContentWidth(Math.round(resolved));
    }
    onContentWidthChanged: syncWidth()
    onExpandedChanged: syncWidth()
    onMaxWidthChanged: syncWidth()

    // 行组：两行歌词的快照。payload 只在换行时整对象赋值，离开中的旧块保持
    // 最后一帧（对应 Vue 离开元素停止重渲染）；走带时间只跟未冻结的块。
    component LyricPair: Item {
        id: group
        width: parent ? parent.width : 0
        height: parent ? parent.height : 0
        property var payload: null
        property bool frozen: false
        property real frozenTimeMs: 0
        signal pairWidth(bool primary, real width)
        Column {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            spacing: 1
            TaskbarLyricLine {
                width: parent.width
                height: implicitHeight
                text: group.payload ? group.payload.primary : ""
                units: group.payload ? group.payload.units : []
                wordByWord: group.payload ? group.payload.wordByWord : false
                timeMs: group.frozen ? group.frozenTimeMs : window.renderTime
                startMs: group.payload ? group.payload.startMs : 0
                endTimeMs: group.payload ? group.payload.endTimeMs : 0
                timeOffset: group.payload ? group.payload.offsetMs : 0
                anchorRight: window.anchorRight
                accentColor: window.accentText
                baseColor: window.fg
                fontSize: Math.min(window.fontSize, Math.max(11, (window.stripHeight - 10) / 2.3))
                onNaturalWidth: function (value) {
                    group.pairWidth(true, value);
                }
            }
            TaskbarLyricLine {
                width: parent.width
                height: implicitHeight
                // Two lines need the height of the lyric area; the area height
                // must not depend on this item being visible, or the condition
                // can never become true.
                visible: (group.payload ? group.payload.secondary.length > 0 : false) && group.height >= 26
                text: group.payload ? group.payload.secondary : ""
                units: []
                wordByWord: false
                timeMs: group.frozen ? group.frozenTimeMs : window.renderTime
                startMs: group.payload ? group.payload.startMs : 0
                endTimeMs: group.payload ? group.payload.endTimeMs : 0
                timeOffset: group.payload ? group.payload.offsetMs : 0
                anchorRight: window.anchorRight
                accentColor: window.accentText
                baseColor: window.muted
                fontSize: Math.min(window.fontSize, Math.max(11, (window.stripHeight - 10) / 2.3)) * 0.82
                onNaturalWidth: function (value) {
                    group.pairWidth(false, value);
                }
            }
        }
    }

    // The native side owns the window geometry: binding width/height from QML
    // recreates the platform window and unmounts the strip. The layout reads
    // the real strip size from the controller instead of the stale window one.
    readonly property real stripHeight: controller.stripHeight
    color: "transparent"
    title: qsTr("EchoMusicNext 任务栏歌词")
    // No visible binding on purpose: a constant binding fights the C++ side
    // (which shows the window after mounting) and makes the toolkit recreate
    // the platform window, which silently unmounts the strip.
    // The strip starts fully transparent: the controller reveals it only after
    // the mount, so it never shows up inside the app window first.
    opacity: 0
    flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowDoesNotAcceptFocus
    Component.onCompleted: {
        controller.attach(this);
        copy.currentGroup.payload = copy.snapshot();
    }
    Component.onDestruction: controller.detach()

    // A right click on the strip opens the shell's context menu. It is NOT
    // handled here: the taskbar area gives the right button to the shell's
    // XAML island, so the platform interposes on the strip's window procedure
    // and reports the click back through the controller.

    Rectangle {
        id: content
        // The window keeps the full region size; the content only takes the
        // width it needs, and the native window region clips the rest. The
        // collapsed width reads the resolved (roll-protected) value: the raw
        // contentWidth would shrink the moment the next line measures short,
        // clipping the still-visible long line half way through the roll
        // (lyricWidth.ts feeds the *resolved* width into the DOM box too).
        width: window.expanded ? window.controller.currentWidth : copy.appliedWidth
        // The reference keeps the strip 8px shorter than the taskbar row; the
        // window's own height is stale for an externally sized window.
        height: Math.max(0, stripHeight - 8)
        y: 4
        x: window.anchorRight ? parent.width - width : 0
        radius: Theme.controlRadius
        color: window.expanded ? Qt.rgba(window.fg.r, window.fg.g, window.fg.b, 0.05) : "transparent"
        Behavior on color {
            ColorAnimation {
                duration: Theme.store.motionEnabled ? 180 : 0
            }
        }

        RowLayout {
            id: strip
            anchors.fill: parent
            anchors.leftMargin: 4
            anchors.rightMargin: 4
            spacing: 4
            layoutDirection: window.anchorRight ? Qt.RightToLeft : Qt.LeftToRight

            // 封面（点击唤起主窗口）
            EchoCover {
                id: cover
                objectName: "taskbarCover"
                // The reference keeps the cover in the collapsed state and only
                // drops it on narrow strips while expanded.
                // The cover and the control buttons always stay; a narrow
                // strip compresses the song info and the progress area instead.
                // The cover keeps its size when expanded: growing it would
                // shift the control buttons on every hover.
                Layout.preferredWidth: window.coverWidth
                Layout.preferredHeight: window.coverWidth
                cornerRadius: 8
                // 首次安装/数据清除（队列里没有任何曲目）时封面位显示软件
                // 图标；有曲目但封面尚未就绪时保留 music 字形占位。
                source: window.playback.cover || (window.playback.hasTrack ? "" : "qrc:/echo/app/linux-icon.png")
                placeholderGlyph: "music"
                placeholderGlyphSize: 14
                placeholderColor: Qt.rgba(window.fg.r, window.fg.g, window.fg.b, 0.08)
                placeholderOpacity: 1
                Behavior on Layout.preferredWidth {
                    NumberAnimation {
                        duration: Theme.store.motionEnabled ? 180 : 0
                        easing.type: Easing.OutCubic
                    }
                }
                MouseArea {
                    id: coverMouse
                    anchors.fill: parent
                    // The cover is the only entry point into the app.
                    cursorShape: Qt.PointingHandCursor
                    // The manual drag mode reports the press / movement /
                    // release to the controller, which moves the platform
                    // window: the strip is a child of the taskbar, so QML
                    // cannot move it. A press that does not move is a click
                    // and opens the main window.
                    onPressed: function (mouse) {
                        coverMouse.pressX = mouse.x;
                        coverMouse.dragging = false;
                        if (window.controller.dragEnabled)
                            window.controller.dragBegin();
                    }
                    onPositionChanged: function (mouse) {
                        if (!pressed || !window.controller.dragEnabled)
                            return;
                        coverMouse.dragging = true;
                        window.controller.dragUpdate();
                    }
                    onReleased: function (mouse) {
                        if (!window.controller.dragEnabled) {
                            window.controller.openMainRequested();
                            return;
                        }
                        window.controller.dragEnd();
                        if (!coverMouse.dragging)
                            window.controller.openMainRequested();
                    }
                    property real pressX: 0
                    property bool dragging: false
                }
            }

            // 控制条与详情横向排列（参考实现：controls 在左，details 填充）
            RowLayout {
                id: body
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 0
                // The reference reverses the row for the right anchor
                // (.taskbar.right .body { flex-direction: row-reverse }):
                // song text at the far side, controls next to the cover.
                layoutDirection: window.anchorRight ? Qt.RightToLeft : Qt.LeftToRight

                TaskbarLyricControls {
                    id: controls
                    active: window.expanded
                    playback: window.playback
                    fg: window.fg
                    accent: window.accentText
                    controlSize: window.controlSize
                }

                Item {
                    id: details
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.leftMargin: 4
                    Layout.rightMargin: 4

                    // 歌词两行（未展开时）。换行动画作用于整块歌词：每组各自
                    // 持一份两行快照，在同一个裁剪容器里整组平移，而不是每行
                    // 各自动画（对应 TaskbarPlayerView <Transition
                    // name="taskbar-lines">：旧行向上滑出、新行自下而上滑入）。
                    Item {
                        id: copy
                        anchors.fill: parent
                        clip: true
                        opacity: window.expanded ? 0 : 1
                        visible: opacity > 0.01
                        Behavior on opacity {
                            NumberAnimation {
                                duration: Theme.store.motionEnabled ? 180 : 0
                            }
                        }

                        property bool rolling: false
                        property real appliedWidth: 0
                        property Item currentGroup: groupA
                        property Item standbyGroup: groupB
                        property Item rollOutGroup: null
                        property Item rollInGroup: null

                        // One immutable description of the line pair: the key
                        // names match the LyricLine properties they feed.
                        function snapshot(): var {
                            return {
                                primary: window.primaryText,
                                secondary: window.secondaryText,
                                wordByWord: window.wordByWord,
                                units: window.units,
                                startMs: window.lineStartMs,
                                endTimeMs: window.lineEndTimeMs,
                                offsetMs: window.lyricOffset
                            };
                        }

                        // Only the current group's measurements count: stale
                        // re-measures from the leaving pair are dropped.
                        function applyPairWidth(group, primary, width): void {
                            if (group !== currentGroup)
                                return;
                            if (primary)
                                window.primaryWidth = width;
                            else
                                window.secondaryWidth = width;
                        }

                        function finishRoll(): void {
                            if (!rolling)
                                return;
                            rolling = false;
                            syncWidth();
                        }

                        // 行键在一个绑定刷新里可能变化多次（文本先落、行尾/
                        // 副行后落）。这里只排队一次，最后一次和真正的执行都
                        // 发生在刷新完成之后，快照总是落定态。
                        property bool rollQueued: false
                        function requestRoll(): void {
                            if (rollQueued)
                                return;
                            rollQueued = true;
                            Qt.callLater(function () {
                                rollQueued = false;
                                rollLines();
                            });
                        }

                        // 换行：先把两组归位到 resting 态（支持 stop 打断重推），
                        // 新组拿最新快照、旧组在离开瞬间冻结走带时间。
                        function rollLines(): void {
                            const wasRolling = rolling;
                            if (rolling) {
                                rollAnim.stop();
                                finishRoll();
                            }
                            const leaving = currentGroup;
                            const entering = standbyGroup;
                            const fresh = snapshot();
                            if (leaving.payload && leaving.payload.primary === fresh.primary && leaving.payload.secondary === fresh.secondary) {
                                // 恢复风暴（会话刷新在恢复落地前后各翻一次
                                // lineKey）会让后一次 roll 打断前一组的动画：
                                // stop() 把两组停在半途位姿，而旧文本与新文本
                                // 相等又直接 bail——不归位，组永远停在撕裂态
                                // （旧行大半可见、新行停在半途下方），整条冻结
                                // 在旧文本上。打断后的同文本 bail 先回到 resting。
                                if (wasRolling) {
                                    leaving.y = 0;
                                    leaving.opacity = 1;
                                    entering.y = copy.height;
                                    entering.opacity = 0;
                                    leaving.frozen = false;
                                    entering.frozen = false;
                                    syncWidth();
                                }
                                return;
                            }
                            rolling = true;
                            leaving.y = 0;
                            leaving.opacity = 1;
                            entering.y = height;
                            entering.opacity = 0;
                            // Flip first: the fresh payload's synchronous width
                            // reports must count as current; assigned while the
                            // leaving pair still owns the slot they would be
                            // dropped and the content width would freeze at the
                            // startup line forever.
                            currentGroup = entering;
                            standbyGroup = leaving;
                            entering.payload = fresh;
                            leaving.frozenTimeMs = window.renderTime;
                            leaving.frozen = true;
                            entering.frozen = false;
                            rollOutGroup = leaving;
                            rollInGroup = entering;
                            rollAnim.start();
                        }

                        ParallelAnimation {
                            id: rollAnim
                            // transform 0.5s cubic-bezier(0.4, 0, 0.2, 1) +
                            // opacity 0.5s ease；两块相邻平移、互不交叠。
                            NumberAnimation {
                                target: copy.rollOutGroup
                                property: "y"
                                to: -copy.height
                                duration: 500
                                easing.type: Easing.BezierSpline
                                easing.bezierCurve: [0.4, 0, 0.2, 1, 1, 1]
                            }
                            NumberAnimation {
                                target: copy.rollOutGroup
                                property: "opacity"
                                to: 0
                                duration: 500
                            }
                            NumberAnimation {
                                target: copy.rollInGroup
                                property: "y"
                                from: copy.height
                                to: 0
                                duration: 500
                                easing.type: Easing.BezierSpline
                                easing.bezierCurve: [0.4, 0, 0.2, 1, 1, 1]
                            }
                            NumberAnimation {
                                target: copy.rollInGroup
                                property: "opacity"
                                to: 1
                                duration: 500
                            }
                            onFinished: copy.finishRoll()
                        }

                        LyricPair {
                            id: groupA
                            onPairWidth: function (primary, width) {
                                copy.applyPairWidth(groupA, primary, width);
                            }
                        }
                        LyricPair {
                            id: groupB
                            onPairWidth: function (primary, width) {
                                copy.applyPairWidth(groupB, primary, width);
                            }
                        }
                    }

                    // 歌曲信息（展开时，与歌词同区域叠放）
                    Column {
                        id: songInfo
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 0
                        opacity: window.expanded ? 1 : 0
                        visible: opacity > 0.01
                        enabled: window.expanded
                        Behavior on opacity {
                            NumberAnimation {
                                duration: Theme.store.motionEnabled ? 180 : 0
                            }
                        }
                        Text {
                            width: songInfo.width
                            visible: window.roomForDetails
                            text: window.songTitle
                            color: window.fg
                            font.pixelSize: Math.min(window.fontSize, Math.max(11, (stripHeight - 21) / 1.9))
                            elide: Text.ElideRight
                            horizontalAlignment: window.anchorRight ? Text.AlignRight : Text.AlignLeft
                        }
                        Text {
                            width: songInfo.width
                            visible: stripHeight > 44 && window.roomForDetails
                            text: window.artist
                            color: window.muted
                            font.pixelSize: Math.min(window.fontSize, Math.max(11, (stripHeight - 21) / 1.9)) * 0.82
                            elide: Text.ElideRight
                            horizontalAlignment: window.anchorRight ? Text.AlignRight : Text.AlignLeft
                        }
                    }

                    // 进度条（展开时，贴在底部）
                    TaskbarLyricSeekBar {
                        id: seekArea
                        objectName: "taskbarProgress"
                        active: window.expanded && window.roomForDetails
                        playback: window.playback
                        fg: window.fg
                        accent: window.accentText
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: 12
                    }
                }
            }
        }
    }
}
