pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Controls
import EchoMusic.Shell
import EchoMusic.Theme

EchoDrawer {
    id: drawer
    objectName: "playbackSongDetailDrawer"
    required property SongDetailContext detailContext
    required property RouteController router
    signal pageRequested
    preferredWidth: 460
    onAboutToShow: detailContext.setPresented(true)
    onAboutToHide: detailContext.setPresented(false)
    contentItem: ColumnLayout {
        spacing: 0
        EchoDrawerHeader {
            Layout.fillWidth: true
            heading: qsTr("歌曲详情")
            description: drawer.detailContext.detail.title || qsTr("未知歌曲")
            closeLabel: qsTr("关闭歌曲详情")
            onCloseRequested: drawer.close()
            leadingContent: Component {
                EchoCover {
                    implicitWidth: 44
                    implicitHeight: 44
                    source: drawer.detailContext.detail.cover_url || ""
                    resourceActive: drawer.visible
                    cornerRadius: Theme.mediaRadius
                }
            }
        }
        EchoDivider {
            Layout.fillWidth: true
            lineColor: Theme.subtleBorder
        }
        Flickable {
            id: scroll
            objectName: "playbackSongDetailScroll"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            contentHeight: content.implicitHeight + 32
            SongDetailContent {
                id: content
                x: 16
                y: 16
                width: Math.max(0, scroll.width - 32)
                detail: drawer.detailContext
                router: drawer.router
                colors: Theme.floating
                compactInformation: true
                resourceActive: drawer.visible
                onNavigated: drawer.pageRequested()
            }
            ScrollBar.vertical: EchoPopupScrollBar {
                surface: drawer
                view: scroll
                resourceActive: drawer.visible
            }
        }
    }
}
