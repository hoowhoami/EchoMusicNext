pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Controls
import EchoMusic.Shell
import EchoMusic.Theme

Column {
    id: section
    required property SongDetailContext detail
    required property RouteController router
    property ThemeColors colors: Theme.colors
    property bool resourceActive: true
    property bool compactInformation: false
    signal navigated
    property string expandedRanking: ""
    function rankingItems(rank: var): var {
        const matched = section.detail.rankingDetails.filter(item => rank.platform > 0 && item.platform === rank.platform || rank.platform_name && item.platform_name === rank.platform_name);
        return matched.length ? matched : section.detail.rankings.length <= 1 ? section.detail.rankingDetails : [];
    }
    spacing: 16
    SongInformationSection {
        width: parent.width
        detailContext: section.detail
        colors: section.colors
        compact: section.compactInformation
        onNavigated: section.navigated()
        router: section.router
    }
    Repeater {
        model: [
            {
                title: qsTr("可选音质"),
                tags: section.detail.qualities
            },
            {
                title: qsTr("可用音效"),
                tags: section.detail.effects
            }
        ]
        delegate: Column {
            id: tagGroup
            required property var modelData
            width: section.width
            visible: modelData.tags.length > 0
            height: visible ? implicitHeight : 0
            spacing: 12
            Label {
                text: tagGroup.modelData.title
                font: Theme.font(14, Theme.boldWeight)
                color: section.colors.text
            }
            Flow {
                width: parent.width
                spacing: 10
                Repeater {
                    model: tagGroup.modelData.tags
                    delegate: EchoTag {
                        required property string modelData
                        text: modelData
                        tone: EchoTag.Accent
                        size: EchoTag.Small
                    }
                }
            }
        }
    }
    Column {
        width: parent.width
        visible: section.detail.rankings.length > 0
        spacing: 12
        Label {
            text: qsTr("榜单成就")
            font: Theme.font(14, Theme.boldWeight)
            color: section.colors.text
        }
        Label {
            width: parent.width
            visible: String(section.detail.detail.ranking_summary || "").length > 0
            text: "• " + (section.detail.detail.ranking_summary || "")
            color: section.colors.secondaryText
            font: Theme.font(12, Theme.regularWeight)
            wrapMode: Text.Wrap
        }
        Repeater {
            model: section.detail.rankings
            delegate: Rectangle {
                id: ranking
                required property var modelData
                required property int index
                readonly property string cardKey: String(modelData.platform || modelData.platform_name || "rank") + ":" + index
                readonly property bool expanded: section.expandedRanking === cardKey
                readonly property var items: section.rankingItems(modelData)
                width: parent.width
                implicitHeight: rankContent.implicitHeight
                radius: Theme.cardRadius
                color: Theme.withAlpha(section.colors.tone, .06)
                EchoDashedFrame {
                    anchors.fill: parent
                    solid: true
                    strokeColor: Theme.withAlpha(section.colors.tone, .10)
                    radius: Theme.cardRadius
                }
                ColumnLayout {
                    id: rankContent
                    width: parent.width
                    spacing: 0
                    AbstractButton {
                        id: rankToggle
                        EchoActionCursor {}
                        objectName: "songRankingToggle"
                        Layout.fillWidth: true
                        implicitHeight: rankRow.implicitHeight + 36
                        Accessible.name: ranking.modelData.platform_name || qsTr("榜单详情")
                        onClicked: section.expandedRanking = ranking.expanded ? "" : ranking.cardKey
                        contentItem: RowLayout {
                            id: rankRow
                            spacing: 14
                            EchoCover {
                                Layout.leftMargin: 18
                                Layout.preferredWidth: 42
                                Layout.preferredHeight: 42
                                source: (ranking.items.find(item => String(item.logo_pic || "").length) || {}).logo_pic || ranking.modelData.platform_logo || ""
                                resourceActive: section.resourceActive
                                cornerRadius: Theme.cardRadius
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                Layout.minimumWidth: 0
                                spacing: 4
                                Label {
                                    Layout.fillWidth: true
                                    text: ranking.modelData.platform_name || qsTr("未知平台")
                                    color: section.colors.text
                                    font: Theme.font(16, Theme.boldWeight)
                                    elide: Text.ElideRight
                                }
                                Label {
                                    Layout.fillWidth: true
                                    text: qsTr("累计上榜：%1次    最近上榜：%2").arg(ranking.modelData.ranking_times === null ? "—" : ranking.modelData.ranking_times).arg(ranking.modelData.last_time || qsTr("未知"))
                                    font: Theme.font(12, Theme.regularWeight)
                                    color: section.colors.secondaryText
                                    wrapMode: Text.Wrap
                                }
                            }
                            EchoTag {
                                text: qsTr("第 %1 名").arg(ranking.modelData.ranking_num === null ? "—" : ranking.modelData.ranking_num)
                                tone: EchoTag.Accent
                                size: EchoTag.Small
                                font: Theme.font(14, Theme.boldWeight)
                                implicitHeight: 30
                                radius: 15
                            }
                            EchoIcon {
                                Layout.rightMargin: 18
                                name: "chevron-down"
                                rotation: ranking.expanded ? 180 : 0
                                Behavior on rotation {
                                    enabled: section.resourceActive && Theme.store.motionEnabled
                                    NumberAnimation {
                                        duration: 180
                                        easing.type: Easing.OutCubic
                                    }
                                }
                                Layout.preferredWidth: 18
                                Layout.preferredHeight: 18
                                color: section.colors.secondaryText
                            }
                        }
                        background: Rectangle {
                            radius: Theme.cardRadius
                            color: rankToggle.hovered ? Theme.withAlpha(section.colors.tone, .04) : "transparent"
                        }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.leftMargin: 18
                        Layout.rightMargin: 18
                        Layout.bottomMargin: ranking.expanded ? 18 : 0
                        visible: ranking.expanded
                        spacing: 10
                        Repeater {
                            model: ranking.items
                            delegate: RowLayout {
                                id: rankingEntry
                                required property var modelData
                                Layout.fillWidth: true
                                spacing: 12
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    Layout.minimumWidth: 0
                                    Label {
                                        Layout.fillWidth: true
                                        text: rankingEntry.modelData.name || ""
                                        color: section.colors.text
                                        font: Theme.font(13, Theme.semiboldWeight)
                                        elide: Text.ElideRight
                                    }
                                    Label {
                                        text: qsTr("%1 · 累计上榜 %2 次").arg(rankingEntry.modelData.platform_name || "").arg(rankingEntry.modelData.count === null ? "—" : rankingEntry.modelData.count)
                                        color: section.colors.secondaryText
                                        font: Theme.font(11, Theme.regularWeight)
                                    }
                                }
                                Label {
                                    text: rankingEntry.modelData.top_ranking > 0 ? qsTr("最高第 %1 名").arg(rankingEntry.modelData.top_ranking) : rankingEntry.modelData.top_ranking === 0 ? qsTr("暂无排名") : "—"
                                    color: section.colors.accentText
                                    font: Theme.font(12, Theme.semiboldWeight)
                                }
                            }
                        }
                        Label {
                            Layout.fillWidth: true
                            visible: ranking.items.length === 0
                            text: qsTr("暂无更多榜单详情")
                            color: section.colors.secondaryText
                            font: Theme.font(12, Theme.regularWeight)
                        }
                    }
                }
            }
        }
    }
    Label {
        width: parent.width
        visible: section.detail.loading
        text: qsTr("正在加载歌曲详情…")
        color: section.colors.secondaryText
        horizontalAlignment: Text.AlignHCenter
        font: Theme.font(12, Theme.regularWeight)
    }
    Label {
        width: parent.width
        visible: section.detail.errorMessage.length > 0
        text: section.detail.errorMessage
        wrapMode: Text.Wrap
        color: section.colors.secondaryText
        font: Theme.font(12, Theme.regularWeight)
    }
    EchoSoftButton {
        visible: section.detail.errorMessage.length > 0
        text: qsTr("重试")
        onClicked: section.detail.reload()
    }
}
