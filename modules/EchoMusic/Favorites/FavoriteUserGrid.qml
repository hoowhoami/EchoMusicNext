pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import EchoMusic.Controls
import EchoMusic.Music
import EchoMusic.Theme

// Virtualized followed-singer and user cards. SocialUserModel owns row data;
// only viewport delegates and a bounded loading placeholder set are instantiated.
GridView {
    id: grid
    property var collection: null
    property bool singer: false
    property bool loading: false
    property bool presentationReady: false
    property bool pageActive: true
    property EchoPageScrollContainer pageContainer: null
    property bool navigationAvailable: false
    property string emptyText: ""
    signal openRequested(string id)
    readonly property int columns: Math.max(1, Math.floor(width / (180 + 20)))
    readonly property real cardWidth: Math.max(1, width / columns - 20)
    readonly property real cardHeight: (cardWidth - 26) + 70
    cellWidth: cardWidth + 20
    cellHeight: cardHeight + 20
    clip: true
    reuseItems: true
    cacheBuffer: cellHeight * 3
    boundsBehavior: Flickable.StopAtBounds
    function syncPresentation(): void {
        if (!grid.presentationReady)
            return;
        // Detach rows before switching delegates: integer placeholders have no
        // required SocialUserModel roles, including for pooled user cards.
        // Discard the old pool as well, so cards cannot retain the old row roles.
        const reuse = grid.reuseItems;
        grid.reuseItems = false;
        grid.model = null;
        grid.delegate = grid.loading ? skeletonDelegate : userDelegate;
        grid.model = grid.loading ? grid.columns * 2 : grid.collection;
        grid.reuseItems = reuse;
    }
    onLoadingChanged: syncPresentation()
    onCollectionChanged: if (!grid.loading)
        syncPresentation()
    onColumnsChanged: if (grid.loading)
        syncPresentation()
    Component.onCompleted: {
        presentationReady = true;
        syncPresentation();
    }
    ScrollBar.vertical: EchoPageScrollBar {
        objectName: "favoriteUserScrollbar"
        view: grid
        container: grid.pageContainer
        viewActive: grid.visible && grid.pageActive
    }
    Component {
        id: userDelegate
        FavoriteUserCard {
            id: card
            // Required-role bindings: nickname/avatarUrl stay on the card's own
            // declared properties; the remaining SocialUserModel roles are
            // declared here so the view injects them per row.
            required property var description
            required property var artistId
            required property var userId
            property bool pooled: false
            width: grid.cardWidth
            height: grid.cardHeight
            singer: grid.singer
            targetId: grid.singer ? String(card.artistId ?? "") : String(card.userId ?? "")
            // 此处副标题使用 source_desc；为空时隐藏整行。
            metaText: String(card.description ?? "")
            navigationAvailable: grid.singer && grid.navigationAvailable
            resourceActive: grid.pageActive && grid.visible && !pooled
            GridView.onPooled: pooled = true
            GridView.onReused: pooled = false
            onOpenRequested: id => grid.openRequested(id)
        }
    }
    Component {
        id: skeletonDelegate
        ContentSkeleton {
            property bool pooled: false
            width: grid.cardWidth
            height: grid.cardHeight
            resourceActive: grid.pageActive && grid.visible && !pooled
            GridView.onPooled: pooled = true
            GridView.onReused: pooled = false
        }
    }
    footer: Item {
        width: grid.width
        // The bounded empty state appears only after loading settles.
        height: grid.count === 0 && grid.visible && !grid.loading ? 320 : 0
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            y: 150
            visible: grid.count === 0 && !grid.loading
            text: grid.emptyText
            textFormat: Text.PlainText
            color: Theme.secondaryText
            font: Theme.font(14, Theme.mediumWeight)
        }
    }
}
