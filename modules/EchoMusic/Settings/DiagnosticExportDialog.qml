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
    property string eventId: ""
    readonly property var event: eventId.length > 0 ? service.record(eventId) : service.latestRecord
    readonly property bool currentSession: !event.id
    property bool includeDump: !currentSession && !!event.hasDump
    property string notes: ""
    property bool exportStarted: false
    property bool showDetails: false
    objectName: "diagnosticExportDialog"
    heading: qsTr("故障诊断")
    description: !service.loading && currentSession ? qsTr("未找到历史故障，将导出本次运行日志。") : ""
    descriptionPixelSize: 12
    preferredWidth: 580
    windowHeightMargin: 70
    maximumHeight: 680
    flushBody: true
    onAboutToShow: {
        service.resetExport();
        service.refresh();
    }
    onClosed: {
        pickerOwner.close();
        if (exportStarted && service.busy)
            service.cancel();
        notes = "";
    }
    Component.onDestruction: if (exportStarted && service.busy)
        service.cancel()
    body: Flickable {
        id: scroll
        implicitHeight: content.implicitHeight
        contentHeight: content.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ColumnLayout {
            id: content
            width: scroll.width - 24
            spacing: 12
            TextEdit {
                visible: !dialog.currentSession
                Layout.fillWidth: true
                text: (dialog.event.kind === "unclean_exit" ? qsTr("运行开始：%1\n版本 %2 · %3") : qsTr("故障时间：%1\n版本 %2 · %3")).arg(dialog.event.timeText || "").arg(dialog.event.version || qsTr("未知")).arg(dialog.event.platformText || qsTr("未知"))
                textFormat: TextEdit.PlainText
                readOnly: true
                selectByMouse: true
                wrapMode: TextEdit.WrapAnywhere
                font: Theme.font(12, Theme.regularWeight)
                color: Theme.floatingSecondaryText
            }
            EchoSoftButton {
                objectName: "diagnosticTechnicalDetails"
                visible: !dialog.currentSession
                text: dialog.showDetails ? qsTr("收起技术详情") : qsTr("技术详情")
                colors: Theme.floating
                onClicked: dialog.showDetails = !dialog.showDetails
            }
            TextEdit {
                objectName: "diagnosticEventDetails"
                visible: !dialog.currentSession && dialog.showDetails
                Layout.fillWidth: true
                text: qsTr("类型：%1\n报告编号：%2\n构建：%3").arg(dialog.event.kindText || "").arg(dialog.event.id || "").arg(dialog.event.buildId || qsTr("未知")) + (dialog.event.detailText && dialog.event.detailText !== "{\n}\n" ? "\n" + dialog.event.detailText : "")
                textFormat: TextEdit.PlainText
                readOnly: true
                selectByMouse: true
                wrapMode: TextEdit.WrapAnywhere
                font: Theme.font(11, Theme.regularWeight)
                color: Theme.floatingSecondaryText
            }
            RowLayout {
                visible: !dialog.currentSession && !!dialog.event.hasDump
                Layout.fillWidth: true
                Label {
                    Layout.fillWidth: true
                    text: qsTr("包含崩溃转储")
                    color: Theme.floatingText
                    font: Theme.font(13, Theme.mediumWeight)
                }
                EchoSwitch {
                    objectName: "diagnosticIncludeDump"
                    checked: dialog.includeDump
                    enabled: !dialog.service.busy
                    Accessible.name: qsTr("包含崩溃转储")
                    onToggled: dialog.includeDump = checked
                }
            }
            Label {
                visible: !dialog.currentSession
                Layout.fillWidth: true
                text: dialog.event.hasDump ? qsTr("转储及附件约 %1 MiB").arg((Number(dialog.event.estimatedBytes || 0) / 1048576).toFixed(1)) : qsTr("未找到崩溃转储，将导出已有日志与记录。")
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
                font: Theme.font(12, Theme.regularWeight)
                color: Theme.floatingSecondaryText
            }
            SettingsNotice {
                visible: dialog.includeDump
                text: qsTr("崩溃转储可能包含内存中的个人信息，无法保证彻底脱敏。请仅发送给可信的排查人员。")
            }
            Label {
                Layout.fillWidth: true
                text: qsTr("问题描述（选填）")
                color: Theme.floatingText
                font: Theme.font(12, Theme.mediumWeight)
            }
            EchoTextArea {
                objectName: "diagnosticNotes"
                Layout.fillWidth: true
                Layout.preferredHeight: 100
                enabled: !dialog.service.busy
                text: dialog.notes
                colors: Theme.floating
                placeholderText: qsTr("出问题前做了什么？请勿填写密码或凭证。")
                Accessible.name: qsTr("问题描述")
                onTextChanged: {
                    if (text.length > 2000)
                        remove(2000, text.length);
                    dialog.notes = text;
                }
            }
            ProgressBar {
                visible: dialog.exportStarted && dialog.service.busy
                Layout.fillWidth: true
                from: 0
                to: 100
                value: dialog.service.progress
                Accessible.name: qsTr("诊断包导出进度")
            }
            Label {
                objectName: "diagnosticExportStatus"
                visible: dialog.exportStarted || dialog.service.errorString.length > 0
                Layout.fillWidth: true
                text: dialog.service.errorString || (dialog.exportStarted ? dialog.service.status : "")
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
                font: Theme.font(12, Theme.regularWeight)
                color: Theme.floatingSecondaryText
            }
        }
        ScrollBar.vertical: EchoPopupScrollBar {
            surface: dialog
            view: scroll
        }
    }
    footerContent: RowLayout {
        Item {
            Layout.fillWidth: true
        }
        EchoSoftButton {
            text: dialog.service.busy && dialog.exportStarted ? qsTr("取消导出") : qsTr("关闭")
            colors: Theme.floating
            onClicked: dialog.service.busy && dialog.exportStarted ? dialog.service.cancel() : dialog.close()
        }
        EchoSoftButton {
            objectName: "diagnosticShowExport"
            visible: dialog.exportStarted && !dialog.service.busy && dialog.service.exportedFile.toString().length > 0
            text: qsTr("打开所在目录")
            colors: Theme.floating
            onClicked: dialog.service.revealExport()
        }
        EchoSoftButton {
            objectName: "diagnosticSaveZip"
            text: qsTr("选择位置并导出")
            enabled: !dialog.service.busy && !dialog.service.loading && (dialog.currentSession || !!dialog.event.id)
            accent: true
            colors: Theme.floating
            onClicked: pickerOwner.open()
        }
    }
    EchoFileDialogLoader {
        id: pickerOwner
        objectName: "diagnosticSavePickerOwner"
        hostItem: dialog.contentItem
        sourceComponent: EchoFileDialog {
            objectName: "diagnosticSavePicker"
            title: qsTr("保存诊断包")
            fileMode: EchoFileDialog.SaveFile
            nameFilters: [qsTr("ZIP 压缩文件 (*.zip)")]
            defaultSuffix: "zip"
            currentFile: dialog.service.suggestedFileName(dialog.event.id || "")
            onAccepted: {
                dialog.exportStarted = true;
                if (dialog.currentSession)
                    dialog.service.exportCurrent(selectedFile, dialog.notes);
                else
                    dialog.service.exportCrash(dialog.event.id, selectedFile, dialog.includeDump, dialog.notes);
            }
        }
    }
}
