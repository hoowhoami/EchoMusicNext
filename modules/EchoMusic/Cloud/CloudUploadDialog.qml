pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Controls
import EchoMusic.Theme
import EchoMusic.Cloud

// Upload service and task survive dialog disposal.
// Each dialog and picker is created on demand and released after its cycle.
EchoDialog {
    id: dialog
    objectName: "cloudUploadDialog"
    required property CloudUploadService service
    property bool manualPicker: false
    property bool backgroundDraft: false
    readonly property bool manual: service.step === "manual"
    readonly property bool pick: service.step === "pick"
    heading: qsTr("上传到云盘")
    headingGlyph: "cloud-upload"
    preferredWidth: 460
    maximumHeight: 560
    windowHeightMargin: 64
    managedDismiss: true
    closePolicy: Popup.NoAutoClose
    function leave(): void {
        if (service.busy && !service.backgroundReminderMuted)
            backgroundOwner.open();
        else
            close();
    }
    onDismissRequested: leave()
    onClosed: service.dismiss()
    Connections {
        target: dialog.service
        function onAccountInvalidated(): void {
            backgroundOwner.close();
            dialog.close();
        }
    }
    EchoFileDialogLoader {
        id: fileOwner
        objectName: "cloudUploadFileOwner"
        hostItem: dialog.contentItem
        sourceComponent: Component {
            EchoFileDialog {
                title: dialog.manualPicker ? qsTr("选择匹配上传的音频") : qsTr("选择上传的音频")
                fileMode: dialog.manualPicker ? EchoFileDialog.OpenFile : EchoFileDialog.OpenFiles
                nameFilters: [qsTr("音频文件 (*.aac *.aif *.aiff *.amr *.ape *.caf *.dff *.dsf *.flac *.m4a *.mp3 *.oga *.ogg *.opus *.wav *.wave *.wma)")]
                onAccepted: dialog.service.selectFiles(selectedFiles, dialog.manualPicker)
            }
        }
    }
    EchoFolderDialogLoader {
        id: folderOwner
        objectName: "cloudUploadFolderOwner"
        hostItem: dialog.contentItem
        sourceComponent: Component {
            EchoFileDialog {
                fileMode: EchoFileDialog.OpenFolder
                title: qsTr("选择上传的音乐文件夹")
                onAccepted: dialog.service.selectFolder(selectedFolder)
            }
        }
    }
    body: ColumnLayout {
        spacing: 12
        ColumnLayout {
            visible: dialog.pick
            Layout.fillWidth: true
            spacing: 12
            Repeater {
                model: [
                    {
                        title: qsTr("上传歌曲"),
                        note: qsTr("上传时自动匹配歌曲信息，支持多选，单文件不超过 100MB"),
                        glyph: "cloud-upload"
                    },
                    {
                        title: qsTr("上传文件夹"),
                        note: qsTr("收集文件夹内所有音频文件后自动匹配上传"),
                        glyph: "folder-open"
                    },
                    {
                        title: qsTr("匹配上传"),
                        note: qsTr("上传时手动匹配歌曲信息，单文件不超过 100MB"),
                        glyph: "search"
                    }
                ]
                delegate: ItemDelegate {
                    id: option
                    required property var modelData
                    required property int index
                    objectName: "cloudUploadOption" + index
                    Layout.fillWidth: true
                    implicitHeight: Math.max(68, contentItem.implicitHeight + 28)
                    padding: 14
                    hoverEnabled: true
                    Accessible.name: modelData.title
                    background: Rectangle {
                        radius: Theme.cardRadius
                        color: option.hovered || option.visualFocus ? Theme.mix(Theme.floating.mutedSurface, Theme.accent, 0.05) : Theme.floating.mutedSurface
                        border.width: 1
                        border.color: option.visualFocus ? Theme.accent : option.hovered ? Theme.mix(Theme.floating.controlBorder, Theme.accent, 0.4) : Theme.floating.controlBorder
                    }
                    contentItem: RowLayout {
                        spacing: 12
                        Rectangle {
                            implicitWidth: 40
                            implicitHeight: 40
                            radius: Theme.controlRadius
                            color: Theme.withAlpha(Theme.accent, .12)
                            EchoIcon {
                                anchors.centerIn: parent
                                width: 20
                                height: 20
                                name: option.modelData.glyph
                                color: Theme.floating.accentText
                            }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 4
                            Text {
                                Layout.fillWidth: true
                                text: option.modelData.title
                                font: Theme.font(13, Theme.semiboldWeight)
                                color: Theme.floating.text
                            }
                            Text {
                                Layout.fillWidth: true
                                text: option.modelData.note
                                font: Theme.font(11, Theme.regularWeight)
                                color: Theme.floating.secondaryText
                                wrapMode: Text.Wrap
                            }
                        }
                    }
                    onClicked: {
                        if (index === 1)
                            folderOwner.open();
                        else {
                            dialog.manualPicker = index === 2;
                            fileOwner.open();
                        }
                    }
                }
            }
        }
        Loader {
            Layout.fillWidth: true
            active: dialog.manual
            visible: active
            sourceComponent: ColumnLayout {
                id: manualForm
                objectName: "cloudUploadManualForm"
                property string querySignature: ""
                function search(): void {
                    querySignature = titleInput.text + "\n" + artistInput.text;
                    dialog.service.searchManual(titleInput.text, artistInput.text);
                }
                readonly property bool showResults: querySignature.length > 0 && querySignature === titleInput.text + "\n" + artistInput.text && !dialog.service.searching
                spacing: 10
                Text {
                    Layout.fillWidth: true
                    text: (dialog.service.manualFile.name ?? "") + "（" + ((dialog.service.manualFile.size ?? 0) / 1048576).toFixed(1) + " MB）"
                    color: Theme.floating.secondaryText
                    font: Theme.font(12, Theme.regularWeight)
                    elide: Text.ElideMiddle
                }
                EchoTextField {
                    id: titleInput
                    objectName: "cloudUploadManualTitle"
                    Layout.fillWidth: true
                    text: dialog.service.manualFile.title ?? ""
                    placeholderText: qsTr("歌名（必填）")
                    enabled: !dialog.service.searching && !dialog.service.paused
                    onTextEdited: manualForm.querySignature = ""
                    onAccepted: manualForm.search()
                }
                EchoTextField {
                    id: artistInput
                    Layout.fillWidth: true
                    text: dialog.service.manualFile.artist ?? ""
                    placeholderText: qsTr("歌手")
                    enabled: titleInput.enabled
                    onTextEdited: manualForm.querySignature = ""
                    onAccepted: manualForm.search()
                }
                EchoTextField {
                    Layout.fillWidth: true
                    text: dialog.service.manualFile.album ?? ""
                    placeholderText: qsTr("专辑（可选）")
                    enabled: titleInput.enabled
                    onAccepted: manualForm.search()
                }
                EchoButton {
                    Layout.alignment: Qt.AlignRight
                    objectName: "cloudUploadSearch"
                    text: qsTr("搜索")
                    glyph: "search"
                    colors: Theme.floating
                    loading: dialog.service.searching && !dialog.service.paused
                    enabled: titleInput.text.trim().length > 0 && !dialog.service.searching && !dialog.service.paused
                    onClicked: manualForm.search()
                }
                Text {
                    visible: manualForm.showResults
                    text: dialog.service.candidates.length > 0 ? qsTr("搜索结果（点击选择）") : qsTr("没有找到匹配歌曲")
                    font: Theme.font(12, Theme.semiboldWeight)
                    color: Theme.floating.text
                }
                ListView {
                    id: results
                    objectName: "cloudUploadResults"
                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.min(180, Math.max(100, dialog.bodyHeightLimit - 220))
                    visible: manualForm.showResults && count > 0
                    clip: true
                    model: dialog.service.candidates
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: EchoPopupScrollBar {
                        surface: dialog
                        view: results
                    }
                    delegate: ItemDelegate {
                        id: candidate
                        required property var modelData
                        required property int index
                        width: ListView.view.width
                        implicitHeight: 52
                        enabled: !dialog.service.paused
                        background: Rectangle {
                            radius: Theme.controlRadius
                            color: candidate.hovered ? Theme.floating.hover : "transparent"
                        }
                        contentItem: RowLayout {
                            spacing: 8
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 4
                                Text {
                                    Layout.fillWidth: true
                                    text: candidate.modelData.title
                                    elide: Text.ElideRight
                                    color: Theme.floating.text
                                    font: Theme.font(13, Theme.mediumWeight)
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: [candidate.modelData.artist, candidate.modelData.album_title].filter(Boolean).join(" · ")
                                    elide: Text.ElideRight
                                    color: Theme.floating.secondaryText
                                    font: Theme.font(11, Theme.regularWeight)
                                }
                            }
                            Text {
                                readonly property real duration: candidate.modelData.duration_ms ?? 0
                                text: duration > 0 ? Math.floor(duration / 60000) + ":" + ("0" + Math.floor(duration / 1000) % 60).slice(-2) : "--:--"
                                font: Theme.font(11, Theme.regularWeight)
                                color: Theme.floating.secondaryText
                            }
                        }
                        onClicked: dialog.service.uploadMatch(index)
                    }
                }
            }
        }
        ColumnLayout {
            visible: !dialog.pick && !dialog.manual
            Layout.fillWidth: true
            spacing: 8
            Text {
                Layout.fillWidth: true
                text: dialog.service.statusLabel
                font: Theme.font(12, Theme.semiboldWeight)
                color: Theme.floating.text
                wrapMode: Text.Wrap
            }
            ListView {
                id: files
                objectName: "cloudUploadFiles"
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(320, Math.max(100, dialog.bodyHeightLimit - 64 - (errorText.visible ? errorText.implicitHeight : 0)))
                clip: true
                model: dialog.service
                boundsBehavior: Flickable.StopAtBounds
                reuseItems: true
                EchoViewPoolGuard {
                    targetView: files
                }
                ScrollBar.vertical: EchoPopupScrollBar {
                    surface: dialog
                    view: files
                }
                delegate: Rectangle {
                    id: file
                    required property string trackTitle
                    required property string artist
                    required property string fileName
                    required property string uploadState
                    required property string uploadError
                    required property bool instantUpload
                    required property bool matched
                    required property real percent
                    width: ListView.view.width
                    height: 56
                    radius: Theme.controlRadius
                    color: "transparent"
                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 8
                        spacing: 10
                        EchoSpinner {
                            visible: file.uploadState === "matching" || file.uploadState === "uploading"
                            resourceActive: dialog.visible && !dialog.service.paused && dialog.contentItem.Window.window && dialog.contentItem.Window.window.visible && dialog.contentItem.Window.window.visibility !== Window.Minimized
                            Layout.preferredWidth: 14
                            Layout.preferredHeight: 14
                            color: Theme.floating.accentText
                        }
                        EchoIcon {
                            visible: file.uploadState === "success" || file.uploadState === "failed"
                            Layout.preferredWidth: 14
                            Layout.preferredHeight: 14
                            name: file.uploadState === "success" ? "check" : file.uploadState === "failed" ? "x" : "music"
                            color: file.uploadState === "failed" ? Theme.floating.dangerText : Theme.floating.accentText
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 4
                            Text {
                                Layout.fillWidth: true
                                text: file.trackTitle
                                elide: Text.ElideRight
                                font: Theme.font(13, Theme.mediumWeight)
                                color: Theme.floating.text
                            }
                            Text {
                                Layout.fillWidth: true
                                text: file.uploadError || [file.artist, file.fileName].filter(Boolean).join(" · ")
                                elide: Text.ElideRight
                                font: Theme.font(11, Theme.regularWeight)
                                color: file.uploadError ? Theme.floating.dangerText : Theme.floating.secondaryText
                            }
                        }
                        Text {
                            text: file.uploadState === "success" ? (file.instantUpload ? qsTr("秒传成功") : qsTr("成功")) + " · " + (file.matched ? qsTr("已关联曲库") : qsTr("云端自动匹配")) : file.uploadState === "failed" ? qsTr("失败") : file.uploadState === "matching" ? qsTr("匹配中") : file.uploadState === "uploading" ? qsTr("上传中") : file.uploadState === "cancelled" ? qsTr("已取消") : qsTr("等待中")
                            font: Theme.font(11, Theme.regularWeight)
                            color: Theme.floating.secondaryText
                        }
                    }
                }
            }
        }
        Text {
            id: errorText
            Layout.fillWidth: true
            visible: text.length > 0
            text: dialog.service.errorMessage
            font: Theme.font(12, Theme.regularWeight)
            color: Theme.floating.dangerText
            wrapMode: Text.Wrap
        }
    }
    footerContent: RowLayout {
        spacing: 8
        RowLayout {
            visible: !dialog.pick && !dialog.manual
            Layout.fillWidth: true
            spacing: 8
            Rectangle {
                Layout.fillWidth: true
                Layout.maximumWidth: 280
                Layout.minimumWidth: 16
                implicitHeight: 6
                radius: 3
                color: Theme.floating.controlBorder
                Rectangle {
                    height: parent.height
                    width: parent.width * dialog.service.progress
                    radius: parent.radius
                    color: Theme.accent
                }
            }
            Text {
                text: dialog.service.completed + " / " + dialog.service.total
                font: Theme.font(11, Theme.semiboldWeight)
                color: Theme.floating.secondaryText
            }
        }
        Item {
            visible: dialog.pick || dialog.manual
            Layout.fillWidth: true
        }
        EchoSoftButton {
            visible: dialog.manual && !dialog.service.paused
            text: qsTr("返回")
            colors: Theme.floating
            enabled: !dialog.service.searching
            onClicked: dialog.service.reset()
        }
        EchoSoftButton {
            visible: dialog.pick || dialog.manual && !dialog.service.paused
            text: qsTr("取消")
            colors: Theme.floating
            onClicked: dialog.close()
        }
        EchoSoftButton {
            visible: dialog.service.busy
            text: qsTr("取消上传")
            colors: Theme.floating
            onClicked: dialog.service.stop()
        }
        EchoButton {
            visible: dialog.service.paused
            text: qsTr("继续")
            colors: Theme.floating
            enabled: dialog.service.canResume
            onClicked: dialog.service.resume()
        }
        EchoButton {
            visible: dialog.service.busy && !dialog.service.paused
            text: qsTr("后台运行")
            colors: Theme.floating
            onClicked: dialog.leave()
        }
        EchoButton {
            visible: !dialog.pick && !dialog.manual && !dialog.service.busy
            text: qsTr("完成")
            colors: Theme.floating
            onClicked: dialog.close()
        }
    }
    EchoPopupLoader {
        id: backgroundOwner
        objectName: "cloudUploadBackgroundOwner"
        hostItem: dialog.contentItem
        sourceComponent: Component {
            EchoDialog {
                heading: qsTr("上传将在后台继续")
                headingGlyph: "cloud-upload"
                descriptionPixelSize: 13
                description: qsTr("关闭此弹窗不会中断上传，你可以在标题栏「当前任务」面板中查看进度。")
                preferredWidth: 400
                windowHeightMargin: 48
                body: RowLayout {
                    spacing: 8
                    EchoCheckBox {
                        objectName: "cloudUploadReminderCheck"
                        checked: dialog.backgroundDraft
                        colors: Theme.floating
                        onToggled: dialog.backgroundDraft = checked
                        Accessible.name: qsTr("以后不再提醒")
                    }
                    Item {
                        objectName: "cloudUploadReminderLabel"
                        Layout.fillWidth: true
                        implicitHeight: 18
                        EchoTextTagRow {
                            anchors.fill: parent
                            text: qsTr("以后不再提醒")
                            font: Theme.font(12, Theme.regularWeight)
                            color: Theme.floating.secondaryText
                            lineHeight: 18
                        }
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: dialog.backgroundDraft = !dialog.backgroundDraft
                        }
                    }
                }
                footerContent: RowLayout {
                    Item {
                        Layout.fillWidth: true
                    }
                    EchoSoftButton {
                        text: qsTr("留在本页")
                        colors: Theme.floating
                        onClicked: backgroundOwner.close()
                    }
                    EchoButton {
                        text: qsTr("我知道了")
                        colors: Theme.floating
                        onClicked: {
                            dialog.service.backgroundReminderMuted = dialog.backgroundDraft;
                            backgroundOwner.close();
                            dialog.close();
                        }
                    }
                }
            }
        }
    }
}
