pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Theme
import EchoMusic.Controls
import EchoMusic.Search

ColumnLayout {
    id: discovery
    objectName: "searchDiscoveryPanel"
    required property SearchModel service
    property ThemeColors colors: Theme.floating
    property bool pageActive: true
    signal keywordPicked(string keyword)
    signal historyEdited
    spacing: 16
    ColumnLayout {
        Layout.fillWidth: true
        visible: !discovery.service.discoveryLoading && discovery.service.searchHistory.length > 0
        spacing: 12
        RowLayout {
            Layout.fillHeight: false
            spacing: 5
            Layout.fillWidth: true
            Text {
                textFormat: Text.PlainText
                text: qsTr("历史搜索")
                color: discovery.colors.text
                font: Theme.font(15, Theme.semiboldWeight)
            }
            Item {
                Layout.fillWidth: true
            }
            EchoIconButton {
                colors: discovery.colors
                glyph: "trash"
                text: qsTr("清空搜索历史")
                padding: 6
                tooltipText: text
                Layout.preferredWidth: 28
                Layout.preferredHeight: 28
                onClicked: {
                    discovery.service.clearHistory();
                    discovery.historyEdited();
                }
            }
        }
        Flow {
            id: chips

            Layout.fillWidth: true
            spacing: 8
            // History is bounded at twenty domain records, not a result list.
            Repeater {
                model: discovery.service.searchHistory
                delegate: EchoActionButton {
                    id: chip
                    required property string modelData
                    text: modelData
                    implicitHeight: 32
                    implicitWidth: Math.min(chips.width, chipContent.implicitWidth + 18)
                    leftPadding: 10
                    rightPadding: 8
                    hoverEnabled: true
                    Accessible.name: text
                    onClicked: discovery.keywordPicked(modelData)
                    transform: Translate {
                        y: discovery.pageActive && chip.hovered && Theme.normalDuration > 0 ? -1 : 0
                        Behavior on y {
                            enabled: discovery.pageActive
                            NumberAnimation {
                                duration: Theme.normalDuration
                            }
                        }
                    }
                    background: Rectangle {
                        color: Theme.withAlpha(discovery.colors.text, chip.hovered ? 0.14 : 0.10)
                        radius: Theme.controlRadius
                    }
                    contentItem: RowLayout {
                        id: chipContent
                        spacing: 6
                        EchoIcon {
                            name: "clock"
                            Layout.preferredWidth: 14
                            Layout.preferredHeight: 14
                            color: discovery.colors.iconMain
                        }
                        Text {
                            textFormat: Text.PlainText
                            Layout.fillWidth: true
                            text: chip.text
                            color: discovery.colors.text
                            font: Theme.font(12, Theme.mediumWeight)
                            elide: Text.ElideRight
                        }
                        EchoIconButton {
                            colors: discovery.colors
                            glyph: "x"
                            text: qsTr("删除历史 %1").arg(chip.text)
                            Layout.preferredWidth: 16
                            Layout.preferredHeight: 16
                            padding: 3
                            onClicked: {
                                discovery.service.removeHistory(chip.modelData);
                                discovery.historyEdited();
                            }
                        }
                    }
                }
            }
        }
        Item {
            Layout.preferredHeight: 8
        }
    }
    Repeater {
        model: discovery.service.discoveryLoading ? 2 : 0
        delegate: Rectangle {
            Layout.fillWidth: true
            implicitHeight: 20 + 20 + 16 + 132 + 20
            color: discovery.colors.hotCard
            radius: Theme.cardRadius
            EchoSkeleton {
                x: 20
                y: 20
                width: 80
                height: 20
                animate: discovery.pageActive
            }
            GridLayout {
                x: 20
                y: 56
                width: parent.width - 40
                columns: 3
                rowSpacing: 4
                columnSpacing: 20
                Repeater {
                    model: 9
                    EchoSkeleton {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 40
                        animate: discovery.pageActive
                    }
                }
            }
        }
    }
    Repeater {
        model: discovery.service.discoveryLoading ? null : discovery.service.hotGroups
        delegate: Rectangle {
            id: group
            required property string groupTitle
            required property list<string> keywords
            required property list<string> reasons
            Layout.fillWidth: true
            implicitHeight: content.implicitHeight + (width <= 360 ? 32 : 40)
            radius: Theme.cardRadius
            color: discovery.colors.hotCard
            ColumnLayout {
                id: content
                x: group.width <= 360 ? 16 : 20
                y: x
                width: group.width - x * 2
                spacing: 16
                Text {
                    textFormat: Text.PlainText
                    text: group.groupTitle
                    color: discovery.colors.text
                    font: Theme.font(17, Theme.strongWeight)
                    Layout.preferredHeight: 24
                }
                GridLayout {
                    id: grid
                    Layout.fillWidth: true
                    flow: GridLayout.TopToBottom
                    columns: discovery.width > 580 ? 3 : discovery.width > 360 ? 2 : 1
                    rows: Math.max(1, Math.ceil(group.keywords.length / columns))
                    columnSpacing: discovery.width > 580 ? 20 : 12
                    rowSpacing: 4
                    Repeater {
                        model: group.keywords
                        delegate: EchoActionButton {
                            id: hot
                            objectName: "searchHotKeyword"
                            required property int index
                            required property string modelData
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            Layout.preferredWidth: (content.width - (grid.columns - 1) * grid.columnSpacing) / grid.columns
                            Layout.maximumWidth: Layout.preferredWidth
                            implicitHeight: 40
                            padding: 4
                            hoverEnabled: true
                            Accessible.name: modelData
                            onClicked: discovery.keywordPicked(modelData)
                            background: Rectangle {
                                radius: Theme.controlRadius
                                color: hot.hovered || hot.visualFocus ? Theme.withAlpha(Theme.accent, 0.10) : "transparent"
                            }
                            contentItem: RowLayout {
                                spacing: 8
                                Text {
                                    textFormat: Text.PlainText
                                    text: hot.index + 1
                                    Layout.preferredWidth: 20
                                    font: Theme.font(14, Theme.semiboldWeight)
                                    color: hot.index < 3 || hot.hovered ? discovery.colors.accentText : discovery.colors.secondaryText
                                }
                                Text {
                                    textFormat: Text.PlainText
                                    text: hot.modelData
                                    Layout.fillWidth: true
                                    elide: Text.ElideRight
                                    font: Theme.font(13, hot.index < 3 ? Theme.semiboldWeight : Theme.regularWeight)
                                    color: hot.hovered ? discovery.colors.accentText : hot.index < 3 ? discovery.colors.text : discovery.colors.secondaryText
                                }
                                Text {
                                    textFormat: Text.PlainText
                                    readonly property string reason: group.reasons[hot.index] || ""
                                    text: reason
                                    visible: reason.length > 0 && reason.length <= 2 && reason !== hot.modelData
                                    color: discovery.colors.accentText
                                    font: Theme.font(10, Theme.regularWeight)
                                }
                            }
                        }
                    }
                }
                Text {
                    textFormat: Text.PlainText
                    visible: !group.keywords.length
                    text: qsTr("暂无热门搜索")
                    color: discovery.colors.secondaryText
                    font: Theme.font(12, Theme.regularWeight)
                }
            }
        }
    }
    Text {
        textFormat: Text.PlainText
        visible: !discovery.service.discoveryLoading && discovery.service.hotGroups.rowCount() === 0
        text: qsTr("暂无热门搜索")
        color: discovery.colors.secondaryText
        font: Theme.font(12, Theme.regularWeight)
    }
}
