pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Shell
import EchoMusic.Controls
import EchoMusic.Theme

RoutePage {
    id: page
    required property DetailsService service
    property WindowController windowService: null
    readonly property MvDetailContext detail: service.mvFor(context)
    readonly property var info: detail.detail
    readonly property bool videoPresented: pageActive && visible && page.Window.window !== null && page.Window.window.visible && page.Window.visibility !== Window.Minimized
    readonly property string collectionId: info.id === null || info.id === undefined ? "" : String(info.id)
    readonly property bool collected: detail.collection.loaded && detail.collection.contains(collectionId)
    objectName: "mvDetailPage"
    property bool resumeBarrageComposer: false
    readonly property string authorLine: (info.artists || []).map(a => a.name || "").filter(n => n.length).join(" / ") || info.artist || qsTr("未知歌手")
    function countText(value: var): string {
        if (value === undefined || value === null)
            return "—";
        if (value >= 100000000)
            return (value / 100000000).toFixed(1) + qsTr("亿");
        if (value >= 10000)
            return (value / 10000).toFixed(1) + qsTr("万");
        return String(value);
    }
    function qualityLabel(quality: string): string {
        return ({
                fhd: "1080P",
                hd: "720P",
                qhd: "540P",
                sd: "432P",
                ld: "270P"
            })[quality] || quality.toUpperCase();
    }
    function timeText(ms: real): string {
        const seconds = Math.floor(ms / 1000);
        return String(Math.floor(seconds / 60)).padStart(2, "0") + ":" + String(seconds % 60).padStart(2, "0");
    }
    function updatePresentation(): void {
        if (page.detail) {
            page.detail.setPresented(page.videoPresented);
            if (page.settings)
                page.detail.barrage.setEnabled(page.settings.mvBarrageEnabled);
        }
        if (!page.pageActive) {
            resumeBarrageComposer = false;
            // Route presentation can change before recipe construction or
            // after child retirement. Each owner enforces its own availability.
            if (barrageOwner)
                barrageOwner.close();
            if (fullscreenOwner)
                fullscreenOwner.close();
        }
    }
    onPageActiveChanged: updatePresentation()
    onVideoPresentedChanged: updatePresentation()
    onDetailChanged: updatePresentation()
    onSettingsChanged: updatePresentation()
    Component.onCompleted: updatePresentation()
    Connections {
        target: page.detail ? page.detail.barrage : null
        function onVerificationStarted(): void {
            if (barrageOwner && barrageOwner.visible) {
                page.resumeBarrageComposer = true;
                barrageOwner.close();
            }
        }
        function onSubmitted(): void {
            page.resumeBarrageComposer = false;
        }
        function onScopeReset(): void {
            page.resumeBarrageComposer = false;
        }
        function onSubmissionFailed(): void {
            const resume = page.resumeBarrageComposer;
            page.resumeBarrageComposer = false;
            // Verification removes its modal input shield before this queued
            // reopen. The owner still enforces route/window availability.
            if (resume)
                Qt.callLater(function () {
                    if (page && page.pageActive && barrageOwner)
                        barrageOwner.open();
                });
        }
    }
    Connections {
        target: page.settings
        function onChanged(): void {
            if (page.detail && page.settings)
                page.detail.barrage.setEnabled(page.settings.mvBarrageEnabled);
        }
    }
    EchoPageScrollContainer {
        id: viewport
        anchors.fill: parent
        pageActive: page.pageActive
        scrollView: scroll
        Flickable {
            id: scroll
            objectName: "mvPageScroll"
            anchors.fill: parent
            contentHeight: content.implicitHeight
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: EchoPageScrollBar {
                view: scroll
                container: viewport
                viewActive: page.pageActive
            }
            ColumnLayout {
                id: content
                x: (scroll.width - width) / 2
                width: Math.min(1120, Math.max(0, scroll.width - 32))
                spacing: 14
                Item {
                    Layout.preferredHeight: 2
                }
                MvVideoSurface {
                    id: normalVideo
                    Layout.fillWidth: true
                    Layout.preferredHeight: width * 9 / 16
                    detail: page.detail
                    settings: page.settings
                    presented: page.videoPresented
                    outputActive: fullscreenOwner.item === null
                    onFullscreenRequested: fullscreenOwner.open()
                }
                RowLayout {
                    Layout.fillWidth: true
                    Item {
                        Layout.fillWidth: true
                    }
                    EchoSoftButton {
                        objectName: "mvCollection"
                        text: page.detail.collection.errorMessage.length ? qsTr("重试收藏状态") : page.collected ? qsTr("已收藏") : qsTr("收藏")
                        glyph: page.collected ? "heart-filled" : "heart"
                        accent: page.collected
                        loading: page.detail.collection.loading || page.detail.collection.pending(page.collectionId)
                        enabled: !page.detail.loading && page.collectionId.length > 0 && !page.detail.collection.loading && !page.detail.collection.pending(page.collectionId)
                        onClicked: page.detail.collection.toggle(page.collectionId)
                    }
                    EchoSoftButton {
                        id: barrageButton
                        objectName: "mvBarrageButton"
                        text: qsTr("弹幕") + "  " + (page.settings.mvBarrageEnabled ? qsTr("已开启") : qsTr("已关闭"))
                        glyph: "message-circle"
                        accent: page.settings.mvBarrageEnabled
                        enabled: page.detail.sourceHash.length > 0
                        onClicked: barrageOwner.open()
                    }
                }
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: hero.implicitHeight + 36
                    radius: Theme.cardRadius
                    color: Theme.card
                    EchoDashedFrame {
                        anchors.fill: parent
                        solid: true
                        strokeColor: Theme.border
                        radius: Theme.cardRadius
                    }
                    ColumnLayout {
                        id: hero
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: 18
                        spacing: 16
                        GridLayout {
                            Layout.fillWidth: true
                            columns: page.width < 768 ? 1 : 3
                            columnSpacing: 14
                            rowSpacing: 14
                            EchoCover {
                                Layout.preferredWidth: 96
                                Layout.preferredHeight: 96
                                source: page.info.cover_url || ""
                                resourceActive: page.pageActive
                                cornerRadius: Theme.mediaRadius
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                Layout.minimumWidth: 0
                                spacing: 6
                                EchoTag {
                                    visible: page.info.recommendation_code === 1
                                    text: qsTr("推荐版本")
                                    tone: EchoTag.Accent
                                    size: EchoTag.Small
                                }
                                Label {
                                    Layout.fillWidth: true
                                    text: page.info.title || qsTr("MV")
                                    wrapMode: Text.Wrap
                                    font: Theme.font(24, Theme.extraBoldWeight)
                                    lineHeight: 1.3
                                    color: Theme.text
                                }
                                RowLayout {
                                    Layout.fillWidth: true
                                    Layout.minimumWidth: 0
                                    spacing: 8
                                    EchoCover {
                                        Layout.preferredWidth: 24
                                        Layout.preferredHeight: 24
                                        visible: (page.info.artists || []).length > 0 && String(page.info.artists[0].avatar_url || "").length > 0
                                        source: visible ? page.info.artists[0].avatar_url : ""
                                        cornerRadius: 12
                                        resourceActive: page.videoPresented
                                    }
                                    EchoTextTagRow {
                                        Layout.fillWidth: true
                                        Layout.minimumWidth: 0
                                        text: page.authorLine
                                        font: Theme.font(14, Theme.boldWeight)
                                        color: Theme.text
                                        spacing: 8
                                        Label {
                                            text: "· " + qsTr("发布于 %1").arg(page.info.published_at ? String(page.info.published_at).split(" ")[0] : qsTr("未知"))
                                            font: Theme.font(13, Theme.regularWeight)
                                            color: Theme.secondaryText
                                        }
                                    }
                                }
                            }
                            RowLayout {
                                Layout.alignment: Qt.AlignTop
                                visible: page.detail.versionCount > 1
                                spacing: 8
                                EchoSoftButton {
                                    objectName: "mvPreviousVersion"
                                    text: qsTr("上一版")
                                    enabled: page.detail.versionIndex > 0
                                    onClicked: page.detail.selectVersion(page.detail.versionIndex - 1)
                                }
                                Label {
                                    text: (page.detail.versionIndex + 1) + " / " + page.detail.versionCount
                                    color: Theme.secondaryText
                                    font: Theme.font(12, Theme.boldWeight)
                                }
                                EchoSoftButton {
                                    objectName: "mvNextVersion"
                                    text: qsTr("下一版")
                                    enabled: page.detail.versionIndex + 1 < page.detail.versionCount
                                    onClicked: page.detail.selectVersion(page.detail.versionIndex + 1)
                                }
                            }
                        }
                        GridLayout {
                            Layout.fillWidth: true
                            columns: page.width < 768 ? 2 : 4
                            columnSpacing: 10
                            rowSpacing: 10
                            Repeater {
                                model: [
                                    {
                                        label: qsTr("播放量"),
                                        value: page.info.play_count
                                    },
                                    {
                                        label: qsTr("时长"),
                                        value: page.info.duration_ms === null || page.info.duration_ms === undefined ? undefined : page.timeText(page.info.duration_ms)
                                    },
                                    {
                                        label: qsTr("收藏"),
                                        value: page.info.collection_count
                                    },
                                    {
                                        label: qsTr("下载"),
                                        value: page.info.download_count
                                    }
                                ]
                                delegate: Rectangle {
                                    id: stat
                                    required property var modelData
                                    Layout.fillWidth: true
                                    implicitHeight: statText.implicitHeight + 28
                                    color: Theme.card
                                    radius: Theme.itemRadius
                                    ColumnLayout {
                                        id: statText
                                        anchors.left: parent.left
                                        anchors.right: parent.right
                                        anchors.top: parent.top
                                        anchors.margins: 14
                                        spacing: 6
                                        Label {
                                            text: stat.modelData.label
                                            color: Theme.secondaryText
                                            font: Theme.font(12, Theme.regularWeight)
                                        }
                                        Label {
                                            text: stat.modelData.label === qsTr("时长") ? stat.modelData.value === undefined ? "—" : String(stat.modelData.value) : page.countText(stat.modelData.value)
                                            color: Theme.text
                                            font: Theme.font(15, Theme.extraBoldWeight)
                                        }
                                    }
                                }
                            }
                        }
                        Flow {
                            Layout.fillWidth: true
                            spacing: 8
                            visible: (page.info.tags || []).length > 0 || String(page.info.edition || "").length > 0
                            EchoTag {
                                visible: String(page.info.edition || "").length > 0
                                text: page.info.edition || ""
                                size: EchoTag.Small
                            }
                            Repeater {
                                model: page.info.tags || []
                                delegate: EchoTag {
                                    required property var modelData
                                    text: modelData.tag_name || ""
                                    tone: EchoTag.Accent
                                    size: EchoTag.Small
                                }
                            }
                        }
                        Label {
                            Layout.fillWidth: true
                            visible: String(page.info.description || "").length > 0
                            text: page.info.description || ""
                            wrapMode: Text.Wrap
                            color: Theme.secondaryText
                            font: Theme.font(13, Theme.regularWeight)
                            lineHeight: 1.8
                        }
                    }
                }
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: sourceContent.implicitHeight + 36
                    radius: Theme.cardRadius
                    color: Theme.card
                    EchoDashedFrame {
                        anchors.fill: parent
                        solid: true
                        strokeColor: Theme.border
                        radius: Theme.cardRadius
                    }
                    ColumnLayout {
                        id: sourceContent
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: 18
                        spacing: 10
                        Label {
                            Layout.bottomMargin: 4
                            text: qsTr("视频片源")
                            color: Theme.text
                            font: Theme.font(15, Theme.extraBoldWeight)
                        }
                        Repeater {
                            model: page.detail.sources
                            delegate: AbstractButton {
                                id: source
                                EchoActionCursor {}
                                objectName: "mvSourceCard"
                                required property var modelData
                                Layout.fillWidth: true
                                implicitHeight: contentItem.implicitHeight + 28
                                topPadding: 14
                                bottomPadding: 14
                                leftPadding: 16
                                rightPadding: 16
                                Accessible.name: page.qualityLabel(modelData.quality) + " " + modelData.codec
                                onClicked: page.detail.selectSource(modelData.hash)
                                contentItem: GridLayout {
                                    columns: page.width < 768 ? 1 : 2
                                    columnSpacing: 16
                                    rowSpacing: 8
                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        Layout.minimumWidth: 0
                                        spacing: 10
                                        Label {
                                            text: page.qualityLabel(source.modelData.quality)
                                            font: Theme.font(14, Theme.extraBoldWeight)
                                            color: Theme.text
                                        }
                                        Flow {
                                            Layout.fillWidth: true
                                            spacing: 8
                                            Repeater {
                                                model: [source.modelData.codec, source.modelData.width && source.modelData.height ? source.modelData.width + "×" + source.modelData.height : "", source.modelData.bit_rate ? Math.round(source.modelData.bit_rate / 1000) + " kbps" : "", source.modelData.size_bytes ? (source.modelData.size_bytes / 1024 / 1024).toFixed(1) + " MB" : ""].filter(v => String(v).length)
                                                delegate: EchoTag {
                                                    required property string modelData
                                                    text: modelData
                                                    font: Theme.font(11, Theme.regularWeight)
                                                }
                                            }
                                        }
                                    }
                                    Label {
                                        text: source.modelData.hash === page.detail.sourceHash ? qsTr("当前播放") : qsTr("切换")
                                        color: Theme.accentText
                                        font: Theme.font(12, Theme.boldWeight)
                                    }
                                }
                                background: Rectangle {
                                    radius: Theme.cardRadius
                                    color: source.modelData.hash === page.detail.sourceHash ? Theme.mix(Theme.card, Theme.accent, .07) : Theme.card
                                    EchoDashedFrame {
                                        anchors.fill: parent
                                        solid: true
                                        strokeColor: source.modelData.hash === page.detail.sourceHash ? Theme.withAlpha(Theme.accent, .6) : Theme.border
                                        radius: Theme.cardRadius
                                    }
                                }
                            }
                        }
                        Label {
                            visible: page.detail.sources.length === 0 && !page.detail.loading
                            text: qsTr("暂无更多片源")
                            color: Theme.secondaryText
                            font: Theme.font(12, Theme.regularWeight)
                        }
                    }
                }
                Item {
                    Layout.preferredHeight: 14
                }
            }
        }
    }
    EchoPopupLoader {
        id: barrageOwner
        objectName: "mvBarrageOwner"
        hostItem: page
        sourceComponent: Component {
            BarragePanel {
                controller: page.detail.barrage
                settings: page.settings
                anchorItem: barrageButton
                uiScale: page.settings.uiScale
            }
        }
    }
    EchoPopupLoader {
        id: fullscreenOwner
        objectName: "mvFullscreenOwner"
        hostItem: page
        sourceComponent: Component {
            Popup {
                id: fullscreen
                objectName: "mvFullscreenPopup"
                EchoPopupLifecycle {
                    surface: fullscreen
                }
                property bool observedFullscreen: false
                scale: page.settings.uiScale
                transformOrigin: Popup.TopLeft
                x: 0
                y: 0
                width: parent ? parent.width / scale : 0
                height: parent ? parent.height / scale : 0
                padding: 0
                margins: 0
                modal: true
                focus: true
                closePolicy: Popup.CloseOnEscape
                background: Rectangle {
                    color: "black"
                }
                onOpened: {
                    if (page.windowService) {
                        page.windowService.beginContentFullscreen(fullscreen);
                        observedFullscreen = page.windowService.fullscreen;
                    }
                    page.detail.setVideoSink(fullscreenVideo.videoSink);
                }
                onClosed: {
                    page.detail.setVideoSink(normalVideo.videoSink);
                    if (page.windowService)
                        page.windowService.endContentFullscreen(fullscreen);
                }
                Connections {
                    target: page.windowService
                    function onContentFullscreenExitRequested(owner: QtObject): void {
                        if (owner === fullscreen)
                            fullscreen.close();
                    }
                    function onChanged(): void {
                        if (!page.windowService || !fullscreen.opened)
                            return;
                        if (page.windowService.fullscreen)
                            fullscreen.observedFullscreen = true;
                        else if (fullscreen.observedFullscreen)
                            fullscreen.close();
                    }
                }
                contentItem: MvVideoSurface {
                    id: fullscreenVideo
                    detail: page.detail
                    settings: page.settings
                    fullScreen: true
                    presented: page.videoPresented
                    onFullscreenRequested: fullscreen.close()
                }
            }
        }
    }
}
