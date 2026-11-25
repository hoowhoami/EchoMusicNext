pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import EchoMusic.Controls
import EchoMusic.Theme

// One native scroll surface. Avatar-only directories virtualize the whole
// catalog; mixed directories retain the bounded featured prefix and name rows.
GridView {
    id: directory
    // Both in-memory catalogs and windowed directory models provide these roles.
    required property var collection
    property string errorMessage: ""
    property bool avatarsOnly: false
    property int avatarLimit: 20
    property bool pageActive: true
    property bool navigationAvailable: false
    property EchoPageScrollContainer pageContainer: null
    property Component navigationHeader
    property Component emptyFooter
    readonly property int columns: Math.max(1, Math.floor(width / 200))
    readonly property real cardWidth: Math.max(1, width / columns - 20)
    readonly property real cardHeight: cardWidth + 44
    readonly property int featuredCount: featured.count
    signal openRequested(string id)
    cellWidth: width / columns
    cellHeight: avatarsOnly ? cardHeight + 20 : 44
    model: avatarsOnly ? collection : names
    clip: true
    reuseItems: true
    EchoViewPoolGuard {
        targetView: directory
    }
    cacheBuffer: avatarsOnly ? cellHeight : 132
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
            visible: !directory.avatarsOnly
            width: directory.width
            columns: directory.columns
            columnSpacing: 20
            rowSpacing: 20
            Repeater {
                // This bounded prefix is deliberately not the full catalog.
                model: directory.avatarsOnly ? null : featured
                delegate: ArtistDirectoryAvatar {
                    required presentationReady
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
            height: !directory.avatarsOnly && featured.count > 0 && names.count > 0 ? 20 : 0
        }
    }
    footer: Loader {
        width: directory.width - 20
        // The name slice can be empty while featured artists are present.
        active: directory.collection.count === 0 || directory.errorMessage.length > 0
        sourceComponent: directory.emptyFooter
    }
    delegate: avatarsOnly ? avatarDelegate : nameDelegate
    Component {
        id: avatarDelegate
        ArtistDirectoryAvatar {
            required presentationReady
            property bool pooled: false
            objectName: "artistAvatarCard"
            width: directory.cardWidth
            height: directory.cardHeight
            navigationAvailable: directory.navigationAvailable
            resourceActive: directory.pageActive && directory.visible && !pooled
            GridView.onPooled: pooled = true
            GridView.onReused: pooled = false
            onOpenRequested: id => directory.openRequested(id)
        }
    }
    Component {
        id: nameDelegate
        EchoSoftButton {
            id: nameButton
            required property string title
            required property string resourceId
            required property bool presentationReady
            property bool pooled: false
            objectName: "artistNameEntry"
            width: directory.cellWidth - 20
            height: 36
            plain: true
            enabled: presentationReady
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
                visible: nameButton.presentationReady
            }
            Loader {
                anchors.centerIn: parent
                width: parent.width * .65
                height: 14
                active: !nameButton.presentationReady && directory.pageActive && !nameButton.pooled
                sourceComponent: EchoSkeleton {
                    animate: directory.pageActive
                }
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
}
