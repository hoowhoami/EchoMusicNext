pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Controls
import EchoMusic.Theme
import EchoMusic.Shell

EchoAnchoredPopup {
    id: popup
    objectName: "sidebarLayoutPopup"
    required property SidebarLayoutModel layout
    modal: false
    property bool reorderActive: false
    onAboutToShow: reorderActive = true
    onAboutToHide: reorderActive = false
    background: EchoFloatingSurface {
        border.width: 1
        border.color: Theme.withAlpha(Theme.floatingText, 0.12)
    }
    preferredWidth: 368
    maximumHeight: 620
    padding: 10
    topPadding: 12
    alignEnd: false
    side: EchoAnchoredPopup.Beside
    implicitHeight: body.implicitHeight + topPadding + bottomPadding
    contentItem: ColumnLayout {
        id: body
        spacing: 0
        RowLayout {
            Layout.fillHeight: false
            Layout.fillWidth: true
            Layout.leftMargin: 6
            Layout.rightMargin: 6
            Layout.topMargin: 4
            Layout.bottomMargin: 12
            spacing: 10
            Label {
                Layout.fillWidth: true
                text: qsTr("菜单与歌单")
                color: Theme.floatingText
                font: Theme.font(15, Theme.boldWeight)
            }
            EchoSoftButton {
                text: qsTr("恢复默认")
                implicitHeight: 28
                leftPadding: 8
                rightPadding: 8
                colors: Theme.floating
                font: Theme.font(12, Theme.strongWeight)
                onClicked: popup.layout.restoreDefaults()
            }
        }
        EchoBoundedListView {
            id: sections
            maximumViewportHeight: Math.max(0, popup.contentHeightLimit - 44)
            Layout.fillWidth: true
            Layout.preferredHeight: implicitHeight
            clip: true
            reuseItems: true
            cacheBuffer: 0
            spacing: 9
            model: popup.layout.sectionModel
            ScrollBar.vertical: EchoPopupScrollBar {
                surface: popup
                view: sections
                resourceActive: popup.visible && popup.reorderActive
            }
            delegate: SidebarLayoutSection {
                required property var rowData
                width: sections.width - 2
                sectionData: rowData
                layout: popup.layout
                resourceActive: popup.visible && popup.reorderActive
            }
        }
    }
}
