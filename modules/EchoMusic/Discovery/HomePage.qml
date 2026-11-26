pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Shell
import EchoMusic.Controls
import EchoMusic.Theme

RoutePage {
    id: page
    objectName: "homePage"
    required property DiscoveryService service
    EchoPopupLoader {
        hostItem: page
        objectName: "userAgreementOwner"
        requestedVisible: page.pageActive && !page.settings.userAgreementAccepted
        sourceComponent: UserAgreementDialog {
            settings: page.settings
            uiScale: page.settings.uiScale
            onDeclineRequested: Qt.quit()
        }
    }
    EchoPageScrollContainer {
        id: viewport
        objectName: "recommendationViewport"
        anchors.fill: parent
        pageActive: page.pageActive
        hideScrollbar: configOwner.visible
        scrollView: cards
        onCaptureAnchorRequested: {
            if (scrollTop <= 0)
                return;
            let index = cards.indexAt(cards.width / 2, cards.contentY + 1);
            if (index < 0)
                index = cards.indexAt(cards.width / 2, cards.contentY + cards.spacing + 1);
            const item = cards.itemAtIndex(index);
            if (item)
                capturedAnchor = {
                    id: page.service.recommendationCards.at(index).key,
                    delta: cards.contentY - item.y
                };
        }
        onRestoreAnchorRequested: anchor => {
            for (let index = 0; index < page.service.recommendationCards.count; ++index) {
                if (page.service.recommendationCards.at(index).key !== anchor.id)
                    continue;
                cards.positionViewAtIndex(index, ListView.Beginning);
                cards.forceLayout();
                const item = cards.itemAtIndex(index);
                if (item) {
                    cards.contentY = item.y + anchor.delta;
                    anchorRestored = true;
                }
                return;
            }
        }
        ListView {
            id: cards
            objectName: "recommendationCardsView"
            width: parent.width
            height: parent.height
            topMargin: 108
            bottomMargin: 24
            spacing: 32
            cacheBuffer: 0
            reuseItems: true
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: page.service.recommendationCards
            EchoViewPoolGuard {
                targetView: cards
            }
            ScrollBar.vertical: EchoPageScrollBar {
                objectName: "recommendationPageScrollBar"
                view: cards
                container: viewport
                viewActive: page.pageActive
            }
            delegate: Item {
                id: slot
                required property RecommendationCard card
                property bool pooled: false
                width: cards.width
                height: section.implicitHeight
                ListView.onPooled: pooled = true
                ListView.onReused: pooled = false
                Loader {
                    id: section
                    x: Theme.pageHorizontalInset
                    width: Math.max(0, slot.width - Theme.pageHorizontalInset * 2)
                    readonly property bool resourceActive: page.pageActive && !slot.pooled && slot.y + slot.height > cards.contentY && slot.y < cards.contentY + cards.height
                    sourceComponent: slot.card.key === "fm" ? fmSection : songSection
                    Component {
                        id: songSection
                        RecommendationPanel {
                            width: section.width
                            card: slot.card
                            service: page.service
                            router: page.router
                            uiScale: page.settings.uiScale
                            resourceActive: section.resourceActive
                        }
                    }
                    Component {
                        id: fmSection
                        PersonalFmRecommendation {
                            width: section.width
                            service: page.service
                            router: page.router
                            uiScale: page.settings.uiScale
                            resourceActive: section.resourceActive
                        }
                    }
                }
            }
        }
        EchoPageStickyHeader {
            container: viewport
            flowPosition: 0
            sticky: false
            height: 108
            RowLayout {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.leftMargin: Theme.pageHorizontalInset
                anchors.rightMargin: Theme.pageHorizontalInset
                anchors.topMargin: 24
                height: implicitHeight
                spacing: 12
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    spacing: 5
                    Label {
                        Layout.fillWidth: true
                        text: qsTr("歌曲推荐")
                        elide: Text.ElideRight
                        font: Theme.spacedFont(24, Theme.semiboldWeight, -.6)
                        color: Theme.text
                    }
                    Label {
                        Layout.fillWidth: true
                        text: page.service.greeting + qsTr("，发现下一首心动好歌")
                        elide: Text.ElideRight
                        font: Theme.font(12, Theme.regularWeight)
                        color: Theme.secondaryText
                    }
                }
                EchoIconButton {
                    id: addCard
                    objectName: "recommendationAddCard"
                    Layout.preferredWidth: 36
                    Layout.preferredHeight: 36
                    Layout.alignment: Qt.AlignTop
                    glyph: "plus"
                    text: qsTr("管理推荐")
                    contentItem: Item {
                        EchoIcon {
                            anchors.centerIn: parent
                            width: 25
                            height: 25
                            name: "plus"
                            color: Theme.secondaryText
                        }
                    }
                    onClicked: configOwner.open()
                    background: EchoDashedFrame {
                        resourceActive: page.pageActive
                        strokeColor: Theme.colors.strongBorder
                        fillColor: addCard.hovered ? Theme.hover : "transparent"
                        radius: Theme.cardRadius
                    }
                }
            }
        }
    }
    EchoPopupLoader {
        id: configOwner
        hostItem: page
        sourceComponent: RecommendationConfigPopup {
            layout: page.service.recommendationCards
            uiScale: page.settings.uiScale
        }
    }
}
