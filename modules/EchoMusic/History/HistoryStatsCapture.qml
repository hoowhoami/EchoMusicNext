pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import EchoMusic.Controls
import EchoMusic.Theme

// Created only by HistoryStatsShare on an actual share action. It is rendered
// outside the live viewport, captured through Qt Quick, then fully destroyed.
Rectangle {
    id: stage
    required property var stats
    required property real viewportWidth
    required property string shareTitle
    required property string updatedAt
    readonly property int pendingImages: content.pendingImages
    objectName: "historyStatsCapture"
    height: column.implicitHeight + 32
    color: Theme.dark ? "#18181b" : "#ffffff"
    Rectangle {
        anchors.fill: parent
        color: Theme.main
    }
    ColumnLayout {
        id: column
        x: 16
        y: 16
        width: Math.max(0, stage.width - 32)
        spacing: 14
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 2
            Layout.rightMargin: 2
            Layout.topMargin: 2
            spacing: 16
            EchoTextTagRow {
                objectName: "historyStatsCaptureTitle"
                Layout.fillWidth: true
                text: stage.shareTitle
                font: Theme.font(22, Theme.extraBoldWeight)
                lineHeight: 26.4
                color: Theme.text
            }
            EchoTextTagRow {
                text: qsTr("更新时间：%1").arg(stage.updatedAt)
                font: Theme.font(12, Theme.boldWeight)
                lineHeight: 16.8
                color: Theme.secondaryText
                opacity: .78
                Layout.alignment: Qt.AlignBottom
            }
        }
        HistoryStatsContent {
            id: content
            Layout.fillWidth: true
            stats: stage.stats
            viewportWidth: stage.viewportWidth
            animate: false
            interactive: false
        }
    }
}
