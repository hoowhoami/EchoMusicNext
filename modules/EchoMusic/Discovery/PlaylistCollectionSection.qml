pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Controls
import EchoMusic.Theme
import EchoMusic.Music
import EchoMusic.Shell

Item {
    id: collectionSection
    objectName: "playlistSection-" + stateKey
    required property string heading
    required property string stateKey
    required property SongCatalogModel collection
    required property Flickable scrollView
    required property real contentTop
    required property RouteController router
    property Component headingActions
    property string targetRoute: "playlist-detail"
    property bool resourceActive: true
    property bool loading: false
    property string errorMessage: ""
    property int previewIndex: 0
    readonly property int columns: Math.max(1, Math.floor((content.width + 20) / 200))
    readonly property real cardWidth: Math.max(0, Math.floor((content.width - (columns - 1) * 20) / columns))
    readonly property bool skeletons: loading && collection.count === 0
    readonly property bool empty: !loading && collection.count === 0
    readonly property bool inViewport: resourceActive && scrollView.height > 0 && contentTop + height > scrollView.contentY && contentTop < scrollView.contentY + scrollView.height
    signal retryRequested
    implicitHeight: content.height + 32
    function rememberIndex(index: int): void {
        previewIndex = Math.max(0, index);
        EchoPageState.save("playlist-carousel-" + stateKey, {
            index: previewIndex
        });
    }
    function restoreIndex(): void {
        previewIndex = EchoPageState.load("playlist-carousel-" + stateKey).index || 0;
    }
    onStateKeyChanged: restoreIndex()
    Component.onCompleted: restoreIndex()
    Rectangle {
        anchors.fill: parent
        radius: Theme.cardRadius
        color: Theme.withAlpha(Theme.text, Theme.dark ? .06 : .035)
    }
    Column {
        id: content
        x: 16
        y: 16
        width: Math.max(0, collectionSection.width - 32)
        spacing: 16
        RowLayout {
            width: parent.width
            height: 36
            Label {
                Layout.fillWidth: true
                text: collectionSection.heading
                color: Theme.text
                font: Theme.font(15, Theme.semiboldWeight)
            }
            Loader {
                active: collectionSection.headingActions !== null
                sourceComponent: collectionSection.headingActions
            }
        }
        Loader {
            id: viewport
            width: parent.width
            height: collectionSection.empty ? 130 : collectionSection.cardWidth + 70
            active: collectionSection.inViewport && width > 0
            asynchronous: true
            sourceComponent: collectionSection.empty ? emptyContent : carouselContent
        }
    }
    Component {
        id: carouselContent
        EchoCarouselView {
            id: carousel
            objectName: "playlistCarousel-" + collectionSection.stateKey
            property bool viewportReady: false
            property bool imagesReady: false
            width: viewportReady ? viewport.width : 0
            height: viewportReady ? viewport.height : 0
            resourceActive: collectionSection.inViewport
            navigationPrefix: "playlist"
            navigationKey: collectionSection.stateKey
            initialIndex: collectionSection.previewIndex
            spacing: 20
            previewWidth: collectionSection.cardWidth
            activeFocusOnTab: !collectionSection.skeletons && count > 1
            onIndexRemembered: index => {
                if (!collectionSection.skeletons)
                    collectionSection.rememberIndex(index);
            }
            function syncPresentation(): void {
                imagesReady = false;
                viewportReady = false;
                restored = false;
                reuseItems = false;
                model = null;
                delegate = collectionSection.skeletons ? skeleton : poster;
                model = collectionSection.skeletons ? collectionSection.columns : collectionSection.collection;
                currentIndex = Math.max(0, Math.min(collectionSection.previewIndex, count - 1));
                positionViewAtIndex(currentIndex, ListView.Beginning);
                viewportReady = true;
                forceLayout();
                positionViewAtIndex(currentIndex, ListView.Beginning);
                imagesReady = true;
                reuseItems = true;
            }
            Component.onCompleted: syncPresentation()
            Connections {
                target: collectionSection
                function onSkeletonsChanged(): void {
                    carousel.syncPresentation();
                }
                function onStateKeyChanged(): void {
                    carousel.syncPresentation();
                }
            }
            Component {
                id: poster
                ContentCard {
                    property bool pooled: false
                    width: collectionSection.cardWidth
                    height: width + 70
                    durationText: ""
                    navigationAvailable: collectionSection.router.available(collectionSection.targetRoute)
                    resourceActive: collectionSection.inViewport && carousel.imagesReady && !pooled
                    ListView.onPooled: pooled = true
                    ListView.onReused: pooled = false
                    onOpenRequested: id => collectionSection.router.navigate(collectionSection.targetRoute, {}, id)
                }
            }
            Component {
                id: skeleton
                ContentSkeleton {
                    property bool pooled: false
                    width: collectionSection.cardWidth
                    height: width + 70
                    resourceActive: collectionSection.inViewport && !pooled
                    ListView.onPooled: pooled = true
                    ListView.onReused: pooled = false
                }
            }
        }
    }
    Component {
        id: emptyContent
        ColumnLayout {
            Label {
                Layout.alignment: Qt.AlignHCenter
                text: collectionSection.errorMessage || qsTr("暂时没有内容")
                color: Theme.secondaryText
                font: Theme.font(12, Theme.regularWeight)
            }
            EchoSoftButton {
                Layout.alignment: Qt.AlignHCenter
                visible: collectionSection.errorMessage.length > 0
                text: qsTr("重试")
                onClicked: collectionSection.retryRequested()
            }
        }
    }
}
