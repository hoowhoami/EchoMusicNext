pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import EchoMusic.Shell
import EchoMusic.Controls
import EchoMusic.Theme
import EchoMusic.Music

// DiscoveryService owns the recommendation catalog and initial fetch.
// PlaybackService owns the shared discovery queue; this page presents and navigates it.
RoutePage {
    id: page
    objectName: "discoverFlowPage"
    required property DiscoveryService service
    readonly property SongCatalogModel collection: service.discoverFlow
    readonly property SongCatalogActions actions: service.actions(page.collection)
    // Named partState, not state: Item already owns the state-machine property.
    readonly property int partState: service.states[DiscoveryService.DiscoverFlow]
    readonly property bool loading: (page.partState === DiscoveryService.Loading || page.partState === DiscoveryService.Idle) && page.collection.count === 0
    readonly property bool loadingMore: page.partState === DiscoveryService.Loading && page.collection.count > 0 && !page.refreshPending
    readonly property bool refreshLoading: page.loading || page.refreshPending && page.partState === DiscoveryService.Loading
    readonly property string errorMessage: page.partState === DiscoveryService.Error ? service.errors[DiscoveryService.DiscoverFlow] : ""
    readonly property int activeIndex: Math.max(0, Math.min(m_activeIndex, Math.max(0, page.collection.count - 1)))
    readonly property var currentItem: page.collection.count > 0 ? page.collection.item(page.activeIndex) : null
    readonly property string artworkUrl: page.currentItem ? String(page.currentItem.coverUrl || "") : ""
    // The backdrop shows the same auto-rotating artist portraits the playback
    // page's portrait mode uses (playback.portraits follows the playing track).
    // While the gallery is empty — not yet fetched, or the track has no
    // portraits — the displayed cover keeps the page from showing a bare
    // surface, matching the previous cover-only behaviour.
    readonly property url portraitSource: service.playback.portraits.source
    readonly property url backdropSource: page.portraitSource.toString().length > 0 ? page.portraitSource : page.artworkUrl
    readonly property string progressLabel: page.collection.count > 0 ? (page.activeIndex + 1) + " / " + page.collection.count : "0 / 0"
    readonly property string currentId: page.currentItem ? String(page.currentItem.resourceId || "") : ""
    readonly property bool isCurrentTrack: page.service.discoverFlowActive && page.currentId.length > 0 && page.service.playback.trackId === page.currentId
    readonly property bool isCurrentPlaying: page.isCurrentTrack && page.service.playback.playing
    // favoriteRevision is read so the binding re-evaluates after a favorite
    // write (same pattern as CatalogSongList); isFavorite alone is an invokable.
    readonly property bool favorite: page.actions.favoriteRevision >= 0 && page.currentId.length > 0 && page.actions.isFavorite(page.currentId)
    readonly property bool canGoPrev: page.activeIndex > 0
    readonly property bool canGoNext: page.collection.count === 0 || page.activeIndex < page.collection.count - 1 || !page.loadingMore
    readonly property bool narrow: page.width <= 600
    readonly property bool short: page.height <= 420
    readonly property bool resourcesActive: page.pageActive && page.visible && page.Window.window !== null && page.Window.window.visible && page.Window.window.visibility !== Window.Minimized
    clip: true
    // Follow the shared queue until an explicit refresh. A fresh preview list
    // must not be replaced by the still-playing previous track.
    property bool followActiveQueue: true
    property int m_activeIndex: 0
    property bool actionBusy: false
    property bool playbackBusy: false
    property bool refreshPending: false
    property string busyTarget: ""
    property real lastWheelAt: -1
    // Bounded bottom-preload bookkeeping: advance only when rows actually grow.
    property int appendTarget: -1
    focus: page.pageActive
    activeFocusOnTab: false

    function goPrev(): void {
        page.appendTarget = -1;
        if (!page.canGoPrev)
            return;
        page.m_activeIndex -= 1;
        page.startCurrentPlayback();
    }
    function goNext(): void {
        if (page.activeIndex < page.collection.count - 1) {
            page.m_activeIndex += 1;
            page.startCurrentPlayback();
            return;
        }
        if (page.loadingMore)
            return;
        // At the last row, append a batch before advancing.
        page.appendTarget = page.collection.count;
        page.service.loadDiscoverFlow(true);
    }
    function startCurrentPlayback(): void {
        const item = page.currentItem;
        if (!item)
            return;
        if (item.catalogPlayable === false) {
            page.service.notify(qsTr("当前歌曲暂不可播放"), 2);
            return;
        }
        page.followActiveQueue = true;
        page.busyTarget = String(item.resourceId);
        page.playbackBusy = true;
        page.service.playDiscover(String(item.resourceId));
    }
    function playCurrent(): void {
        if (page.isCurrentTrack) {
            page.service.playback.toggle();
            return;
        }
        page.startCurrentPlayback();
    }
    function toggleFavorite(): void {
        if (!page.currentId.length || page.actionBusy)
            return;
        page.actionBusy = true;
        page.actions.toggleFavorite(page.currentId);
        page.actionBusy = false;
    }
    function refresh(): void {
        page.refreshPending = true;
        page.followActiveQueue = false;
        page.m_activeIndex = 0;
        page.appendTarget = -1;
        page.service.loadDiscoverFlow(false);
    }

    onPageActiveChanged: page.followActiveQueue = true
    // The portrait gallery fetches and rotates only while this page is on
    // screen; leaving releases it the same way the playback page's portrait
    // mode does. Arming is keyed per surface: the playback page's route exit
    // animation keeps its presentation "active" briefly after a navigation,
    // and a keyed release must not drop the gallery this page already armed.
    onResourcesActiveChanged: page.service.playback.portraits.setSurfaceActive(
                                  "discover-flow", page.resourcesActive)
    Component.onCompleted: page.service.playback.portraits.setSurfaceActive(
                               "discover-flow", page.resourcesActive)
    Component.onDestruction: page.service.playback.portraits.setSurfaceActive("discover-flow", false)
    onPartStateChanged: {
        if (page.partState !== DiscoveryService.Loading)
            page.refreshPending = false;
    }
    // Clear the busy flag once playback either resolves to this row or fails.
    Connections {
        target: page.service.playback
        function onChanged(): void {
            if (page.playbackBusy && ((!page.service.playback.loading && page.service.playback.trackId === page.busyTarget) || page.service.playback.errorMessage.length > 0 || !page.service.discoverFlowActive)) {
                page.playbackBusy = false;
                page.busyTarget = "";
            }
            // Follow player-originated track changes without another request.
            if (!page.followActiveQueue || !page.service.discoverFlowActive)
                return;
            const index = page.collection.indexOf(page.service.playback.trackId);
            if (index >= 0 && index !== page.m_activeIndex)
                page.m_activeIndex = index;
        }
    }
    function finishAppend(): void {
        if (page.appendTarget < 0 || page.loadingMore)
            return;
        const target = page.appendTarget;
        page.appendTarget = -1;
        if (page.collection.count > target && page.activeIndex < page.collection.count - 1) {
            page.m_activeIndex += 1;
            page.startCurrentPlayback();
        }
    }
    // Reveal/advance exactly when a bottom preload actually added rows.
    Connections {
        target: page.collection
        function onChanged(): void {
            page.finishAppend();
        }
    }
    Connections {
        target: page.service
        function onChanged(): void {
            page.finishAppend();
            if (page.followActiveQueue && page.service.discoverFlowActive) {
                const index = page.collection.indexOf(page.service.playback.trackId);
                if (index >= 0)
                    page.m_activeIndex = index;
            }
        }
    }

    // Shared route entrance; the info component owns its row out-in transition.
    EchoEnter {
        id: infoEnter
        target: info
        active: page.pageActive
    }

    DiscoverFlowBackdrop {
        objectName: "discoverFlowBackdrop"
        anchors.fill: parent
        source: page.backdropSource
        resourceActive: page.resourcesActive
        // A portrait that cannot be decoded is retired by the gallery
        // controller, which rotates to the next URL (or empties, falling the
        // backdrop back to the cover). Cover failures report into the same
        // controller as a no-op: the URL is not the gallery's source.
        onFailed: imageUrl => page.service.playback.portraits.imageFailed(imageUrl)
    }

    // Header: title left, "换一批推荐" right.
    Item {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.topMargin: page.narrow ? 16 : 20
        anchors.leftMargin: page.narrow ? 18 : 28
        anchors.rightMargin: page.narrow ? 18 : 28
        height: 36
        Text {
            objectName: "discoverFlowHeading"
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("刷歌")
            height: 25.2
            verticalAlignment: Text.AlignVCenter
            color: Theme.text
            font: Theme.font(18, Theme.boldWeight)
        }
        EchoIconButton {
            id: refreshButton
            objectName: "discoverFlowRefresh"
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            implicitWidth: 36
            implicitHeight: 36
            padding: 8
            hoverBackground: false
            glyph: "refresh-source"
            text: qsTr("换一批推荐")
            tooltipText: text
            // Matches the action column's resting colour instead of the faint
            // secondary tone: the icon floats over the artwork like the four
            // transport buttons and must read the same way.
            foreground: hoverHighlighted || visualFocus ? Theme.text : Theme.withAlpha(Theme.text, 0.8)
            enabled: !page.refreshLoading
            onClicked: page.refresh()
        }
        RotationAnimator {
            target: refreshButton.contentItem
            from: 0
            to: 360
            duration: 900
            loops: Animation.Infinite
            running: page.resourcesActive && page.refreshLoading && Theme.store.motionEnabled
            onRunningChanged: {
                if (!running)
                    refreshButton.contentItem.rotation = 0;
            }
        }
    }

    // Stage: info column + fixed action column.
    RowLayout {
        objectName: "discoverFlowStage"
        visible: page.currentItem !== null
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.topMargin: page.short ? 68 : page.narrow ? 76 : 88
        anchors.leftMargin: page.narrow ? 18 : 32
        anchors.rightMargin: page.narrow ? 18 : 32
        anchors.bottomMargin: page.short ? 28 : page.narrow ? 32 : 40
        spacing: page.narrow ? 20 : 32

        Item {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            Layout.fillHeight: true
            Layout.minimumHeight: 0
            DiscoverFlowInfo {
                id: info
                objectName: "discoverFlowInfo"
                width: Math.min(680, parent.width)
                height: parent.height
                anchors.bottom: parent.bottom
                item: page.currentItem
                progressLabel: page.progressLabel
                narrow: page.narrow
                shortPage: page.short
                resourceActive: page.resourcesActive
            }
        }

        Item {
            Layout.preferredWidth: 52
            Layout.preferredHeight: actionColumn.implicitHeight
            Layout.alignment: Qt.AlignBottom
            ColumnLayout {
                id: actionColumn
                objectName: "discoverFlowActions"
                width: parent.width
                height: implicitHeight
                spacing: page.short ? 6 : 12
                EchoIconButton {
                    objectName: "discoverFlowPrev"
                    Layout.alignment: Qt.AlignHCenter
                    implicitWidth: 36
                    implicitHeight: 36
                    padding: 7
                    hoverBackground: false
                    glyph: "chevron-up"
                    text: qsTr("上一首")
                    foreground: hoverHighlighted || visualFocus ? Theme.text : Theme.withAlpha(Theme.text, 0.8)
                    enabled: page.canGoPrev
                    onClicked: page.goPrev()
                }
                EchoIconButton {
                    objectName: "discoverFlowFavorite"
                    Layout.alignment: Qt.AlignHCenter
                    implicitWidth: 36
                    implicitHeight: 36
                    padding: 7
                    hoverBackground: false
                    glyph: page.favorite ? "heart-filled" : "heart"
                    text: page.favorite ? qsTr("取消收藏") : qsTr("收藏")
                    tooltipText: text
                    foreground: page.favorite ? Theme.colors.danger : hoverHighlighted || visualFocus ? Theme.text : Theme.withAlpha(Theme.text, 0.8)
                    enabled: page.currentId.length > 0 && !page.actionBusy
                    onClicked: page.toggleFavorite()
                }
                EchoIconButton {
                    id: flowPlayButton
                    objectName: "discoverFlowPlay"
                    Layout.alignment: Qt.AlignHCenter
                    implicitWidth: 52
                    implicitHeight: 52
                    padding: 13
                    hoverBackground: false
                    animatePlayPause: !page.playbackBusy
                    glyph: page.playbackBusy ? "" : page.isCurrentPlaying ? "pause" : "play"
                    text: page.isCurrentPlaying ? qsTr("暂停") : qsTr("播放")
                    foreground: Theme.text
                    enabled: page.currentItem !== null && !page.playbackBusy
                    onClicked: page.playCurrent()
                    EchoSpinner {
                        name: "loader-2"
                        rotationDuration: 900
                        anchors.centerIn: parent
                        implicitWidth: 26
                        implicitHeight: 26
                        visible: page.playbackBusy
                        opacity: flowPlayButton.enabled ? 1 : flowPlayButton.disabledOpacity
                        resourceActive: page.resourcesActive && page.playbackBusy
                        color: Theme.text
                    }
                }
                EchoIconButton {
                    objectName: "discoverFlowNext"
                    Layout.alignment: Qt.AlignHCenter
                    implicitWidth: 36
                    implicitHeight: 36
                    padding: 7
                    hoverBackground: false
                    glyph: "chevron-down"
                    text: qsTr("下一首")
                    foreground: hoverHighlighted || visualFocus ? Theme.text : Theme.withAlpha(Theme.text, 0.8)
                    enabled: page.canGoNext
                    onClicked: page.goNext()
                }
            }
            Item {
                anchors.top: actionColumn.bottom
                anchors.topMargin: 6
                anchors.horizontalCenter: actionColumn.horizontalCenter
                width: 20
                height: 20
                EchoSpinner {
                    name: "loader-2"
                    rotationDuration: 900
                    anchors.centerIn: parent
                    implicitWidth: 16
                    implicitHeight: 16
                    visible: page.loadingMore
                    resourceActive: page.resourcesActive && page.loadingMore
                    color: Theme.withAlpha(Theme.text, 0.8)
                }
            }
        }
    }

    // Loading state (initial fetch).
    RowLayout {
        objectName: "discoverFlowLoading"
        anchors.centerIn: parent
        visible: page.loading
        spacing: 12
        EchoSpinner {
            name: "loader-2"
            rotationDuration: 900
            Layout.alignment: Qt.AlignHCenter
            implicitWidth: 24
            implicitHeight: 24
            resourceActive: page.loading && page.resourcesActive
        }
        Text {
            text: qsTr("正在获取推荐")
            color: Theme.secondaryText
            font: Theme.font(14, Theme.regularWeight)
        }
    }

    // Show the error state only when no content is available.
    ColumnLayout {
        objectName: "discoverFlowError"
        anchors.centerIn: parent
        width: Math.min(420, Math.max(0, parent.width - 64))
        visible: page.errorMessage.length > 0 && page.collection.count === 0
        spacing: 0
        EchoIcon {
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: 34
            Layout.preferredHeight: 34
            name: "music"
            color: Theme.iconSecondary
        }
        Text {
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: 14
            text: qsTr("暂时无法加载推荐歌曲")
            color: Theme.text
            font: Theme.font(14, Theme.semiboldWeight)
        }
        Text {
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: 6
            Layout.fillWidth: true
            text: qsTr("内容还没能加载出来，请稍后重试。")
            color: Theme.secondaryText
            font: Theme.font(12, Theme.regularWeight)
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
        }
        EchoSoftButton {
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: 20
            text: qsTr("重新加载")
            glyph: "refresh"
            onClicked: page.refresh()
        }
    }

    Keys.onPressed: event => {
        if (event.key === Qt.Key_Down || event.key === Qt.Key_PageDown) {
            event.accepted = true;
            page.goNext();
        } else if (event.key === Qt.Key_Up || event.key === Qt.Key_PageUp) {
            event.accepted = true;
            page.goPrev();
        }
    }
    WheelHandler {
        onWheel: event => {
            if (Math.abs(event.angleDelta.y) < 24)
                return;
            const now = Date.now();
            // Throttle wheel navigation to one step per 420ms.
            if (page.lastWheelAt >= 0 && now - page.lastWheelAt < 420)
                return;
            page.lastWheelAt = now;
            if (event.angleDelta.y < 0)
                page.goNext();
            else
                page.goPrev();
        }
    }
}
