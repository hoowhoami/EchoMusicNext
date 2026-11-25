pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Controls
import EchoMusic.Music
import EchoMusic.Theme
import EchoMusic.Shell

Item {
    id: panel
    objectName: "recommendationPanel-" + card.key
    required property RecommendationCard card
    required property DiscoveryService service
    required property RouteController router
    property bool resourceActive: true
    property real uiScale: 1
    property int previewIndex: 0
    property string demandKey: ""
    readonly property bool grouped: card.groups.count > 0 || card.key === "3102" || card.key === "3103"
    readonly property bool hasContent: card.collection.count > 0 || card.groups.count > 0
    readonly property bool emptyBody: !hasContent && (card.status === 2 || card.status === 3)
    // A section reserves its full preview extent while it is still loading.
    // Retained content keeps its own extent through refresh/error states. The
    // outer ListView only estimates unmeasured delegates, so a section that grew
    // from a short loading body to the loaded body would otherwise shift every
    // section below it and leave blank space at the viewport bottom.
    readonly property int previewSongs: card.collection.count > 0 ? Math.min(6, card.collection.count) : 6
    // The sleeve follows the reference aspect ratio; captions have two fixed
    // single-line slots, so upstream paragraph breaks cannot shrink artwork.
    readonly property real vipPreviewWidth: Math.min(206, width * .48)
    readonly property real vipSleeveHeight: Math.ceil(vipPreviewWidth * 1.34)
    readonly property real vipCaptionHeight: Theme.font(12, Theme.regularWeight).pixelSize * 1.5 + 4 + Theme.font(10, Theme.regularWeight).pixelSize * 1.5
    readonly property real bodyHeight: emptyBody ? 96 : grouped ? 240 : card.key === "3006" ? vipSleeveHeight + 8 + vipCaptionHeight : card.key === "3004" ? 240 : Math.ceil(previewSongs / 2) * 84
    implicitHeight: contentLayout.implicitHeight
    function rememberIndex(index: int): void {
        previewIndex = Math.max(0, index);
        panel.EchoPageState.save("recommendation-preview-" + card.key, {
            index: previewIndex
        });
    }
    readonly property bool loading: card.status === 0 || card.status === 1
    readonly property bool style: card.key === "style"
    readonly property bool daily: card.key === "daily"
    readonly property bool hasInformation: daily || style || card.key === "3014"
    // A stable reading budget keeps late subtitles/tags from changing rows.
    // Only the available header width determines whether information wraps.
    readonly property real headingBudget: headingTitle.implicitWidth - headingBadge.implicitWidth + headingBadge.horizontalPadding * 2 + headingBadge.font.pixelSize * 2
    readonly property bool inlineInformation: hasInformation && width - (width >= 340 ? headerActions.implicitWidth + 8 : 0) >= headingBudget + 12 + (style ? 168 : 120)
    readonly property string information: {
        if (!daily)
            return card.subtitle;
        const parts = [];
        if (service.dailyDate.length)
            parts.push(Qt.formatDate(new Date(service.dailyDate + "T00:00:00"), qsTr("M月d日")));
        if (service.dailySubtitle.trim().length)
            parts.push(service.dailySubtitle.trim());
        return parts.join(" · ");
    }
    readonly property string informationFallback: {
        if (card.errorMessage.length)
            return card.errorMessage;
        if (card.status === 3)
            return qsTr("暂时无法加载推荐");
        if (card.status !== 2)
            return qsTr("正在准备推荐");
        if (card.collection.totalCount > 0)
            return qsTr("%1 首歌曲").arg(card.collection.totalCount);
        return qsTr("暂无推荐内容");
    }
    readonly property string informationGlyph: daily || card.key === "3101" ? "calendar" : card.key === "3014" ? "music" : card.key === "3102" || card.key === "3103" ? "adjustments" : "headphones"
    function syncDemand(): void {
        if (!card || !service)
            return;
        if (demandKey !== card.key) {
            if (demandKey.length)
                service.activateCard(demandKey, false);
            demandKey = card.key;
            previewIndex = panel.EchoPageState.load("recommendation-preview-" + card.key).index || 0;
        }
        service.activateCard(card.key, resourceActive);
        if (!resourceActive && stylePicker)
            stylePicker.close();
    }
    onResourceActiveChanged: syncDemand()
    onCardChanged: syncDemand()
    Component.onCompleted: {
        previewIndex = panel.EchoPageState.load("recommendation-preview-" + card.key).index || 0;
        syncDemand();
    }
    Component.onDestruction: service.activateCard(card.key, false)
    ColumnLayout {
        id: contentLayout
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        spacing: 16
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            ColumnLayout {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                spacing: 8
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12
                    EchoTextTagRow {
                        id: headingTitle
                        objectName: "recommendationHeading-" + panel.card.key
                        Layout.fillWidth: !panel.inlineInformation
                        Layout.minimumWidth: 0
                        text: panel.card.title
                        font: Theme.font(15, Theme.semiboldWeight)
                        color: Theme.text
                        spacing: 4
                        wrapTags: false
                        EchoBadge {
                            id: headingBadge
                            count: panel.card.collection.totalCount > 99 ? "99+" : panel.card.collection.totalCount
                            tone: EchoBadge.Neutral
                        }
                    }
                    Loader {
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        active: panel.inlineInformation
                        visible: active
                        sourceComponent: HeaderInformation {}
                    }
                }
                Loader {
                    Layout.fillWidth: true
                    active: panel.hasInformation && !panel.inlineInformation
                    visible: active
                    sourceComponent: HeaderInformation {}
                }
            }
            Loader {
                id: headerActions
                active: !panel.grouped && panel.width >= 340
                visible: active
                Layout.alignment: Qt.AlignTop
                sourceComponent: cardActions
            }
        }
        Loader {
            Layout.alignment: Qt.AlignRight
            active: !panel.grouped && panel.width < 340
            visible: active
            sourceComponent: cardActions
        }
        Loader {
            objectName: "recommendationBody"
            Layout.fillWidth: true
            Layout.preferredHeight: panel.bodyHeight
            Layout.minimumHeight: panel.bodyHeight
            Layout.maximumHeight: panel.bodyHeight
            active: panel.resourceActive
            asynchronous: true
            sourceComponent: panel.card.groups.count > 0 ? groupContent : panel.card.key === "3004" || panel.card.key === "3006" ? featureContent : songContent
        }
    }
    component HeaderInformation: Item {
        implicitHeight: 28
        Item {
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            width: Math.min(implicitWidth, parent.width)
            implicitWidth: informationRow.implicitWidth + 16
            height: 28
            visible: !panel.style
            RowLayout {
                id: informationRow
                anchors.fill: parent
                spacing: 6
                EchoIcon {
                    Layout.preferredWidth: 12
                    Layout.preferredHeight: 12
                    name: panel.informationGlyph
                    color: Theme.secondaryText
                }
                EchoTextLine {
                    objectName: "recommendationInformation-" + panel.card.key
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    text: panel.information.length ? panel.information : panel.informationFallback
                    font: Theme.font(11, Theme.regularWeight)
                    color: Theme.secondaryText
                }
            }
        }
        EchoSoftButton {
            objectName: panel.style ? "styleRecommendationFilters" : ""
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            width: Math.min(implicitWidth, parent.width)
            visible: panel.style
            text: qsTr("筛选：%1").arg(panel.service.styleSummary)
            glyph: "chevron-down"
            size: EchoControlSize.Small
            enabled: panel.service.styleGroups.length > 0
            onClicked: stylePicker.open()
        }
    }
    component CardAction: EchoSoftButton {
        glyphSize: 12
        size: EchoControlSize.Small
        accent: true
    }
    Component {
        id: cardActions
        RowLayout {
            spacing: 8
            CardAction {
                objectName: "recommendationViewAll"
                text: qsTr("更多")
                glyph: "list"
                onClicked: panel.router.navigate("recommendation-detail", {}, panel.card.key)
            }
            CardAction {
                objectName: "recommendationPlayAll"
                text: qsTr("播放")
                glyph: "play"
                enabled: panel.card.collection.count > 0
                onClicked: {
                    if (panel.card.key === "3107")
                        panel.service.playFeed(3107);
                    else
                        panel.card.actions.play();
                }
            }
        }
    }
    component SongPreview: Item {
        id: preview
        objectName: "homeSongCard"
        required property int index
        required property string title
        required property string artistText
        required property string resourceId
        required property string coverUrl
        required property bool catalogPlayable
        required property var privilegeTags
        required property string qualityLabel
        required property var recommendation
        required property bool presentationReady
        property int presentationMode: 0
        property bool pooled: false
        readonly property SongCatalogModel collection: panel.card.collection
        readonly property bool resourceActive: panel.resourceActive && !pooled && (presentationMode !== 0 || index < 6)
        readonly property var entry: ({
                title: preview.title,
                artistText: preview.artistText,
                resourceId: preview.resourceId,
                coverUrl: preview.coverUrl,
                catalogPlayable: preview.catalogPlayable,
                privilegeTags: preview.privilegeTags,
                qualityLabel: preview.qualityLabel,
                recommendation: preview.recommendation
            })
        visible: presentationReady && (presentationMode !== 0 || index < 6)
        GridView.onPooled: pooled = true
        GridView.onReused: pooled = false
        ListView.onPooled: pooled = true
        ListView.onReused: pooled = false
        Loader {
            anchors.fill: parent
            anchors.bottomMargin: preview.presentationMode === 0 ? 2 : 0
            // Native view reuse keeps a bounded presentation tree. Pooling
            // retires its images/motion via resourceActive instead of tearing
            // down and rebuilding the same controls during each slide.
            active: panel.resourceActive && (preview.presentationMode !== 0 || preview.index < 6)
            asynchronous: true
            sourceComponent: preview.presentationMode === 0 ? songRow : preview.presentationMode === 1 ? vipSong : featureSong
        }
        Component {
            id: songRow
            RecommendationSongRow {
                customPlayback: panel.card.key === "3107"
                onPlayRequested: panel.service.playFeed(3107, "", "", "", 0, 6, preview.index)
                entry: preview.entry
                commands: panel.card.actions
                resourceActive: panel.resourceActive && !preview.pooled
            }
        }
        Component {
            id: vipSong
            RecommendationVipSong {
                sleeveHeight: panel.vipSleeveHeight
                entry: preview.entry
                commands: panel.card.actions
                resourceActive: panel.resourceActive && !preview.pooled
            }
        }
        Component {
            id: featureSong
            RecommendationFeatureSong {
                entry: preview.entry
                commands: panel.card.actions
                resourceActive: panel.resourceActive && !preview.pooled
            }
        }
    }
    Component {
        id: songContent
        Item {
            GridView {
                id: previews
                objectName: panel.style ? "styleRecommendationPreview" : "recommendationPreview"
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.rightMargin: -12
                anchors.top: parent.top
                height: Math.min(parent.height, 252)
                cellWidth: width / 2
                cellHeight: 84
                interactive: false
                keyNavigationEnabled: false
                clip: true
                cacheBuffer: 0
                reuseItems: true
                model: panel.card.collection
                EchoViewPoolGuard {
                    targetView: previews
                }
                visible: panel.card.collection.count > 0
                delegate: SongPreview {
                    width: previews.cellWidth - 12
                    height: 84
                }
            }
            Loader {
                anchors.fill: parent
                active: panel.card.collection.count === 0
                sourceComponent: emptyContent
            }
        }
    }
    component Carousel: ListView {
        id: carousel
        orientation: ListView.Horizontal
        snapMode: ListView.SnapToItem
        boundsBehavior: Flickable.StopAtBounds
        flickableDirection: Flickable.HorizontalFlick
        spacing: 12
        clip: true
        reuseItems: true
        // Prepare neighboring presentation slots, not a second song collection.
        cacheBuffer: previewWidth + spacing
        property real previewWidth: compositionWidth
        readonly property int compositionColumns: Math.max(1, Math.floor((parent.width + spacing) / (320 + spacing)))
        readonly property real compositionWidth: Math.max(0, (width - spacing * (compositionColumns - 1)) / compositionColumns)
        EchoViewPoolGuard {
            targetView: carousel
        }
        property bool restored: false
        property bool viewReady: false
        activeFocusOnTab: count > 1
        HoverHandler {
            id: carouselHover
            parent: carousel.parent
            margin: 36
            blocking: false
            enabled: panel.resourceActive
        }
        property bool keyboardNavigation: false
        readonly property bool navigationShown: panel.resourceActive && count > 1 && (carouselHover.hovered || previous.hovered || next.hovered || keyboardNavigation && activeFocus || previous.visualFocus || next.visualFocus)
        onActiveFocusChanged: if (!activeFocus)
            keyboardNavigation = false
        Keys.onLeftPressed: {
            keyboardNavigation = true;
            step(-1);
        }
        Keys.onRightPressed: {
            keyboardNavigation = true;
            step(1);
        }
        CarouselArrow {
            id: previous
            parent: carousel.parent
            objectName: "recommendationCarouselPrevious-" + panel.card.key
            anchors.right: carousel.left
            anchors.verticalCenter: carousel.verticalCenter
            visible: carousel.navigationShown && !carousel.atXBeginning
            glyph: "chevron-left"
            text: qsTr("上一组")
            onClicked: carousel.step(-1)
        }
        CarouselArrow {
            id: next
            parent: carousel.parent
            objectName: "recommendationCarouselNext-" + panel.card.key
            anchors.left: carousel.right
            anchors.verticalCenter: carousel.verticalCenter
            visible: carousel.navigationShown && !carousel.atXEnd
            glyph: "chevron-right"
            text: qsTr("下一组")
            onClicked: carousel.step(1)
        }
        function restoreIndex(): void {
            if (viewReady && !restored && count > 0 && width > 0) {
                forceLayout();
                currentIndex = Math.min(panel.previewIndex, count - 1);
                positionViewAtIndex(currentIndex, ListView.Beginning);
                restored = true;
            }
        }
        function step(delta: int): void {
            const index = Math.max(0, Math.min(count - 1, currentIndex + delta));
            slide.stop();
            cancelFlick();
            const start = contentX;
            // Cached native positions and uniform slots avoid a viewport
            // round trip just to calculate the animation destination.
            const item = itemAtIndex(index);
            const requested = item ? item.x : originX + index * (previewWidth + spacing);
            const destination = Math.max(originX, Math.min(requested, originX + Math.max(0, contentWidth - width)));
            currentIndex = index;
            panel.rememberIndex(currentIndex);
            if (motion.allowed && Math.abs(destination - start) > .5) {
                contentX = start;
                slide.from = start;
                slide.to = destination;
                slide.start();
            } else
                contentX = destination;
        }
        EchoMotionScope {
            id: motion
            target: carousel
            active: panel.resourceActive
            animations: [slide]
        }
        NumberAnimation {
            id: slide
            objectName: "recommendationCarouselSlide-" + panel.card.key
            target: carousel
            property: "contentX"
            duration: Theme.panelDuration
            easing.type: Easing.BezierSpline
            easing.bezierCurve: Theme.easeStandard
        }
        onMovementStarted: slide.stop()
        onCountChanged: restoreIndex()
        onWidthChanged: {
            if (restored) {
                slide.stop();
                forceLayout();
                positionViewAtIndex(currentIndex, ListView.Beginning);
            } else
                restoreIndex();
        }
        Component.onCompleted: {
            viewReady = true;
            restoreIndex();
        }
        onMovementEnded: {
            if (slide.running)
                return;
            const left = indexAt(contentX + 1, height / 2);
            if (left >= 0) {
                currentIndex = left;
                panel.rememberIndex(left);
            }
        }
        footer: Item {
            width: 1
            height: carousel.height
        }
    }
    component CarouselArrow: EchoIconButton {
        id: arrow
        width: 36
        height: 36
        padding: 8
        z: 2
        foreground: hovered || visualFocus ? Theme.accentText : Theme.text
        visualContentActive: panel.resourceActive && visible
        hoverBackground: false
        background: null
    }
    Component {
        id: featureContent
        Item {
            Carousel {
                id: features
                objectName: "recommendationFeatureCarousel-" + panel.card.key
                anchors.fill: parent
                // Do not enumerate the source while an initial zero-width
                // slot still awaits its native layout.
                model: width > 0 && previewWidth > 0 ? panel.card.collection : null
                previewWidth: panel.card.key === "3006" ? Math.min(206, width * .48) : compositionWidth
                delegate: SongPreview {
                    width: features.previewWidth
                    height: features.height
                    presentationMode: panel.card.key === "3006" ? 1 : 2
                }
                Loader {
                    anchors.fill: parent
                    active: panel.card.collection.count === 0
                    sourceComponent: emptyContent
                }
            }
        }
    }
    Component {
        id: groupContent
        Item {
            Carousel {
                id: groups
                objectName: "recommendationGroupCarousel-" + panel.card.key
                anchors.fill: parent
                model: panel.card.groups
                previewWidth: panel.card.groups.count > 1 ? compositionWidth : width
                delegate: RecommendationGroupCard {
                    property bool pooled: false
                    card: panel.card
                    service: panel.service
                    router: panel.router
                    playback: panel.service.playback
                    width: groups.previewWidth
                    height: groups.height
                    commands: panel.card.actions
                    resourceActive: panel.resourceActive && !pooled
                    ListView.onPooled: pooled = true
                    ListView.onReused: pooled = false
                }
            }
        }
    }
    Component {
        id: emptyContent
        ColumnLayout {
            width: parent ? parent.width : 0
            EchoSpinner {
                Layout.alignment: Qt.AlignHCenter
                visible: panel.loading
                resourceActive: panel.resourceActive
            }
            Label {
                Layout.fillWidth: true
                text: panel.loading ? qsTr("正在为你挑选好歌…") : panel.card.errorMessage || qsTr("暂时没有推荐内容")
                color: Theme.secondaryText
                font: Theme.font(12, Theme.regularWeight)
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
            }
            EchoSoftButton {
                Layout.alignment: Qt.AlignHCenter
                visible: panel.card.status === 3
                text: qsTr("重试")
                onClicked: panel.service.refreshCard(panel.card.key)
            }
        }
    }
    EchoPopupLoader {
        id: stylePicker
        hostItem: panel
        sourceComponent: EchoOptionPicker {
            objectName: "styleRecommendationPicker"
            heading: qsTr("推荐偏好")
            uiScale: panel.uiScale
            multiple: true
            groups: panel.service.styleGroups.map(group => ({
                        label: group.name,
                        options: group.child.map(tag => ({
                                    id: String(tag.id),
                                    label: tag.name
                                }))
                    }))
            selectedIds: panel.service.selectedTags
            onToggled: id => panel.service.toggleTag(id)
        }
    }
}
