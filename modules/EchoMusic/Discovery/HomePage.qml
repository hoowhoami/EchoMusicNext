pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Window
import EchoMusic.Shell
import EchoMusic.Controls
import EchoMusic.Theme
import EchoMusic.Music

RoutePage {
    id: page
    objectName: "homePage"
    required property DiscoveryService service
    readonly property bool narrow: Window.window ? Window.window.width / settings.uiScale <= 820 : width <= 820
    readonly property list<string> groupLabels: service.styleGroups.map(group => group.name)
    readonly property var activeTags: service.styleGroups.length > 0 ? service.styleGroups[service.activeStyleGroup].child : []
    function loading(part: int): bool {
        return service.states[part] === DiscoveryService.Loading || service.states[part] === DiscoveryService.Idle;
    }
    // Request consent when the home page is first mounted, after startup completes.
    EchoPopupLoader {
        hostItem: page
        objectName: "userAgreementOwner"
        requestedVisible: page.pageActive && !page.settings.userAgreementAccepted
        sourceComponent: UserAgreementDialog {
            settings: page.settings
            uiScale: page.settings.uiScale
            onDeclineRequested: Qt.quit()
        }
    }
    EchoPageScrollContainer {
        id: pageViewport
        anchors.fill: parent
        pageActive: page.pageActive
        scrollView: homeList
        Flickable {
            id: homeList
            objectName: "homeView"
            anchors.fill: parent
            contentWidth: width
            contentHeight: body.height + 56
            flickableDirection: Flickable.VerticalFlick
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: EchoPageScrollBar {
                view: homeList
                container: pageViewport
                viewActive: page.pageActive
            }
            Column {
                id: body
                x: 40
                y: 16
                width: Math.max(0, homeList.width - 80)
                Loader {
                    width: parent.width
                    sourceComponent: intro
                }
                Loader {
                    width: parent.width
                    sourceComponent: styleTitle
                }
                Loader {
                    width: parent.width
                    sourceComponent: filters
                }
                Rectangle {
                    id: styleSection
                    readonly property bool showGrid: page.loading(DiscoveryService.Style) || page.service.states[DiscoveryService.Style] === DiscoveryService.Ready && page.service.styleSongs.count > 0
                    width: body.width
                    height: showGrid ? styleGrid.height + 4 : styleEmpty.height
                    color: showGrid ? Theme.mutedSurface : "transparent"
                    bottomLeftRadius: Theme.popoverRadius
                    bottomRightRadius: Theme.popoverRadius
                    EchoVirtualGridSection {
                        id: styleGrid
                        objectName: "homeStyleGrid"
                        x: 14
                        width: Math.max(0, parent.width - 28 + 10)
                        visible: styleSection.showGrid
                        columns: Math.max(1, Math.floor(width / 250))
                        cellHeight: 72
                        scrollView: homeList
                        contentTop: body.y + styleSection.y
                        resourceActive: page.pageActive && visible
                        model: page.loading(DiscoveryService.Style) ? columns * 2 : page.service.styleSongs
                        itemCount: page.loading(DiscoveryService.Style) ? columns * 2 : page.service.styleSongs.count
                        delegate: page.loading(DiscoveryService.Style) ? styleSkeletonCell : styleSongCell
                        Component {
                            id: styleSongCell
                            HomeSongCard {
                                required property string title
                                required property string artistText
                                required property string resourceId
                                required property string coverUrl
                                required property bool catalogPlayable
                                required property var privilegeTags
                                required property string qualityLabel
                                property bool pooled: false
                                width: styleGrid.cellWidth - 10
                                height: 62
                                service: page.service
                                entry: ({
                                        title,
                                        artistText,
                                        resourceId,
                                        coverUrl,
                                        catalogPlayable,
                                        privilegeTags,
                                        qualityLabel
                                    })
                                resourceActive: page.pageActive && !pooled
                                GridView.onPooled: pooled = true
                                GridView.onReused: pooled = false
                            }
                        }
                        Component {
                            id: styleSkeletonCell
                            ContentSkeleton {
                                property bool pooled: false
                                width: styleGrid.cellWidth - 10
                                height: 62
                                compact: true
                                resourceActive: page.pageActive && !pooled
                                GridView.onPooled: pooled = true
                                GridView.onReused: pooled = false
                            }
                        }
                    }
                    Loader {
                        id: styleEmpty
                        objectName: "homeStyleEmptyLoader"
                        width: parent.width
                        active: !styleSection.showGrid
                        visible: active
                        property string kind: "styleEmpty"
                        sourceComponent: emptyState
                    }
                }
                PlaylistSection {
                    objectName: "homePlaylistSection"
                    kind: "playlist"
                    part: DiscoveryService.Playlists
                    collection: page.service.playlists
                }
                PlaylistSection {
                    objectName: "homeEditorSection"
                    kind: "editor"
                    part: DiscoveryService.Editors
                    collection: page.service.editors
                }
            }
        }
    }
    component PlaylistSection: Column {
        id: section
        required property string kind
        required property int part
        required property SongCatalogModel collection
        readonly property bool showGrid: page.loading(section.part) || page.service.states[section.part] === DiscoveryService.Ready && section.collection.count > 0
        width: body.width
        Loader {
            width: parent.width
            property string kind: section.kind + "Header"
            sourceComponent: playlistTitle
        }
        EchoVirtualGridSection {
            id: grid
            objectName: section.kind === "playlist" ? "homePlaylistGrid" : "homeEditorGrid"
            width: section.width + 20
            visible: section.showGrid
            columns: Math.max(1, Math.floor(width / 200))
            cellHeight: Math.ceil(cellWidth + 46)
            scrollView: homeList
            contentTop: body.y + section.y + y
            resourceActive: page.pageActive && visible
            model: page.loading(section.part) ? columns * 2 : section.collection
            itemCount: page.loading(section.part) ? columns * 2 : section.collection.count
            delegate: page.loading(section.part) ? skeletonCell : playlistCell
            Component {
                id: playlistCell
                ContentCard {
                    property bool pooled: false
                    width: grid.cellWidth - 20
                    height: width + 44
                    durationText: ""
                    resourceActive: page.pageActive && !pooled
                    navigationAvailable: page.router.available("playlist-detail")
                    onOpenRequested: id => page.router.navigate("playlist-detail", {}, id)
                    GridView.onPooled: pooled = true
                    GridView.onReused: pooled = false
                }
            }
            Component {
                id: skeletonCell
                ContentSkeleton {
                    property bool pooled: false
                    width: grid.cellWidth - 20
                    height: width + 44
                    resourceActive: page.pageActive && !pooled
                    GridView.onPooled: pooled = true
                    GridView.onReused: pooled = false
                }
            }
        }
        Loader {
            width: parent.width
            objectName: section.kind === "playlist" ? "homePlaylistEmptyLoader" : "homeEditorEmptyLoader"
            // Loader retains its last size after unloading. The Column must
            // exclude the inactive placeholder, and ancestor hiding is not an
            // empty result (effective grid.visible also follows the route).
            active: !section.showGrid
            visible: active
            property string kind: section.kind + "Empty"
            sourceComponent: emptyState
        }
    }
    Component {
        id: intro
        ColumnLayout {
            spacing: 0
            EchoTextTagRow {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                wrap: true
                text: page.service.greeting
                font: Theme.spacedFont(22, Theme.semiboldWeight, -.55)
                color: Theme.text
            }
            EchoTextTagRow {
                Layout.topMargin: 4
                text: qsTr("由此开启好心情 ~")
                font: Theme.font(12, Theme.regularWeight)
                color: Theme.secondaryText
            }
            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 28
                spacing: 16
                HomeFeatureCard {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    label: qsTr("每日推荐")
                    description: qsTr("为你量身定制")
                    emblem: page.service.todayLabel
                    resourceActive: page.pageActive
                    onClicked: page.router.navigate("recommend-songs")
                }
                HomeFeatureCard {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    label: qsTr("排行榜")
                    description: qsTr("实时热门趋势")
                    emblem: "TOP"
                    secondary: true
                    resourceActive: page.pageActive
                    onClicked: page.router.navigate("ranking")
                }
            }
        }
    }
    Component {
        id: styleTitle
        Item {
            implicitHeight: 82
            RowLayout {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 16
                Text {
                    Layout.fillWidth: true
                    text: qsTr("风格推荐")
                    font: Theme.font(15, Theme.semiboldWeight)
                    color: Theme.text
                }
                EchoSoftButton {
                    objectName: "homePlayStyle"
                    accent: true
                    text: qsTr("播放全部")
                    glyph: "play"
                    glyphSize: 14
                    font: Theme.font(12, Theme.boldWeight)
                    enabled: page.service.styleSongs.count > 0
                    onClicked: page.service.play(page.service.styleSongs)
                }
            }
        }
    }
    Component {
        id: filters
        Rectangle {
            implicitHeight: filterBody.implicitHeight + 28
            color: Theme.mutedSurface
            topLeftRadius: Theme.popoverRadius
            topRightRadius: Theme.popoverRadius
            ColumnLayout {
                id: filterBody
                x: 14
                y: 14
                width: Math.max(0, parent.width - 28)
                spacing: 12
                GridLayout {
                    Layout.fillWidth: true
                    columns: page.narrow ? 1 : 2
                    rowSpacing: 12
                    columnSpacing: 16
                    EchoButtonContent {
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        text: page.service.styleSummary
                        horizontalAlignment: Qt.AlignLeft
                        glyph: "tags"
                        glyphSize: 14
                        spacing: 6
                        font: Theme.font(12, Theme.semiboldWeight)
                        color: Theme.secondaryText
                    }
                    EchoTabBar {
                        objectName: "homeStyleGroups"
                        Layout.fillWidth: page.narrow
                        Layout.preferredWidth: page.narrow ? -1 : Math.min(320, filterBody.width)
                        labels: page.groupLabels
                        currentIndex: page.service.activeStyleGroup
                        accessibleName: qsTr("推荐风格分类")
                        onPicked: index => page.service.selectStyleGroup(index)
                    }
                }
                ListView {
                    id: tags
                    objectName: "homeStyleTags"
                    Layout.fillWidth: true
                    Layout.preferredHeight: 34
                    orientation: ListView.Horizontal
                    spacing: 8
                    clip: true
                    model: page.activeTags
                    reuseItems: true
                    EchoViewPoolGuard {
                        targetView: tags
                    }
                    boundsBehavior: Flickable.StopAtBounds
                    delegate: EchoSoftButton {
                        required property var modelData
                        text: modelData.name
                        size: EchoControlSize.Compact
                        font: Theme.font(12, Theme.boldWeight)
                        accent: page.service.selectedTags.indexOf(modelData.id) >= 0
                        onClicked: page.service.toggleTag(modelData.id)
                    }
                }
            }
        }
    }
    Component {
        id: playlistTitle
        Item {
            id: collectionHeading
            property var configuration: parent
            property string kind: configuration.kind
            implicitHeight: 96
            RowLayout {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 16
                height: 44
                Text {
                    Layout.fillWidth: true
                    text: collectionHeading.kind === "playlistHeader" ? qsTr("推荐歌单") : qsTr("编辑精选")
                    font: Theme.font(15, Theme.semiboldWeight)
                    color: Theme.text
                }
                EchoTabBar {
                    objectName: "homePlaylistCategories"
                    visible: collectionHeading.kind === "playlistHeader"
                    Layout.preferredWidth: 200
                    labels: [qsTr("推荐"), "Hi-Res"]
                    currentIndex: page.service.category === "11292" ? 1 : 0
                    accessibleName: qsTr("推荐歌单分类")
                    onPicked: index => page.service.selectCategory(index === 1 ? "11292" : "0")
                }
            }
        }
    }
    Component {
        id: emptyState
        Rectangle {
            id: emptySection
            property var configuration: parent
            property string kind: configuration.kind
            readonly property int part: kind.startsWith("style") ? DiscoveryService.Style : kind.startsWith("playlist") ? DiscoveryService.Playlists : DiscoveryService.Editors
            readonly property bool style: part === DiscoveryService.Style
            implicitHeight: style ? 170 : 160
            color: style ? Theme.mutedSurface : "transparent"
            bottomLeftRadius: style ? Theme.popoverRadius : 0
            bottomRightRadius: style ? Theme.popoverRadius : 0
            ColumnLayout {
                anchors.centerIn: parent
                spacing: 8
                Text {
                    Layout.alignment: Qt.AlignHCenter
                    text: page.service.states[emptySection.part] === DiscoveryService.Error ? page.service.errors[emptySection.part] : emptySection.style ? qsTr("暂无风格推荐") : qsTr("暂无歌单")
                    textFormat: Text.PlainText
                    color: Theme.secondaryText
                    font: Theme.font(12, Theme.regularWeight)
                }
                EchoSoftButton {
                    Layout.alignment: Qt.AlignHCenter
                    visible: page.service.states[emptySection.part] === DiscoveryService.Error
                    text: qsTr("重新加载")
                    onClicked: page.service.retry(emptySection.part)
                }
            }
        }
    }
}
