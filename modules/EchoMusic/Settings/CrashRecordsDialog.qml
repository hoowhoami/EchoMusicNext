pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Controls
import EchoMusic.Theme
import EchoMusic.Settings

EchoDialog {
    id: dialog
    required property DiagnosticService service
    objectName: "crashRecordsDialog"
    heading: qsTr("崩溃记录")
    description: qsTr("记录保存在本机，不会自动上传。")
    descriptionPixelSize: 12
    preferredWidth: 640
    windowHeightMargin: 80
    maximumHeight: 620
    flushBody: true
    onAboutToShow: service.refresh()
    body: ColumnLayout {
        spacing: 10
        Label {
            visible: dialog.service.errorString.length > 0
            Layout.fillWidth: true
            Layout.rightMargin: 24
            text: dialog.service.errorString
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            color: Theme.floatingSecondaryText
            font: Theme.font(12, Theme.regularWeight)
        }
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredHeight: 360
            Layout.minimumHeight: 120
            ListView {
                id: list
                objectName: "crashRecordsList"
                anchors.fill: parent
                clip: true
                model: dialog.service.records
                reuseItems: true
                cacheBuffer: 120
                spacing: 8
                boundsBehavior: Flickable.StopAtBounds
                EchoViewPoolGuard {
                    targetView: list
                }
                delegate: Item {
                    id: row
                    required property var entry
                    width: list.width
                    height: 104
                    Rectangle {
                        anchors.fill: parent
                        anchors.rightMargin: 24
                        radius: Theme.itemRadius
                        color: Theme.floating.mutedSurface
                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: 12
                            spacing: 12
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 5
                                Label {
                                    Layout.fillWidth: true
                                    text: row.entry.kindText + " · " + row.entry.timeText
                                    textFormat: Text.PlainText
                                    elide: Text.ElideRight
                                    font: Theme.font(13, Theme.semiboldWeight)
                                    color: Theme.floatingText
                                }
                                Label {
                                    Layout.fillWidth: true
                                    text: qsTr("版本 %1 · %2").arg(row.entry.version || qsTr("未知")).arg(row.entry.platformText || qsTr("系统未知"))
                                    textFormat: Text.PlainText
                                    elide: Text.ElideRight
                                    font: Theme.font(11, Theme.regularWeight)
                                    color: Theme.floatingSecondaryText
                                }
                                Label {
                                    Layout.fillWidth: true
                                    text: row.entry.hasDump ? qsTr("有崩溃转储") : qsTr("无崩溃转储，可导出已有材料")
                                    textFormat: Text.PlainText
                                    elide: Text.ElideRight
                                    font: Theme.font(11, Theme.regularWeight)
                                    color: Theme.floatingSecondaryText
                                }
                            }
                            EchoSoftButton {
                                objectName: "crashRecordExport"
                                text: qsTr("详情与导出")
                                enabled: !dialog.service.busy
                                colors: Theme.floating
                                onClicked: exportOwner.open({
                                    eventId: row.entry.id
                                })
                            }
                        }
                    }
                }
                ScrollBar.vertical: EchoPopupScrollBar {
                    surface: dialog
                    view: list
                }
            }
            Label {
                anchors.centerIn: parent
                visible: dialog.service.loading || dialog.service.count === 0
                text: dialog.service.loading ? qsTr("正在读取记录…") : qsTr("暂无崩溃记录")
                color: Theme.floatingSecondaryText
                font: Theme.font(13, Theme.regularWeight)
            }
        }
    }
    footerContent: RowLayout {
        Label {
            Layout.fillWidth: true
            text: qsTr("共 %1 条记录").arg(dialog.service.count)
            color: Theme.floatingSecondaryText
            font: Theme.font(12, Theme.regularWeight)
        }
        EchoSoftButton {
            objectName: "refreshCrashRecords"
            text: qsTr("刷新")
            enabled: !dialog.service.loading && !dialog.service.busy
            colors: Theme.floating
            onClicked: dialog.service.refresh()
        }
        EchoSoftButton {
            text: qsTr("关闭")
            colors: Theme.floating
            onClicked: dialog.close()
        }
    }
    EchoPopupLoader {
        id: exportOwner
        objectName: "crashExportOwner"
        hostItem: dialog.contentItem
        sourceComponent: DiagnosticExportDialog {
            service: dialog.service
            uiScale: dialog.uiScale
        }
    }
}
