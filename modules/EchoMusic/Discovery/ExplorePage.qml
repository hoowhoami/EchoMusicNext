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
                spacing: 32
                RowLayout {
                    width: body.width
                    Label {
                        Layout.fillWidth: true
                        text: qsTr("推荐歌单")
                        color: Theme.text
                        font: Theme.spacedFont(24, Theme.semiboldWeight, -.6)
                    }
                    EchoSoftButton {
                        objectName: "playlistCategoriesEntry"
                        text: qsTr("分类歌单")
                        glyph: "chevron-right"
                        glyphAfterText: true
                        plain: true
                        onClicked: page.router.navigate("playlist-categories")
                    }
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
                    stateKey: "recommended-" + page.discovery.category
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
                    stateKey: "editors"
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
                    stateKey: "albums-" + page.service.region
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
}
