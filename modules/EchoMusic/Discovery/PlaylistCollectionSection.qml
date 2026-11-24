pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Controls
import EchoMusic.Theme
import EchoMusic.Music
import EchoMusic.Shell

Column {
    id: section
    required property string heading
    required property SongCatalogModel collection
    required property Flickable scrollView
    required property real contentTop
    required property RouteController router
    property Component headingActions
    property string targetRoute: "playlist-detail"
    property bool resourceActive: true
    property bool loading: false
    property string errorMessage: ""
    signal retryRequested
    spacing: 16
    RowLayout {
        width: section.width
        height: 36
        Label {
            Layout.fillWidth: true
            text: section.heading
            color: Theme.text
            font: Theme.font(15, Theme.semiboldWeight)
        }
        Loader {
            active: section.headingActions !== null
            sourceComponent: section.headingActions
        }
    }
    EchoVirtualGridSection {
        id: grid
        objectName: "playlistCollectionGrid"
        width: section.width + 20
        columns: Math.max(1, Math.floor(width / 200))
        cellHeight: Math.ceil(cellWidth + 46)
        scrollView: section.scrollView
        contentTop: section.contentTop + y
        resourceActive: section.resourceActive
        itemCount: section.loading ? columns : section.collection.count
        model: section.loading ? columns : section.collection
        delegate: section.loading ? skeleton : poster
        Component {
            id: poster
            ContentCard {
                property bool pooled: false
                width: grid.cellWidth - 20
                height: width + 44
                durationText: ""
                navigationAvailable: section.router.available(section.targetRoute)
                resourceActive: section.resourceActive && !pooled
                onOpenRequested: id => section.router.navigate(section.targetRoute, {}, id)
                GridView.onPooled: pooled = true
                GridView.onReused: pooled = false
            }
        }
        Component {
            id: skeleton
            ContentSkeleton {
                width: grid.cellWidth - 20
                height: width + 44
                resourceActive: section.resourceActive
            }
        }
    }
    Loader {
        width: section.width
        active: !section.loading && section.collection.count === 0
        visible: active
        sourceComponent: ColumnLayout {
            height: 130
            Label {
                Layout.alignment: Qt.AlignHCenter
                text: section.errorMessage || qsTr("暂时没有内容")
                color: Theme.secondaryText
                font: Theme.font(12, Theme.regularWeight)
            }
            EchoSoftButton {
                Layout.alignment: Qt.AlignHCenter
                visible: section.errorMessage.length > 0
                text: qsTr("重试")
                onClicked: section.retryRequested()
            }
        }
    }
}
