pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import EchoMusic.Controls
import EchoMusic.Theme

// The live page and demand-created export use the same bounded composition.
ColumnLayout {
    id: content
    required property real viewportWidth
    property var stats: ({})
    property bool resourceActive: true
    property bool animate: true
    property bool loading: false
    property string errorMessage
    property bool interactive: true
    signal playRequested(string id)
    signal retryRequested
    readonly property int pendingImages: overview.pendingImages + rankings.pendingImages
    spacing: 16
    HistoryStatsOverview {
        id: overview
        Layout.fillWidth: true
        stats: content.stats
        interactive: content.interactive
        onPlayRequested: id => content.playRequested(id)
        resourceActive: content.resourceActive
        animate: content.animate
    }
    HistoryStatsActivity {
        Layout.fillWidth: true
        stats: content.stats
        resourceActive: content.resourceActive
        animate: content.animate
        visible: Number(content.stats.totalSongs || 0) > 0
    }
    HistoryStatsRankings {
        id: rankings
        interactive: content.interactive
        onPlayRequested: id => content.playRequested(id)
        Layout.fillWidth: true
        stats: content.stats
        resourceActive: content.resourceActive
        animate: content.animate
        visible: Number(content.stats.totalSongs || 0) > 0
    }
    Rectangle {
        objectName: "historyStatsEmpty"
        Layout.fillWidth: true
        implicitHeight: emptyBody.implicitHeight + 64
        visible: !Number(content.stats.totalSongs || 0)
        radius: Theme.dialogRadius
        color: Theme.mutedSurface
        ColumnLayout {
            id: emptyBody
            x: 24
            y: 32
            width: Math.max(0, parent.width - 48)
            spacing: 12
            EchoIcon {
                Layout.alignment: Qt.AlignHCenter
                Layout.preferredWidth: 32
                Layout.preferredHeight: 32
                name: "headphones"
                color: Theme.accentText
                resourceActive: content.resourceActive
            }
            EchoTextTagRow {
                objectName: "historyStatsEmptyTitle"
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                fillTextWidth: true
                text: content.loading ? qsTr("正在整理听歌记录") : content.errorMessage ? qsTr("暂时无法读取记录") : qsTr("下一首，就是故事的开始")
                font: Theme.font(18, Theme.semiboldWeight)
                wrap: true
            }
            EchoTextTagRow {
                Layout.fillWidth: true
                fillTextWidth: true
                horizontalAlignment: Text.AlignHCenter
                text: content.loading ? qsTr("你的听歌足迹，马上呈现。") : content.errorMessage || qsTr("播放喜欢的音乐，听歌足迹会慢慢在这里展开。")
                color: Theme.secondaryText
                font: Theme.font(12, Theme.regularWeight)
                wrap: true
            }
            EchoSoftButton {
                objectName: "historyStatsRetry"
                visible: !content.loading && content.errorMessage.length > 0
                Layout.alignment: Qt.AlignHCenter
                text: qsTr("重新读取")
                onClicked: content.retryRequested()
            }
        }
    }
}
