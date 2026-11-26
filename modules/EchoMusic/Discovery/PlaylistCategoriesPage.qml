pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Shell
import EchoMusic.Controls
import EchoMusic.Music
import EchoMusic.Theme

RoutePage {
    id: page
    objectName: "playlistCategoriesPage"
    required property ExploreService service
    EchoPageScrollContainer {
        id: viewport
        anchors.fill: parent
        pageActive: page.pageActive
        scrollView: grid
        stickyHeaders: [heading]
        ContentGrid {
            id: grid
            objectName: "playlistCategoriesGrid"
            x: Theme.pageHorizontalInset
            width: Math.max(0, parent.width - Theme.pageHorizontalInset * 2 + 20)
            height: parent.height
            topMargin: heading.height + 32
            bottomMargin: 32
            pageContainer: viewport
            pageActive: page.pageActive
            sourceModel: page.service.cards
            loading: page.service.loading && page.service.cards.count === 0
            navigationAvailable: page.router.available("playlist-detail")
            onOpenRequested: id => page.router.navigate("playlist-detail", {}, id)
            footer: Item {
                width: grid.width
                height: !grid.loading && page.service.cards.count === 0 ? 180 : 0
                ColumnLayout {
                    anchors.centerIn: parent
                    visible: parent.height > 0
                    Label {
                        text: page.service.errorMessage || qsTr("暂时没有歌单")
                        color: Theme.secondaryText
                        font: Theme.font(13, Theme.regularWeight)
                    }
                    EchoSoftButton {
                        Layout.alignment: Qt.AlignHCenter
                        visible: page.service.errorMessage.length > 0
                        text: qsTr("重试")
                        onClicked: page.service.retry()
                    }
                }
            }
        }
        EchoPageStickyHeader {
            id: heading
            container: viewport
            flowPosition: 16
            height: page.service.errorMessage.length && page.service.cards.count > 0 ? 144 : 96
            ColumnLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.pageHorizontalInset
                anchors.rightMargin: Theme.pageHorizontalInset
                spacing: 12
                Label {
                    text: qsTr("分类歌单")
                    color: Theme.text
                    font: Theme.spacedFont(24, Theme.semiboldWeight, -.6)
                }
                RowLayout {
                    Layout.fillWidth: true
                    Label {
                        Layout.fillWidth: true
                        text: qsTr("按喜欢的分类发现歌单")
                        color: Theme.secondaryText
                        font: Theme.font(12, Theme.regularWeight)
                    }
                    EchoSoftButton {
                        objectName: "playlistCategoryPickerButton"
                        text: page.service.categoryLabel
                        glyph: "chevron-down"
                        enabled: page.service.categories.length > 0
                        onClicked: categoryOwner.open()
                    }
                }
                RowLayout {
                    visible: page.service.errorMessage.length > 0 && page.service.cards.count > 0
                    Label {
                        text: page.service.errorMessage
                        color: Theme.secondaryText
                    }
                    EchoSoftButton {
                        text: qsTr("重试")
                        onClicked: page.service.retry()
                    }
                }
            }
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
