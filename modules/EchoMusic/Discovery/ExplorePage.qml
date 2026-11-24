pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Shell
import EchoMusic.Controls
import EchoMusic.Theme

RoutePage {
    id: page
    objectName: "explorePage"
    required property ExploreService service
    required property DiscoveryService discovery
    EchoPageScrollContainer {
        id: viewport
        anchors.fill: parent
        pageActive: page.pageActive
        scrollView: scroll
        Flickable {
            id: scroll
            objectName: "playlistSquareView"
            anchors.fill: parent
            contentWidth: width
            contentHeight: body.height + 56
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: EchoPageScrollBar {
                view: scroll
                container: viewport
                viewActive: page.pageActive
            }
            Column {
                id: body
                x: 40
                y: 24
                width: Math.max(0, scroll.width - 80)
                spacing: 28
                Label {
                    text: qsTr("歌单广场")
                    color: Theme.text
                    font: Theme.spacedFont(24, Theme.semiboldWeight, -.6)
                }
                RowLayout {
                    width: body.width
                    Label {
                        Layout.fillWidth: true
                        text: qsTr("为每一种心情，找到合拍的歌单")
                        color: Theme.secondaryText
                        font: Theme.font(12, Theme.regularWeight)
                    }
                    EchoTabBar {
                        Layout.preferredWidth: 180
                        labels: [qsTr("推荐"), "Hi-Res"]
                        currentIndex: page.discovery.category === "11292" ? 1 : 0
                        onPicked: index => page.discovery.selectCategory(index === 1 ? "11292" : "0")
                    }
                }
                PlaylistCollectionSection {
                    id: recommended
                    width: body.width
                    heading: qsTr("推荐歌单")
                    collection: page.discovery.playlists
                    scrollView: scroll
                    contentTop: body.y + y
                    router: page.router
                    resourceActive: page.pageActive
                    loading: page.discovery.states[DiscoveryService.Playlists] < DiscoveryService.Ready
                    errorMessage: page.discovery.errors[DiscoveryService.Playlists]
                    onRetryRequested: page.discovery.retry(DiscoveryService.Playlists)
                }
                PlaylistCollectionSection {
                    width: body.width
                    heading: qsTr("编辑精选")
                    collection: page.discovery.editors
                    scrollView: scroll
                    contentTop: body.y + y
                    router: page.router
                    resourceActive: page.pageActive
                    loading: page.discovery.states[DiscoveryService.Editors] < DiscoveryService.Ready
                    errorMessage: page.discovery.errors[DiscoveryService.Editors]
                    onRetryRequested: page.discovery.retry(DiscoveryService.Editors)
                }
                PlaylistCollectionSection {
                    width: body.width
                    heading: qsTr("新碟上架")
                    headingActions: Component {
                        EchoSoftButton {
                            text: ({
                                    all: qsTr("全部"),
                                    chn: qsTr("华语"),
                                    eur: qsTr("欧美"),
                                    jpn: qsTr("日本"),
                                    kor: qsTr("韩国")
                                })[page.service.region]
                            glyph: "chevron-down"
                            onClicked: regionOwner.open()
                        }
                    }
                    collection: page.service.albums
                    scrollView: scroll
                    contentTop: body.y + y
                    router: page.router
                    targetRoute: "album-detail"
                    resourceActive: page.pageActive
                    loading: page.service.albumsLoading
                    errorMessage: page.service.albumError
                    onRetryRequested: page.service.retryAlbums()
                }
                Rectangle {
                    id: categorySection
                    width: body.width
                    height: categoryBody.height + 28
                    color: Theme.mutedSurface
                    radius: Theme.cardRadius
                    Column {
                        id: categoryBody
                        x: 14
                        y: 14
                        width: Math.max(0, parent.width - 28)
                        spacing: 14
                        RowLayout {
                            width: parent.width
                            Label {
                                Layout.fillWidth: true
                                text: qsTr("歌单")
                                font: Theme.font(15, Theme.semiboldWeight)
                                color: Theme.text
                            }
                            EchoSoftButton {
                                objectName: "playlistCategoryPickerButton"
                                text: page.service.categoryLabel
                                glyph: "chevron-down"
                                onClicked: categoryOwner.open()
                            }
                        }
                        EchoVirtualGridSection {
                            id: categories
                            width: parent.width
                            columns: Math.max(1, Math.floor(width / 260))
                            cellHeight: 72
                            scrollView: scroll
                            contentTop: body.y + categorySection.y + categoryBody.y + y
                            model: page.service.cards
                            itemCount: page.service.cards.count
                            resourceActive: page.pageActive
                            delegate: EchoActionButton {
                                id: row
                                required property string resourceId
                                required property string title
                                required property string extraText
                                required property string coverUrl
                                property bool pooled: false
                                width: categories.cellWidth - 8
                                height: 64
                                hoverEnabled: true
                                padding: 8
                                Accessible.name: title
                                background: Rectangle {
                                    radius: Theme.controlRadius
                                    color: row.hovered ? Theme.rowHover : Theme.hotCard
                                }
                                contentItem: RowLayout {
                                    spacing: 10
                                    EchoCover {
                                        Layout.preferredWidth: 46
                                        Layout.preferredHeight: 46
                                        source: row.coverUrl
                                        resourceActive: page.pageActive && !row.pooled
                                    }
                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        Layout.minimumWidth: 0
                                        spacing: 2
                                        Label {
                                            Layout.fillWidth: true
                                            text: row.title
                                            elide: Text.ElideRight
                                            font: Theme.font(13, Theme.semiboldWeight)
                                            color: Theme.text
                                        }
                                        Label {
                                            Layout.fillWidth: true
                                            text: row.extraText
                                            elide: Text.ElideRight
                                            font: Theme.font(12, Theme.regularWeight)
                                            color: Theme.secondaryText
                                        }
                                    }
                                }
                                onClicked: page.router.navigate("playlist-detail", {}, resourceId)
                                GridView.onPooled: pooled = true
                                GridView.onReused: pooled = false
                            }
                        }
                        Loader {
                            width: parent.width
                            active: page.service.cards.count === 0
                            visible: active
                            sourceComponent: ColumnLayout {
                                Label {
                                    Layout.fillWidth: true
                                    text: page.service.loading ? qsTr("正在发现歌单…") : page.service.errorMessage || qsTr("暂时没有歌单")
                                    color: Theme.secondaryText
                                    font: Theme.font(12, Theme.regularWeight)
                                }
                                EchoSoftButton {
                                    visible: page.service.errorMessage.length > 0
                                    text: qsTr("重试")
                                    onClicked: page.service.retry()
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    EchoPopupLoader {
        id: regionOwner
        hostItem: page
        sourceComponent: EchoOptionPicker {
            heading: qsTr("专辑分类")
            uiScale: page.settings.uiScale
            groups: [
                {
                    label: "",
                    options: [
                        {
                            id: "all",
                            label: qsTr("全部")
                        },
                        {
                            id: "chn",
                            label: qsTr("华语")
                        },
                        {
                            id: "eur",
                            label: qsTr("欧美")
                        },
                        {
                            id: "jpn",
                            label: qsTr("日本")
                        },
                        {
                            id: "kor",
                            label: qsTr("韩国")
                        }
                    ]
                }
            ]
            selectedId: page.service.region
            onChosen: id => page.service.selectRegion(id)
        }
    }
    EchoPopupLoader {
        id: categoryOwner
        hostItem: page
        sourceComponent: EchoOptionPicker {
            objectName: "playlistCategoryPicker"
            heading: qsTr("歌单分类")
            uiScale: page.settings.uiScale
            preferredWidth: 500
            groups: page.service.categories.map(g => ({
                        label: g.name,
                        options: g.children.map(v => ({
                                    id: v.id,
                                    label: v.name
                                }))
                    }))
            selectedId: page.service.category
            onChosen: id => page.service.selectCategory(id)
        }
    }
}
