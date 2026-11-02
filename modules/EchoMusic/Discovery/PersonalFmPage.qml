pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Shell
import EchoMusic.Controls
import EchoMusic.Theme
import EchoMusic.Music
import EchoMusic.Playback

// The page presents the playback-owned FM provider.
// Queue, audio, preview and feedback state remain in their existing services.
RoutePage {
    id: page
    objectName: "personalFmPage"
    required property DiscoveryService service
    readonly property PersonalFmController fm: service.playback.personalFm
    readonly property var track: fm.current
    readonly property bool hasTrack: String(track.id || "").length > 0
    readonly property bool initialLoading: fm.previewLoading && !hasTrack
    readonly property bool resourcesActive: fm.signedIn && pageActive && Window.window !== null && Window.window.visible && Window.window.visibility !== Window.Minimized
    readonly property list<string> chips: [String(track.duration || ""), String(track.quality || ""), String(track.language || ""), track.similarity ? qsTr("相似度%1").arg(track.similarity) : ""].filter(value => value.length > 0)
    ThemeColors {
        id: mediaColors
        text: "white"
        tone: "white"
        secondaryText: "#bdffffff"
        accentText: "white"
        accent: Theme.accent
        elevated: "#0b1620"
        card: "#0b1620"
        border: "#24ffffff"
    }
    Component {
        id: poolSwitch
        EchoTabBar {
            objectName: "personalFmPoolSwitch"
            width: Math.min(280, page.width - 48)
            labels: [qsTr("口味"), qsTr("风格"), qsTr("探索")]
            currentIndex: page.fm.songPool
            enabled: !page.fm.busy
            radioGroup: true
            accessibleName: qsTr("私人 FM 歌曲范围")
            onPicked: index => page.fm.setSongPool(index)
        }
    }
    EchoPageScrollContainer {
        id: viewport
        anchors.fill: parent
        visible: page.fm.signedIn
        pageActive: page.pageActive
        scrollView: view
        stickyHeaders: [stickyHeader]
        Flickable {
            id: view
            objectName: "personalFmScrollView"
            anchors.fill: parent
            topMargin: 176
            // Plain Flickable starts at 0, unlike ListView's leading margin.
            // Start at the real top before layout can clamp/cache 0 as a scroll.
            contentY: -topMargin
            contentWidth: width
            contentHeight: body.implicitHeight + 32
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: EchoPageScrollBar {
                view: view
                container: viewport
            }
            ColumnLayout {
                id: body
                x: 24
                y: 20
                width: Math.max(0, view.width - 48)
                spacing: 28
                Item {
                    Layout.fillWidth: true
                    implicitHeight: 268
                    Item {
                        id: hero
                        objectName: "personalFmHero"
                        y: 8
                        width: parent.width
                        height: 248
                        PersonalFmVinyl {
                            objectName: "personalFmCurrentDisc"
                            x: 160
                            y: (parent.height - height) / 2
                            z: 1
                            visible: page.hasTrack || page.initialLoading
                            artwork: page.track.cover || ""
                            text: (page.fm.currentPlaying ? qsTr("暂停 %1") : qsTr("播放 %1")).arg(page.track.title || "")
                            trackId: page.track.id || ""
                            spinning: page.fm.currentPlaying
                            resourceActive: page.resourcesActive && visible
                            loading: page.initialLoading
                            onClicked: page.fm.play()
                        }
                        Item {
                            id: sideDiscs
                            x: 272
                            width: Math.max(0, hero.width - x)
                            height: parent.height
                            // Create at most three visible side discs without hidden cover instances.
                            readonly property int visibleCount: {
                                const available = Math.max(0, width - 92 + 18);
                                const widths = [186, 168, 168];
                                let count = 0;
                                let used = 0;
                                for (let i = 0; i < widths.length; ++i) {
                                    const next = widths[i] + (i > 0 ? 28 : 0);
                                    if (used + next > available && count > 0)
                                        break;
                                    used += next;
                                    ++count;
                                }
                                return Math.max(1, count);
                            }
                            Row {
                                x: 112
                                y: (parent.height - 176) / 2
                                spacing: 28
                                Repeater {
                                    model: page.initialLoading ? 2 : Math.min(sideDiscs.visibleCount, page.fm.sideTracks.length)
                                    delegate: PersonalFmVinyl {
                                        id: sideDisc
                                        required property int index
                                        readonly property var entry: page.fm.sideTracks[index] || ({})
                                        objectName: "personalFmSideDisc-" + index
                                        artwork: entry.cover || ""
                                        text: qsTr("播放 %1 · %2").arg(entry.title || "").arg(entry.artist || "")
                                        resourceActive: page.resourcesActive
                                        loading: page.initialLoading
                                        onClicked: page.fm.play(String(entry.id || ""))
                                        EchoToolTip {
                                            parent: sideDisc
                                            text: sideDisc.text
                                            requestedVisible: sideDisc.hovered
                                        }
                                    }
                                }
                            }
                        }
                        EchoCover {
                            id: poster
                            objectName: "personalFmPoster"
                            width: 248
                            height: 248
                            z: 2
                            cornerRadius: Theme.cardRadius
                            resourceActive: page.resourcesActive
                            readonly property color base: "#0b1620"
                            readonly property color first: Qt.tint(base, Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, .30))
                            readonly property color second: Qt.tint(base, Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, .40))
                            source: "data:image/svg+xml;utf8," + encodeURIComponent('<svg xmlns="http://www.w3.org/2000/svg" width="248" height="248"><defs><linearGradient id="g" x1="0.280814" y1="-0.102208" x2="0.719186" y2="1.102208"><stop stop-color="' + first + '"/><stop offset=".56" stop-color="' + second + '"/><stop offset="1" stop-color="#0b1620"/></linearGradient><radialGradient id="r" cx=".16" cy=".18" r=".352165"><stop stop-color="' + Theme.accent + '" stop-opacity=".28"/><stop offset="1" stop-color="' + Theme.accent + '" stop-opacity="0"/></radialGradient></defs><rect width="248" height="248" fill="url(#g)"/><rect width="248" height="248" fill="url(#r)"/></svg>')
                            Rectangle {
                                anchors.fill: parent
                                radius: Theme.cardRadius
                                color: "transparent"
                                border.color: "#14ffffff"
                            }
                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 16
                                spacing: 0
                                EchoTabBar {
                                    objectName: "personalFmModeSwitch"
                                    Layout.fillWidth: true
                                    labels: [qsTr("红心"), qsTr("小众"), qsTr("速览"), qsTr("电台")]
                                    currentIndex: page.fm.mode
                                    enabled: !page.fm.busy
                                    radioGroup: true
                                    accessibleName: qsTr("私人 FM 模式")
                                    colors: mediaColors
                                    color: "#24ffffff"
                                    selectionColor: "#38ffffff"
                                    selectionTextColor: "white"
                                    hoverColor: "#1affffff"
                                    onPicked: index => page.fm.setMode(index)
                                }
                                Item {
                                    Layout.fillHeight: true
                                }
                                Text {
                                    Layout.fillWidth: true
                                    Layout.topMargin: 12
                                    text: page.fm.title
                                    color: "white"
                                    font: Theme.spacedFont(28, Theme.extraBoldWeight, -1.12)
                                    lineHeightMode: Text.FixedHeight
                                    lineHeight: 28
                                    elide: Text.ElideRight
                                }
                                Item {
                                    Layout.fillHeight: true
                                }
                                Text {
                                    Layout.fillWidth: true
                                    Layout.topMargin: 10
                                    text: page.hasTrack ? page.track.title + " · " + page.track.artist : page.fm.subtitle + " · " + page.fm.poolLabel
                                    color: "#ccffffff"
                                    font: Theme.font(14, Theme.semiboldWeight)
                                    lineHeight: 1.5
                                    elide: Text.ElideRight
                                }
                                Item {
                                    Layout.fillHeight: true
                                }
                                RowLayout {
                                    Layout.fillWidth: true
                                    Layout.topMargin: 18
                                    spacing: 10
                                    Row {
                                        Layout.fillWidth: true
                                        spacing: 6
                                        Repeater {
                                            model: [11, 17, 8, 19, 13, 18, 9, 14, 8, 15]
                                            delegate: Rectangle {
                                                required property int modelData
                                                width: 3
                                                height: modelData
                                                y: 19 - height
                                                radius: 1.5
                                                color: "#66ffffff"
                                            }
                                        }
                                    }
                                    EchoIconButton {
                                        id: dislike
                                        objectName: "personalFmDislike"
                                        Layout.leftMargin: 6
                                        glyph: "heart-off"
                                        text: qsTr("不喜欢")
                                        implicitWidth: 34
                                        implicitHeight: 34
                                        padding: 5
                                        hoverBackground: false
                                        foreground: hovered ? "white" : "#dbffffff"
                                        colors: mediaColors
                                        enabled: !page.fm.busy && page.hasTrack
                                        onClicked: page.fm.dislike()
                                        EchoToolTip {
                                            parent: dislike
                                            text: qsTr("不喜欢")
                                            requestedVisible: dislike.hovered
                                        }
                                    }
                                    Button {
                                        id: fmPlayButton
                                        objectName: "personalFmPlay"
                                        implicitWidth: 52
                                        implicitHeight: 52
                                        enabled: !page.fm.busy
                                        hoverEnabled: true
                                        Accessible.name: page.fm.currentPlaying ? qsTr("暂停私人 FM") : qsTr("播放私人 FM")
                                        onClicked: page.fm.play()
                                        background: Rectangle {
                                            radius: 26
                                            color: Theme.accent
                                            border.width: fmPlayButton.visualFocus ? 2 : 0
                                            border.color: "white"
                                        }
                                        contentItem: Item {
                                            EchoIcon {
                                                anchors.centerIn: parent
                                                width: 18
                                                height: 18
                                                name: page.fm.currentPlaying ? "pause" : "play"
                                                color: Theme.accentForeground
                                                visible: !page.fm.busy
                                            }
                                            EchoSpinner {
                                                anchors.centerIn: parent
                                                width: 18
                                                height: 18
                                                color: "white"
                                                visible: page.fm.busy
                                                resourceActive: page.resourcesActive && visible
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: nowBody.implicitHeight + 44
                    color: Theme.mutedSurface
                    border.color: Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, .07)
                    radius: Theme.popoverRadius
                    ColumnLayout {
                        id: nowBody
                        x: 22
                        y: 22
                        width: Math.max(0, parent.width - 44)
                        spacing: 18
                        Text {
                            text: qsTr("当前播放")
                            font: Theme.spacedFont(15, Theme.boldWeight, -.15)
                            color: Theme.text
                        }
                        GridLayout {
                            Layout.fillWidth: true
                            visible: page.hasTrack || page.initialLoading
                            columns: 2
                            columnSpacing: 28
                            rowSpacing: 0
                            EchoCover {
                                id: nowCover
                                objectName: "personalFmNowCover"
                                Layout.preferredWidth: 180
                                Layout.preferredHeight: 180
                                source: page.track.cover || ""
                                resourceActive: page.resourcesActive
                                Loader {
                                    anchors.fill: parent
                                    z: -1
                                    active: page.resourcesActive
                                    sourceComponent: EchoSurfaceShadow {
                                        anchors.fill: parent
                                        surface: nowCover
                                        radius: Theme.mediaRadius
                                        blur: 3
                                        offset: Qt.vector2d(0, 1)
                                        color: Qt.rgba(0, 0, 0, Theme.dark ? .18 : .1)
                                    }
                                }
                                Rectangle {
                                    anchors.fill: parent
                                    z: 1
                                    radius: Theme.mediaRadius
                                    color: "transparent"
                                    border.color: Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, .07)
                                }
                                EchoSkeleton {
                                    anchors.fill: parent
                                    visible: page.initialLoading
                                    animate: visible && page.resourcesActive
                                }
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                Layout.minimumWidth: 0
                                Layout.minimumHeight: 180
                                spacing: 14
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 6
                                    Text {
                                        objectName: "personalFmNowTitle"
                                        Layout.fillWidth: true
                                        text: page.track.title || ""
                                        font: Theme.spacedFont(28, Theme.boldWeight, -.84)
                                        color: Theme.text
                                        wrapMode: Text.Wrap
                                        lineHeightMode: Text.FixedHeight
                                        lineHeight: 32.48
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        text: page.track.artist || ""
                                        font: Theme.font(15, Theme.semiboldWeight)
                                        color: Theme.secondaryText
                                        wrapMode: Text.Wrap
                                    }
                                    EchoSkeleton {
                                        Layout.fillWidth: true
                                        implicitHeight: 28
                                        visible: page.initialLoading
                                        animate: visible && page.resourcesActive
                                    }
                                    EchoSkeleton {
                                        Layout.preferredWidth: 160
                                        implicitHeight: 15
                                        visible: page.initialLoading
                                        animate: visible && page.resourcesActive
                                    }
                                }
                                Text {
                                    Layout.fillWidth: true
                                    visible: text.length > 0
                                    text: page.track.album || ""
                                    font: Theme.font(13, Theme.semiboldWeight)
                                    color: Theme.secondaryText
                                    wrapMode: Text.WordWrap
                                    lineHeight: 1.5
                                }
                                Text {
                                    objectName: "personalFmReason"
                                    Layout.fillWidth: true
                                    visible: text.length > 0
                                    text: page.track.reason || ""
                                    font: Theme.font(13, Theme.semiboldWeight)
                                    color: Theme.accentText
                                    wrapMode: Text.WordWrap
                                    lineHeight: 1.5
                                }
                                Flow {
                                    Layout.fillWidth: true
                                    spacing: 10
                                    Repeater {
                                        model: page.chips
                                        delegate: EchoTag {
                                            required property string modelData
                                            text: modelData
                                            size: EchoTag.Small
                                        }
                                    }
                                }
                            }
                        }
                        Text {
                            Layout.fillWidth: true
                            Layout.minimumHeight: 180
                            visible: !page.hasTrack && !page.initialLoading
                            text: page.fm.busy ? qsTr("正在获取推荐内容...") : qsTr("暂时没有可展示的推荐内容。")
                            font: Theme.font(13, Theme.regularWeight)
                            color: Theme.secondaryText
                            verticalAlignment: Text.AlignVCenter
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            visible: page.fm.errorMessage.length > 0
                            Text {
                                Layout.fillWidth: true
                                text: page.fm.errorMessage
                                color: Theme.secondaryText
                                font: Theme.font(13, Theme.regularWeight)
                                wrapMode: Text.WordWrap
                            }
                            EchoSoftButton {
                                text: qsTr("重试")
                                onClicked: page.fm.retry()
                            }
                        }
                    }
                }
            }
        }
        EchoPageStickyHeader {
            id: stickyHeader
            container: viewport
            height: detail.implicitHeight
            SongDetailHeader {
                id: detail
                anchors.fill: parent
                heading: qsTr("私人 FM")
                typeLabel: "FM"
                subtitle: qsTr("实时推送契合口味的音乐，随反馈动态更新专属歌单。")
                scrollTop: viewport.scrollTop
                resourceActive: page.resourcesActive
                showPlaybackActions: false
                coverComponent: Component {
                    DiscoveryThemedCover {
                        glyph: "heartbeat"
                        cornerRadius: Theme.mediaRadius / detail.coverScale
                        resourceActive: page.resourcesActive
                    }
                }
                secondaryDetailsComponent: Component {
                    Flow {
                        spacing: 8
                        EchoButtonContent {
                            width: Math.min(parent.width, implicitWidth + 4)
                            height: implicitHeight
                            horizontalAlignment: Qt.AlignLeft
                            text: page.fm.title
                            glyph: "heartbeat"
                            glyphSize: 12
                            spacing: 6
                            lineHeight: 16.5
                            color: Theme.secondaryText
                            font: Theme.font(11, Theme.semiboldWeight)
                        }
                        EchoTextTagRow {
                            width: Math.min(parent.width, implicitWidth + 4)
                            height: implicitHeight
                            text: page.fm.poolLabel
                            color: Theme.secondaryText
                            font: Theme.font(11, Theme.semiboldWeight)
                            lineHeight: 16.5
                        }
                        EchoTextTagRow {
                            width: Math.min(parent.width, implicitWidth + 4)
                            height: implicitHeight
                            visible: page.hasTrack
                            text: page.hasTrack ? page.track.title + " · " + page.track.artist : ""
                            color: Theme.secondaryText
                            font: Theme.font(11, Theme.semiboldWeight)
                            lineHeight: 16.5
                            wrap: true
                        }
                    }
                }
                additionalActions: poolSwitch
                collapsedActions: Component {
                    EchoTabBar {
                        objectName: "personalFmCollapsedPoolSwitch"
                        width: Math.min(280, page.width / 2)
                        compact: true
                        labels: [qsTr("口味"), qsTr("风格"), qsTr("探索")]
                        currentIndex: page.fm.songPool
                        enabled: !page.fm.busy
                        radioGroup: true
                        accessibleName: qsTr("私人 FM 歌曲范围")
                        onPicked: index => page.fm.setSongPool(index)
                    }
                }
            }
        }
    }
    Item {
        anchors.left: parent.left
        anchors.right: parent.right
        height: 420
        visible: !page.fm.signedIn
        ColumnLayout {
            anchors.centerIn: parent
            width: Math.max(0, parent.width - 48)
            spacing: 0
            Rectangle {
                Layout.alignment: Qt.AlignHCenter
                Layout.preferredWidth: 72
                Layout.preferredHeight: 72
                radius: Theme.cardRadius
                color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, .1)
                EchoIcon {
                    anchors.centerIn: parent
                    width: 32
                    height: 32
                    name: "heart-filled"
                    color: Theme.accentText
                }
            }
            Text {
                Layout.alignment: Qt.AlignHCenter
                Layout.topMargin: 20
                text: qsTr("登录后查看私人 FM")
                font: Theme.font(22, Theme.semiboldWeight)
                color: Theme.text
            }
            Text {
                Layout.alignment: Qt.AlignHCenter
                Layout.topMargin: 8
                text: qsTr("猜你喜欢和实时推荐需要登录状态")
                font: Theme.font(13, Theme.regularWeight)
                color: Theme.secondaryText
            }
        }
    }
}
