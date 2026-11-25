pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Controls
import EchoMusic.Theme

EchoDialog {
    id: popup
    required property RecommendationCards layout
    objectName: "recommendationConfigPopup"
    heading: qsTr("管理推荐")
    preferredWidth: 520
    maximumHeight: parent ? parent.height / uiScale - 24 : 620
    modal: false
    flushBody: true
    windowHeightMargin: 24
    readonly property var cardGlyphs: ({
            daily: "calendar",
            fm: "radio",
            style: "adjustments",
            "3001": "headphones",
            "3004": "diamond",
            "3005": "sparkles",
            "3006": "gift",
            "3014": "heart",
            "3101": "star",
            "3102": "music-share",
            "3103": "arrows-shuffle",
            new: "vinyl",
            "3107": "music-discount"
        })
    x: parent ? parent.width - width * uiScale - 12 * uiScale : 0
    y: 12 * uiScale
    body: Component {
        ListView {
            id: selected
            objectName: "recommendationConfigList"
            implicitHeight: popup.bodyHeightLimit
            clip: true
            spacing: 4
            model: popup.layout
            reuseItems: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: EchoPopupScrollBar {
                surface: popup
                view: selected
                resourceActive: popup.visible
            }
            header: ColumnLayout {
                width: selected.width - 24
                spacing: 16
                Label {
                    Layout.fillWidth: true
                    text: qsTr("拖动调整推荐顺序，首项显示在页面顶部；至少保留一项推荐。")
                    wrapMode: Text.Wrap
                    font: Theme.font(12, Theme.regularWeight)
                    color: Theme.floatingSecondaryText
                }
                RowLayout {
                    Layout.fillWidth: true
                    Layout.bottomMargin: 16
                    Label {
                        Layout.fillWidth: true
                        text: qsTr("已选推荐")
                        font: Theme.font(17, Theme.semiboldWeight)
                        color: Theme.floatingText
                    }
                    EchoSoftButton {
                        text: qsTr("恢复默认")
                        colors: Theme.floating
                        size: EchoControlSize.Small
                        onClicked: popup.layout.restoreDefaults()
                    }
                }
            }
            delegate: EchoReorderRow {
                id: cardRow
                required property RecommendationCard card
                width: selected.width - 24
                reorder: reorderControl
                Rectangle {
                    anchors.fill: parent
                    radius: Theme.controlRadius
                    color: Theme.floating.mutedSurface
                    z: -1
                }
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 12
                    anchors.rightMargin: 12
                    spacing: 12
                    EchoIcon {
                        objectName: "recommendationConfigIcon-" + cardRow.card.key
                        Layout.preferredWidth: 24
                        Layout.preferredHeight: 24
                        name: popup.cardGlyphs[cardRow.card.key] || "music"
                        color: Theme.floatingSecondaryText
                    }
                    Label {
                        Layout.fillWidth: true
                        text: cardRow.card.title
                        elide: Text.ElideRight
                        color: Theme.floatingText
                        font: Theme.font(14, Theme.regularWeight)
                    }
                    EchoIconButton {
                        Layout.preferredWidth: 30
                        Layout.preferredHeight: 30
                        objectName: "recommendationRemove-" + cardRow.card.key
                        glyph: "minus"
                        text: qsTr("移除推荐")
                        colors: Theme.floating
                        enabled: popup.layout.count > 1
                        onClicked: popup.layout.toggle(cardRow.card.key)
                    }
                }
                Keys.onPressed: event => {
                    if ((event.modifiers & Qt.AltModifier) && (event.key === Qt.Key_Up || event.key === Qt.Key_Down)) {
                        popup.layout.move(index, index + (event.key === Qt.Key_Up ? -1 : 1));
                        event.accepted = true;
                    }
                }
            }
            EchoListReorder {
                id: reorderControl
                objectName: "recommendationCardReorder"
                width: selected.width - 24
                view: selected
                sourceModel: popup.layout
                rowHeight: 52
                handleRight: 46
                resourceActive: popup.visible
                snapshotForRow: row => ({
                            title: row.card.title
                        })
                onMoveRequested: (from, to) => {
                    popup.layout.move(from, to);
                    selected.currentIndex = to;
                    selected.forceActiveFocus(Qt.OtherFocusReason);
                }
                dragContent: Component {
                    Rectangle {
                        color: Theme.floating.mutedSurface
                        radius: Theme.controlRadius
                        Label {
                            anchors.fill: parent
                            anchors.margins: 14
                            verticalAlignment: Text.AlignVCenter
                            text: reorderControl.snapshot ? reorderControl.snapshot.title : ""
                            color: Theme.floatingText
                            font: Theme.font(14, Theme.regularWeight)
                            elide: Text.ElideRight
                        }
                    }
                }
            }

            footer: ColumnLayout {
                width: selected.width - 24
                spacing: selected.spacing
                Label {
                    Layout.topMargin: 16
                    Layout.bottomMargin: 8
                    text: qsTr("更多推荐")
                    font: Theme.font(17, Theme.semiboldWeight)
                    color: Theme.floatingText
                }
                // The selectable catalog is a fixed, bounded set of card kinds.
                Repeater {
                    model: popup.layout.catalog.filter(card => !popup.layout.contains(card.key))
                    delegate: Rectangle {
                        id: option
                        required property var modelData
                        Layout.fillWidth: true
                        implicitHeight: reorderControl.rowHeight
                        radius: Theme.controlRadius
                        color: Theme.floating.mutedSurface
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 12
                            anchors.rightMargin: 12
                            spacing: 12
                            EchoIcon {
                                Layout.preferredWidth: 24
                                Layout.preferredHeight: 24
                                name: popup.cardGlyphs[option.modelData.key] || "music"
                                color: Theme.floatingSecondaryText
                            }
                            Label {
                                Layout.fillWidth: true
                                Layout.minimumWidth: 0
                                text: option.modelData.title
                                elide: Text.ElideRight
                                color: Theme.floatingText
                                font: Theme.font(14, Theme.regularWeight)
                            }
                            EchoIconButton {
                                Layout.preferredWidth: 30
                                Layout.preferredHeight: 30
                                objectName: "recommendationAdd-" + option.modelData.key
                                glyph: "plus"
                                text: qsTr("添加推荐")
                                colors: Theme.floating
                                onClicked: popup.layout.toggle(option.modelData.key)
                            }
                        }
                    }
                }
            }
        }
    }
}
