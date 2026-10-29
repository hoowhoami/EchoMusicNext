pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Controls
import EchoMusic.Theme
import EchoMusic.Library

ColumnLayout {
    id: pane
    required property PlaylistImportService importer
    required property EchoDialog surface
    readonly property var activeStage: importer.stages.find(stage => stage.status === "matching" || stage.status === "adding") || importer.stages.find(stage => stage.status === "pending") || ({
            title: "",
            artist: ""
        })
    spacing: 12
    Label {
        Layout.fillWidth: true
        text: pane.importer.busy ? qsTr("正在导入 · %1 / %2").arg(pane.importer.total ? pane.importer.imported : 1).arg(pane.importer.total || 4) : qsTr("已处理 %1 首").arg(pane.importer.total || (pane.importer.phase === PlaylistImportService.Failed ? 1 : 0))
        wrapMode: Text.Wrap
        color: Theme.floatingText
        font: Theme.font(12, Theme.mediumWeight)
    }
    Rectangle {
        Layout.fillWidth: true
        implicitHeight: 6
        radius: 3
        color: Theme.floating.mutedSurface
        Rectangle {
            width: parent.width * pane.importer.progress
            height: parent.height
            radius: 3
            color: pane.importer.phase === PlaylistImportService.Completed ? "#10b981" : Theme.floating.accent
        }
    }
    Rectangle {
        Layout.fillWidth: true
        implicitHeight: 76
        visible: pane.importer.busy
        radius: Theme.cardRadius
        color: Theme.mix(Theme.floating.mutedSurface, Theme.floating.accent, 0.08)
        border.color: Theme.withAlpha(Theme.floating.accent, 0.22)
        RowLayout {
            anchors.fill: parent
            anchors.topMargin: 12
            anchors.bottomMargin: 12
            anchors.leftMargin: 16
            anchors.rightMargin: 16
            spacing: 12
            Rectangle {
                Layout.preferredWidth: 32
                Layout.preferredHeight: 32
                radius: 16
                color: Theme.withAlpha(Theme.floating.accent, 0.16)
                Row {
                    anchors.centerIn: parent
                    spacing: 2
                    Repeater {
                        model: 4
                        delegate: Rectangle {
                            id: wave
                            required property int index
                            width: 2
                            height: 9
                            radius: 1
                            color: Theme.floating.accent
                            SequentialAnimation on height {
                                running: pane.surface.opened && pane.importer.busy
                                loops: Animation.Infinite
                                PauseAnimation {
                                    duration: wave.index * 120
                                }
                                NumberAnimation {
                                    to: 18
                                    duration: 450
                                    easing.type: Easing.InOutQuad
                                }
                                NumberAnimation {
                                    to: 7
                                    duration: 450
                                    easing.type: Easing.InOutQuad
                                }
                            }
                        }
                    }
                }
            }
            ColumnLayout {
                Layout.fillHeight: false
                Layout.fillWidth: true
                spacing: 2
                Label {
                    text: qsTr("正在处理")
                    color: Theme.floatingSecondaryText
                    font: Theme.font(10, Theme.regularWeight)
                }
                Label {
                    Layout.fillWidth: true
                    text: pane.activeStage.title
                    elide: Text.ElideRight
                    color: Theme.floatingText
                    font: Theme.font(13, Theme.semiboldWeight)
                }
                Label {
                    Layout.fillWidth: true
                    text: pane.activeStage.artist
                    elide: Text.ElideRight
                    color: Theme.floatingSecondaryText
                    font: Theme.font(11, Theme.regularWeight)
                }
            }
        }
    }
    ListView {
        id: missedList
        objectName: "playlistImportMissed"
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.minimumHeight: 92
        Layout.preferredHeight: Math.min(320, (pane.importer.stages.length + count) * 58)
        clip: true
        model: pane.importer
        reuseItems: true
        EchoViewPoolGuard {
            targetView: missedList
        }
        cacheBuffer: 240
        boundsBehavior: Flickable.StopAtBounds
        Rectangle {
            anchors.fill: parent
            z: -1
            radius: Theme.cardRadius
            color: Theme.floating.mutedSurface
        }
        ScrollBar.vertical: EchoPopupScrollBar {
            surface: pane.surface
            view: missedList
            resourceActive: pane.surface.opened
        }
        header: Column {
            width: missedList.width
            Repeater {
                model: pane.importer.stages
                delegate: Item {
                    id: stageRow
                    required property var modelData
                    required property int index
                    width: missedList.width
                    height: 58
                    Rectangle {
                        anchors.bottom: parent.bottom
                        width: parent.width
                        height: 1
                        visible: stageRow.index < pane.importer.stages.length - 1 || missedList.count > 0
                        color: Theme.floating.controlBorder
                    }
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 16
                        anchors.rightMargin: 16
                        spacing: 12
                        Rectangle {
                            Layout.preferredWidth: 6
                            Layout.preferredHeight: 6
                            radius: 3
                            color: stageRow.modelData.status === "success" ? "#10b981" : stageRow.modelData.status === "failed" ? Theme.floating.danger : Theme.floating.accent
                        }
                        ColumnLayout {
                            Layout.fillHeight: false
                            Layout.fillWidth: true
                            spacing: 2
                            Label {
                                Layout.fillWidth: true
                                text: stageRow.modelData.title
                                elide: Text.ElideRight
                                color: Theme.floatingText
                                font: Theme.font(13, Theme.mediumWeight)
                            }
                            Label {
                                Layout.fillWidth: true
                                text: stageRow.modelData.artist
                                elide: Text.ElideRight
                                color: Theme.floatingSecondaryText
                                font: Theme.font(11, Theme.regularWeight)
                            }
                        }
                        Label {
                            text: stageRow.modelData.status === "success" ? qsTr("完成") : stageRow.modelData.status === "failed" ? qsTr("失败") : stageRow.modelData.status === "pending" ? qsTr("等待") : stageRow.modelData.status === "matching" ? qsTr("解析中") : qsTr("导入中")
                            color: Theme.floatingSecondaryText
                            font: Theme.font(11, Theme.regularWeight)
                        }
                    }
                }
            }
        }
        delegate: Item {
            id: missedRow
            required property int index
            required property string trackTitle
            required property string artist
            required property string reason
            width: missedList.width
            height: 58
            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                visible: missedRow.index < missedList.count - 1
                color: Theme.floating.controlBorder
            }
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 16
                spacing: 12
                Rectangle {
                    Layout.preferredWidth: 6
                    Layout.preferredHeight: 6
                    radius: 3
                    color: "#f59e0b"
                }
                ColumnLayout {
                    Layout.fillHeight: false
                    Layout.fillWidth: true
                    spacing: 2
                    Label {
                        Layout.fillWidth: true
                        text: missedRow.trackTitle
                        elide: Text.ElideRight
                        color: Theme.floatingText
                        font: Theme.font(13, Theme.mediumWeight)
                    }
                    Label {
                        Layout.fillWidth: true
                        text: missedRow.artist + (missedRow.reason.length ? " · " + missedRow.reason : "")
                        elide: Text.ElideRight
                        color: Theme.floatingSecondaryText
                        font: Theme.font(11, Theme.regularWeight)
                    }
                }
                Label {
                    text: qsTr("未匹配")
                    color: Theme.floatingSecondaryText
                    font: Theme.font(11, Theme.regularWeight)
                }
            }
        }
    }

    Label {
        visible: pane.importer.busy
        Layout.fillWidth: true
        text: pane.importer.cloudTask ? qsTr("停止查看不会取消当前任务，稍后仍可在歌单列表中查看导入结果。") : qsTr("停止导入后将不再继续上传或添加歌曲，已提交的内容会保留。")
        wrapMode: Text.Wrap
        color: Theme.floatingSecondaryText
        font: Theme.font(12, Theme.regularWeight)
    }
}
