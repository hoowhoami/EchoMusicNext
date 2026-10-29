pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import EchoMusic.Controls
import EchoMusic.Theme

// A single scroll surface: at most avatarLimit cards in its inline header,
// followed by a virtual grid of names with no cover controls or image sources.
GridView {
    id: directory
    required property SongCatalogModel collection
    property int avatarLimit: 20
    property bool pageActive: true
    property bool navigationAvailable: false
    property EchoPageScrollContainer pageContainer: null
    property Component navigationHeader
    property Component emptyFooter
    readonly property int columns: Math.max(1, Math.floor(width / 200))
    readonly property real cardWidth: Math.max(1, width / columns - 20)
    readonly property real cardHeight: cardWidth + 40
    readonly property int featuredCount: featured.count
    signal openRequested(string id)
    cellWidth: width / columns
    cellHeight: 44
    model: names
    clip: true
    reuseItems: true
    EchoViewPoolGuard {
        targetView: directory
    }
    cacheBuffer: 132
    boundsBehavior: Flickable.StopAtBounds
    ArtistDirectorySliceModel {
        id: featured
        sourceModel: directory.collection
        avatarLimit: directory.avatarLimit
        featured: true
    }
    ArtistDirectorySliceModel {
        id: names
        sourceModel: directory.collection
        avatarLimit: directory.avatarLimit
        featured: false
    }
    ScrollBar.vertical: EchoPageScrollBar {
        objectName: "artistDirectoryScrollbar"
        view: directory
        container: directory.pageContainer
        viewActive: directory.pageActive && directory.visible
    }
    header: Column {
        width: directory.width - 20
        Loader {
            width: parent.width
            sourceComponent: directory.navigationHeader
        }
        Grid {
            id: featuredGrid
            width: directory.width
            columns: directory.columns
            columnSpacing: 20
            rowSpacing: 20
            Repeater {
                // This bounded prefix is deliberately not the full catalog.
                model: featured
                delegate: ArtistDirectoryAvatar {
                    objectName: "artistAvatarCard"
                    width: directory.cardWidth
                    height: directory.cardHeight
                    navigationAvailable: directory.navigationAvailable
                    // Inline GridView headers have their own content origin.
                    // Offscreen cards release both the source and mask texture.
                    resourceActive: directory.pageActive && directory.visible && directory.headerItem !== null && directory.contentY < directory.headerItem.y + featuredGrid.y + y + height && directory.contentY + directory.height > directory.headerItem.y + featuredGrid.y + y
                    onOpenRequested: id => directory.openRequested(id)
                }
            }
        }
        Item {
            width: parent.width
            height: featured.count > 0 && names.count > 0 ? 20 : 0
        }
    }
    footer: Loader {
        width: directory.width - 20
        // The name slice can be empty while featured artists are present.
        active: directory.collection.count === 0
        sourceComponent: directory.emptyFooter
    }
    delegate: EchoSoftButton {
        id: nameButton
        required property string title
        required property string resourceId
        property bool pooled: false
        objectName: "artistNameEntry"
        width: directory.cellWidth - 20
        height: 36
        plain: true
        text: title
        font: Theme.font(16, Theme.regularWeight)
        leftPadding: 0
        rightPadding: 0
        Accessible.name: title
        contentItem: EchoTextTagRow {
            id: nameLabel
            text: nameButton.title
            font: nameButton.font
            color: nameButton.foreground
            fillTextWidth: true
            horizontalAlignment: Text.AlignHCenter
        }
        onClicked: if (directory.navigationAvailable)
            directory.openRequested(resourceId)
        GridView.onPooled: pooled = true
        GridView.onReused: pooled = false
        EchoToolTip {
            parent: nameButton
            requestedVisible: directory.pageActive && !nameButton.pooled && nameButton.hovered && nameLabel.truncated
            text: nameButton.title
        }
    }
}
