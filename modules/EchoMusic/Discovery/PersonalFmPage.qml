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
    Component {
        id: poolSwitch
        EchoTabBar {
            objectName: "personalFmPoolSwitch"
            width: Math.min(280, page.width - Theme.pageHorizontalInset * 2)
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
                x: Theme.pageHorizontalInset
                y: 20
                width: Math.max(0, view.width - Theme.pageHorizontalInset * 2)
                spacing: 28
                Item {
                    Layout.fillWidth: true
                    implicitHeight: 268
                    PersonalFmHero {
                        y: 8
                        width: parent.width
                        fm: page.fm
                        resourceActive: page.resourcesActive
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
                    RowLayout {
                        objectName: "personalFmMetadata"
                        spacing: 8
                        EchoButtonContent {
                            objectName: "personalFmMetadataStation"
                            Layout.preferredWidth: implicitWidth + 4
                            Layout.fillHeight: true
                            horizontalAlignment: Qt.AlignLeft
                            text: page.fm.title
                            glyph: "heartbeat"
                            glyphSize: 12
                            spacing: 6
                            lineHeight: 16.5
                            color: Theme.secondaryText
                            font: Theme.font(11, Theme.semiboldWeight)
                        }
                        EchoTextLine {
                            objectName: "personalFmMetadataPool"
                            Layout.preferredWidth: Math.ceil(implicitWidth)
                            Layout.fillHeight: true
                            fillTextWidth: true
                            text: page.fm.poolLabel
                            color: Theme.secondaryText
                            font: Theme.font(11, Theme.semiboldWeight)
                            lineHeight: 16.5
                        }
                        EchoTextLine {
                            objectName: "personalFmMetadataTrack"
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            Layout.fillHeight: true
                            fillTextWidth: true
                            visible: page.hasTrack
                            text: page.hasTrack ? page.track.title + " · " + page.track.artist : ""
                            color: Theme.secondaryText
                            font: Theme.font(11, Theme.semiboldWeight)
                            lineHeight: 16.5
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
            width: Math.max(0, parent.width - Theme.pageHorizontalInset * 2)
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
