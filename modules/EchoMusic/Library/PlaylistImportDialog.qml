pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Controls
import EchoMusic.Theme
import EchoMusic.Library

EchoDialog {
    id: dialog
    objectName: "playlistImportDialog"
    required property LibraryController service
    readonly property PlaylistImportService importer: service.importer
    PlaylistImportDraft {
        id: inputDraft
    }
    property alias linkText: inputDraft.linkText
    property alias newName: inputDraft.newName
    property alias duplicateDraft: inputDraft.duplicateName
    property bool backgroundDraft: false
    property alias mode: inputDraft.mode
    property alias targetMode: inputDraft.targetMode
    property alias targetId: inputDraft.targetId
    property alias files: inputDraft.files
    property bool inputStep: !importer.hasProgress
    heading: qsTr("导入外部歌单")
    preferredWidth: 680
    maximumHeight: 620
    windowHeightMargin: 64
    managedDismiss: true
    closePolicy: importer.busy ? Popup.NoAutoClose : Popup.CloseOnEscape
    onDismissRequested: {
        if (importer.busy && !importer.backgroundReminderMuted)
            backgroundConfirmOwner.open();
        else
            close();
    }
    function resetInput(): void {
        inputDraft.mode = 0;
        inputDraft.targetMode = 0;
        inputDraft.targetId = "";
        inputDraft.linkText = "";
        inputDraft.newName = "";
        inputDraft.duplicateName = "";
        inputDraft.files = [];
        inputDraft.fileMessage = "";
    }
    function selectScreenshots(selectedFiles: var): void {
        if (importer.busy)
            return;
        const selection = dialog.importer.prepareScreenshotSelection(selectedFiles);
        inputDraft.files = selection.files;
        inputDraft.fileMessage = selection.message;
    }
    onClosed: {
        if (!importer.busy)
            importer.dismiss();
        resetInput();
    }
    onOpened: {
        if (mode === 1 && inputStep)
            importer.refreshTargets();
        if (importer.phase === PlaylistImportService.Preparing && importer.duplicateName.length)
            duplicateDraft = importer.duplicateName;
    }
    headerActions: RowLayout {
        spacing: 6
        EchoTag {
            size: EchoTag.Small
            radius: height / 2
            borderVisible: false
            text: qsTr("1 输入")
            tone: dialog.inputStep ? EchoTag.Accent : EchoTag.Muted
            colors: Theme.floating
        }
        Rectangle {
            implicitWidth: 12
            implicitHeight: 1
            color: Theme.floating.controlBorder
        }
        EchoTag {
            size: EchoTag.Small
            radius: height / 2
            borderVisible: false
            text: qsTr("2 导入")
            tone: !dialog.inputStep ? EchoTag.Accent : EchoTag.Muted
            colors: Theme.floating
        }
    }
    EchoFileDialogLoader {
        id: pickerOwner
        hostItem: dialog.contentItem
        sourceComponent: Component {
            EchoFileDialog {
                id: picker
                title: qsTr("选择歌单截图")
                fileMode: EchoFileDialog.OpenFiles
                nameFilters: [qsTr("歌单截图 (*.jpg *.jpeg *.png)")]
                onAccepted: dialog.selectScreenshots(selectedFiles)
            }
        }
    }
    Connections {
        target: dialog.importer
        function onAccountInvalidated() {
            duplicateConfirmOwner.close();
            backgroundConfirmOwner.close();
            dialog.resetInput();
            dialog.close();
        }
    }
    body: ColumnLayout {
        spacing: 12
        PlaylistImportInput {
            importer: dialog.importer
            draft: inputDraft
            enabled: !dialog.importer.busy
            visible: dialog.inputStep
            Layout.fillWidth: true
            onPickImages: pickerOwner.open()
        }
        PlaylistImportProgress {
            importer: dialog.importer
            surface: dialog
            visible: !dialog.inputStep
            Layout.fillWidth: true
            Layout.fillHeight: true
        }
        Rectangle {
            Layout.fillWidth: true
            visible: dialog.inputStep && (inputDraft.fileMessage.length > 0 || dialog.importer.errorMessage.length > 0)
            implicitHeight: errorRow.implicitHeight + 16
            radius: Theme.cardRadius
            color: Theme.withAlpha(Theme.floating.danger, 0.12)
            RowLayout {
                id: errorRow
                width: parent.width - 24
                anchors.centerIn: parent
                spacing: 8
                EchoIcon {
                    Layout.preferredWidth: 14
                    Layout.preferredHeight: 14
                    name: "alert-triangle"
                    color: Theme.floating.dangerText
                }
                Label {
                    objectName: "playlistImportInputError"
                    Layout.fillWidth: true
                    text: inputDraft.fileMessage || dialog.importer.errorMessage
                    wrapMode: Text.Wrap
                    color: Theme.floating.dangerText
                    font: Theme.font(12, Theme.regularWeight)
                }
            }
        }
    }
    footerContent: RowLayout {
        spacing: 8
        Rectangle {
            visible: !dialog.inputStep && !dialog.importer.busy
            implicitWidth: summaryRow.implicitWidth + 24
            implicitHeight: summaryRow.implicitHeight + 16
            radius: Theme.cardRadius
            color: Theme.withAlpha(dialog.importer.imported > 0 ? "#10b981" : Theme.floating.danger, 0.1)
            RowLayout {
                id: summaryRow
                anchors.centerIn: parent
                spacing: 8
                EchoIcon {
                    objectName: "playlistImportSummaryIcon"
                    name: dialog.importer.imported > 0 ? "check" : "alert-triangle"
                    color: Theme.floatingText
                    Layout.preferredWidth: 15
                    Layout.preferredHeight: 15
                }
                Label {
                    text: qsTr("成功 %1 · 未匹配 %2 · 失败 %3").arg(dialog.importer.imported).arg(dialog.importer.missed).arg(dialog.importer.phase === PlaylistImportService.Failed ? 1 : 0)
                    font: Theme.font(12, Theme.regularWeight)
                    color: Theme.floatingText
                }
            }
        }
        Item {
            Layout.fillWidth: true
        }
        EchoButton {
            visible: dialog.inputStep
            size: EchoControlSize.Standard
            colors: Theme.floating
            accent: false
            text: qsTr("取消")
            enabled: !dialog.importer.busy
            onClicked: dialog.close()
        }
        EchoButton {
            objectName: "playlistImportStart"
            visible: dialog.inputStep
            size: EchoControlSize.Standard
            colors: Theme.floating
            text: qsTr("开始导入")
            enabled: !dialog.importer.busy && (dialog.mode === 0 ? dialog.linkText.trim().length > 0 : dialog.files.length > 0 && (dialog.targetMode === 0 ? dialog.targetId.length > 0 : dialog.newName.trim().length > 0))
            onClicked: {
                if (dialog.mode === 0)
                    dialog.importer.startLink(dialog.linkText);
                else
                    dialog.importer.startScreenshot(dialog.files, dialog.targetMode === 0 ? dialog.targetId : "", dialog.targetMode === 1 ? dialog.newName : "");
            }
        }
        EchoButton {
            visible: dialog.importer.busy
            size: EchoControlSize.Standard
            colors: Theme.floating
            accent: false
            text: dialog.importer.cloudTask ? qsTr("停止查看") : qsTr("停止导入")
            onClicked: dialog.importer.stop()
        }
        EchoButton {
            visible: dialog.importer.busy
            size: EchoControlSize.Standard
            colors: Theme.floating
            text: qsTr("后台运行")
            onClicked: dialog.close()
        }
        EchoButton {
            visible: !dialog.inputStep && !dialog.importer.busy
            size: EchoControlSize.Standard
            colors: Theme.floating
            text: qsTr("完成")
            onClicked: {
                dialog.close();
            }
        }
    }
    EchoPopupLoader {
        id: duplicateConfirmOwner
        hostItem: dialog.contentItem
        requestedVisible: dialog.opened && dialog.importer.phase === PlaylistImportService.Preparing && dialog.importer.duplicateName.length > 0
        sourceComponent: Component {
            EchoDialog {
                id: duplicateConfirm
                uiScale: dialog.uiScale
                heading: qsTr("歌单名称重复")
                description: qsTr("已有同名歌单。你可以修改名称，或保留原名称继续创建。")
                closable: false
                closePolicy: Popup.NoAutoClose
                body: EchoTextField {
                    id: duplicateName
                    text: dialog.duplicateDraft
                    onTextEdited: dialog.duplicateDraft = text
                    implicitHeight: 40
                    colors: Theme.floating
                    font: Theme.font(13, Theme.regularWeight)
                    placeholderText: qsTr("请输入新歌单名称")
                }
                footerContent: RowLayout {
                    spacing: 5
                    Item {
                        Layout.fillWidth: true
                    }
                    EchoButton {
                        size: EchoControlSize.Standard
                        colors: Theme.floating
                        accent: false
                        text: qsTr("取消")
                        onClicked: {
                            dialog.importer.confirmDuplicate(false, "");
                            duplicateConfirm.close();
                        }
                    }
                    EchoButton {
                        size: EchoControlSize.Standard
                        colors: Theme.floating
                        text: qsTr("确定")
                        enabled: dialog.duplicateDraft.trim().length > 0
                        onClicked: {
                            dialog.importer.confirmDuplicate(true, dialog.duplicateDraft);
                            duplicateConfirm.close();
                        }
                    }
                }
            }
        }
    }
    EchoPopupLoader {
        id: backgroundConfirmOwner
        hostItem: dialog.contentItem
        sourceComponent: Component {
            EchoDialog {
                id: backgroundConfirm
                onOpened: dialog.backgroundDraft = false
                uiScale: dialog.uiScale
                preferredWidth: 400
                heading: qsTr("导入将在后台继续")
                description: qsTr("关闭弹窗不会中断查询，你可以在标题栏任务中心查看进度。")
                closable: false
                closePolicy: Popup.NoAutoClose
                body: RowLayout {
                    spacing: 5
                    EchoCheckBox {
                        colors: Theme.floating
                        checked: dialog.backgroundDraft
                        Accessible.name: qsTr("以后不再提醒")
                        onToggled: dialog.backgroundDraft = checked
                    }
                    Label {
                        text: qsTr("以后不再提醒")
                        color: Theme.floatingText
                        font: Theme.font(12, Theme.regularWeight)
                    }
                }
                footerContent: RowLayout {
                    spacing: 5
                    Item {
                        Layout.fillWidth: true
                    }
                    EchoButton {
                        size: EchoControlSize.Standard
                        colors: Theme.floating
                        accent: false
                        text: qsTr("留在本页")
                        onClicked: backgroundConfirm.close()
                    }
                    EchoButton {
                        size: EchoControlSize.Standard
                        colors: Theme.floating
                        text: qsTr("我知道了")
                        onClicked: {
                            if (dialog.backgroundDraft)
                                dialog.importer.backgroundReminderMuted = true;
                            backgroundConfirm.close();
                            dialog.close();
                        }
                    }
                }
            }
        }
    }
}
