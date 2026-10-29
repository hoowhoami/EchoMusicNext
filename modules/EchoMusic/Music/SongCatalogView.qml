pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Controls
import EchoMusic.Theme

Item {
    id: page
    objectName: "songCatalogView"
    required property SongCatalogModel collection
    required property SongCatalogActions actions
    required property Component headerComponent
    // Album/playlist comments share this sticky header and scroll coordinator.
    property Component secondaryBody
    property Flickable secondaryView: null
    property bool secondaryActive: false
    property string secondaryTabLabel: qsTr("评论")
    property string secondaryTabCount: ""
    signal tabPicked(int index)
    readonly property EchoPageScrollContainer pageContainer: viewport
    property Component extraTabs
    property Component extraActions
    property real expandedHeaderHeight: 176
    property real contentInset: 24
    property real headerTopMargin: 0
    // Explore uses a 36px control row with 10px bottom padding; detail
    // pages retain their centered 56px row. Both share the same toolbar.
    property bool compactSongToolbar: false
    /// Shared song tab label; FreeListen follows the user's 歌曲 correction.
    property string songTabLabel: qsTr("歌曲")
    /// Song-tab variants:
    /// - "tab": 36px tab box centered in the 56px toolbar, with a 2px underline.
    /// - "explore": compact 36px box at the top of the 46px toolbar.
    /// Label and badge geometry is shared with the corresponding detail controls.
    property string songTabVariant: "tab"
    /// Server-side total for paginated catalogs (cloud); undefined keeps the
    /// loaded collection count for every existing page.
    property var songTabBadgeCount: undefined
    /// The row with an open context menu retains its context-target fill.
    property string menuSongId: ""
    property bool showSortHeader: true
    /// Pages can hide the song toolbar for empty or failed results.
    property bool showToolbar: true
    /// Optional page-specific empty placeholder. Null keeps the shared message and retry.
    property Component emptyComponent: null
    readonly property real songToolbarRowHeight: compactSongToolbar ? 46 : 56
    readonly property real toolbarHeight: showToolbar ? songToolbarRowHeight + (showSortHeader && !secondaryActive ? 44 : 0) : 0
    property bool loading: false
    property string errorMessage: ""
    property bool resourceActive: true
    readonly property string activeTrackId: actions.currentTrackId
    readonly property real scrollTop: viewport.scrollTop
    signal retryRequested
    signal moreRequested(string id, real sceneX, real sceneY)
    EchoPageScrollContainer {
        id: viewport
        anchors.fill: parent
        pageActive: page.resourceActive
        scrollView: page.secondaryActive && page.secondaryView ? page.secondaryView : songList.listView
        coordinatedViews: page.secondaryView ? [songList.listView, page.secondaryView] : [songList.listView]
        resetViews: coordinatedViews
        stickyHeaders: [header, toolbar]
        CatalogSongList {
            id: songList
            visible: !page.secondaryActive
            anchors.fill: parent
            collection: page.collection
            actions: page.actions
            pageContainer: viewport
            contentInset: page.contentInset
            topMargin: page.headerTopMargin + page.expandedHeaderHeight + page.toolbarHeight
            bottomMargin: 48
            loading: page.loading
            errorMessage: page.errorMessage
            resourceActive: page.resourceActive
            emptyComponent: page.emptyComponent
            menuSongId: page.menuSongId
            onRetryRequested: page.retryRequested()
            onMoreRequested: (id, sceneX, sceneY) => page.moreRequested(id, sceneX, sceneY)
        }
        Loader {
            id: secondaryLoader
            anchors.fill: parent
            active: page.secondaryActive && page.secondaryBody !== null
            visible: page.secondaryActive
            sourceComponent: page.secondaryBody
        }
        EchoPageStickyHeader {
            id: header
            container: viewport
            flowPosition: page.headerTopMargin
            height: (heading.item as Item)?.implicitHeight ?? page.expandedHeaderHeight
            Loader {
                id: heading
                anchors.fill: parent
                sourceComponent: page.headerComponent
            }
        }
        EchoPageStickyHeader {
            id: toolbar
            container: viewport
            visible: page.showToolbar
            flowPosition: page.headerTopMargin + page.expandedHeaderHeight
            stickyTop: header.height
            height: page.toolbarHeight
            // Clip rows below sticky layers and reuse the main skin.
            // An extra opaque or blurred surface would obscure it.
            Rectangle {
                objectName: "songCatalogTabDivider"
                x: page.contentInset
                y: page.songToolbarRowHeight - 1
                width: Math.max(0, parent.width - page.contentInset * 2)
                height: 1
                color: Theme.subtleBorder
            }
            ColumnLayout {
                anchors.fill: parent
                spacing: 0
                RowLayout {
                    objectName: "songCatalogToolbarRow"
                    Layout.fillWidth: true
                    Layout.preferredHeight: page.songToolbarRowHeight
                    Layout.minimumHeight: page.songToolbarRowHeight
                    Layout.maximumHeight: page.songToolbarRowHeight
                    Layout.fillHeight: false
                    Layout.leftMargin: page.contentInset
                    Layout.rightMargin: page.contentInset
                    spacing: 8
                    Item {
                        id: songTabSlot
                        visible: page.secondaryBody === null
                        readonly property bool exploreTab: page.songTabVariant === "explore"
                        objectName: "songCatalogTabSlot"
                        implicitWidth: tab.implicitWidth
                        Layout.preferredHeight: 36
                        Layout.minimumWidth: implicitWidth
                        Layout.fillHeight: false
                        Layout.alignment: songTabSlot.exploreTab ? Qt.AlignTop : Qt.AlignVCenter
                        EchoTextTagRow {
                            id: tab
                            objectName: "songCatalogCountRow"
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            // The two toolbar variants use distinct label-to-underline spacing.
                            anchors.bottomMargin: songTabSlot.exploreTab ? 6 : 4
                            text: page.songTabLabel
                            font: songTabSlot.exploreTab ? Theme.font(14, Theme.semiboldWeight) : Theme.font(15, Theme.boldWeight)
                            // The song tab is the active tab on every page.
                            color: Theme.text
                            spacing: 4
                            wrapTags: false
                            EchoBadge {
                                objectName: "songCatalogCountBadge"
                                count: page.songTabBadgeCount !== undefined ? page.songTabBadgeCount : page.collection.totalCount
                                tone: EchoBadge.Neutral
                            }
                        }
                        Rectangle {
                            objectName: "songCatalogTabUnderline"
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            height: 2
                            radius: 1
                            // The compact toolbar uses a 70% accent underline.
                            color: songTabSlot.exploreTab ? Theme.withAlpha(Theme.accent, 0.7) : Theme.accent
                        }
                    }
                    EchoDetailTabs {
                        visible: page.secondaryBody !== null
                        Layout.fillHeight: true
                        labels: [page.songTabLabel, page.secondaryTabLabel]
                        counts: [page.songTabBadgeCount !== undefined ? page.songTabBadgeCount : page.collection.totalCount, page.secondaryTabCount]
                        currentIndex: page.secondaryActive ? 1 : 0
                        onPicked: index => page.tabPicked(index)
                    }
                    Loader {
                        sourceComponent: page.extraTabs
                        visible: sourceComponent !== null
                        Layout.fillHeight: true
                    }
                    Item {
                        Layout.fillWidth: true
                    }
                    Loader {
                        sourceComponent: page.extraActions
                        visible: sourceComponent !== null && !page.secondaryActive
                    }
                    EchoTextField {
                        visible: !page.secondaryActive
                        objectName: "discoverySongFilter"
                        Layout.preferredWidth: 160
                        Layout.minimumWidth: 80
                        Layout.alignment: page.compactSongToolbar ? Qt.AlignTop : Qt.AlignVCenter
                        placeholderText: qsTr("搜索歌曲...")
                        text: page.collection.query
                        clearable: true
                        onTextEdited: page.collection.query = text
                    }
                    SongLocateButton {
                        visible: !page.secondaryActive
                        objectName: "discoveryLocate"
                        Layout.alignment: page.compactSongToolbar ? Qt.AlignTop : Qt.AlignVCenter
                        onClicked: {
                            const index = page.collection.indexOf(page.activeTrackId);
                            if (index >= 0)
                                EchoListPositioning.positionIfNeeded(songList.listView, index, ListView.Center, songList.pageContainer);
                        }
                    }
                }
                SongListHeader {
                    visible: page.showSortHeader && !page.secondaryActive
                    Layout.fillWidth: true
                    Layout.leftMargin: page.contentInset
                    Layout.rightMargin: page.contentInset
                    Layout.preferredHeight: page.showSortHeader ? 44 : 0
                    Layout.fillHeight: false
                    geometry: songList.geometry
                    sortField: page.collection.sortField
                    sortDirection: page.collection.sortDirection
                    onSortPicked: field => page.collection.toggleSort(field)
                }
            }
        }
    }
}
