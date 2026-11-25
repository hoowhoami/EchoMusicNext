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
    readonly property bool singleGroup: grouped && card.groups.count === 1
    property Action singleGroupAction: null
    readonly property bool hasContent: card.collection.count > 0 || card.groups.count > 0
    readonly property bool emptyBody: !hasContent && (card.status === 2 || card.status === 3)
    // A section reserves its full preview extent while it is still loading.
    // Retained content keeps its own extent through refresh/error states. The
    // outer ListView only estimates unmeasured delegates, so a section that grew
    // from a short loading body to the loaded body would otherwise shift every
    // section below it and leave blank space at the viewport bottom.
    readonly property int previewSongs: card.collection.count > 0 ? Math.min(6, card.collection.count) : 6
    readonly property int songColumns: contentWidth >= 652 ? 2 : 1
    // The sleeve follows the reference aspect ratio; captions have two fixed
    // single-line slots, so upstream paragraph breaks cannot shrink artwork.
    // Integral slots keep ListView snapping and animated targets identical.
    readonly property real vipPreviewWidth: Math.min(206, Math.floor(contentWidth * .48))
    readonly property real vipSleeveHeight: Math.ceil(vipPreviewWidth * 1.34)
    readonly property real vipCaptionHeight: Theme.font(12, Theme.regularWeight).pixelSize * 1.5 + 4 + Theme.font(10, Theme.regularWeight).pixelSize * 1.5
    readonly property real bodyHeight: emptyBody ? 96 : grouped ? 240 : card.key === "3006" ? vipSleeveHeight + 8 + vipCaptionHeight : card.key === "3004" ? 240 : Math.ceil(previewSongs / songColumns) * 84
    readonly property real contentWidth: Math.max(0, width - 32)
    implicitHeight: contentLayout.implicitHeight + 32
    Rectangle {
        objectName: "recommendationSectionSurface"
        anchors.fill: parent
        radius: Theme.cardRadius
        color: Theme.withAlpha(Theme.text, Theme.dark ? .06 : .035)
    }
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
    readonly property bool inlineInformation: hasInformation && contentWidth - (!grouped ? headerActions.implicitWidth + 8 : 0) >= headingBudget + 12 + (style ? 168 : 120)
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
        anchors.margins: 16
        spacing: 16
        RowLayout {
            Layout.fillWidth: true
            Layout.minimumHeight: 28
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
                        Loader {
                            active: !panel.grouped
                            visible: active
                            Layout.leftMargin: 6
                            sourceComponent: EchoIconButton {
                                id: headerPlay
                                objectName: "recommendationPlayAll"
                                width: 28
                                height: 28
                                implicitWidth: 28
                                implicitHeight: 28
                                padding: 7
                                scaleFeedback: true
                                glyph: "play"
                                text: qsTr("播放")
                                foreground: Theme.accentText
                                visualContentActive: panel.resourceActive
                                enabled: panel.card.collection.count > 0
                                background: Rectangle {
                                    radius: width / 2
                                    color: Theme.withAlpha(Theme.accent, headerPlay.hovered ? .18 : .1)
                                    border.width: headerPlay.visualFocus ? 2 : 0
                                    border.color: Theme.accent
                                }
                                onClicked: {
                                    if (panel.card.key === "3107")
                                        panel.service.playFeed(3107);
                                    else
                                        panel.card.actions.play();
                                }
                            }
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
                active: !panel.grouped || panel.singleGroup
                visible: active
                Layout.alignment: Qt.AlignTop
                sourceComponent: moreAction
            }
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
    Component {
        id: moreAction
        EchoSoftButton {
            objectName: "recommendationViewAll"
            text: qsTr("更多")
            Accessible.name: qsTr("更多")
            glyph: "chevron-right"
            glyphAfterText: true
            glyphSize: 14
            plain: true
            hoverBackground: false
            leftPadding: 4
            rightPadding: 0
            size: EchoControlSize.Small
            enabled: !panel.grouped || panel.singleGroupAction !== null && panel.singleGroupAction.enabled
            onClicked: {
                if (panel.singleGroupAction && panel.singleGroup)
                    panel.singleGroupAction.trigger();
                else if (!panel.grouped)
                    panel.router.navigate("recommendation-detail", {}, panel.card.key);
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
                height: Math.min(parent.height, Math.ceil(6 / panel.songColumns) * 84)
                cellWidth: width / panel.songColumns
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
    component Carousel: EchoCarouselView {
        resourceActive: panel.resourceActive
        navigationKey: panel.card.key
        initialIndex: panel.previewIndex
        onIndexRemembered: index => panel.rememberIndex(index)
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
                previewWidth: panel.card.key === "3006" ? Math.min(206, Math.floor(width * .48)) : compositionWidth
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
                model: width > 0 && previewWidth > 0 ? panel.card.groups : null
                previewWidth: panel.card.groups.count > 1 ? compositionWidth : width
                delegate: RecommendationGroupCard {
                    id: themeGroup
                    property bool pooled: false
                    promoteMore: panel.singleGroup
                    Binding {
                        target: panel
                        property: "singleGroupAction"
                        value: themeGroup.moreAction
                        when: panel.singleGroup && themeGroup.index === 0 && !themeGroup.pooled
                    }
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
