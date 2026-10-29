pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Shapes
import EchoMusic.Controls
import EchoMusic.Theme
import EchoMusic.Playback

EchoDialog {
    id: manager
    required property PlaybackAudioControls audio
    property int currentTab: 0
    property string editingPath: ""
    property string draft: ""
    readonly property list<string> sources: ["local", "artist", "headphone", "market"]
    readonly property list<string> labels: [qsTr("本地导入"), qsTr("歌手音效"), qsTr("耳机音效"), qsTr("音效市场")]
    readonly property var files: audio.impulseResponses.filter(file => file.source === sources[currentTab])
    objectName: "audioEffectFilesDialog"
    heading: qsTr("音效文件")
    preferredWidth: 520
    flushBody: true
    function cancelRename(): void {
        editingPath = "";
        draft = "";
    }
    function saveName(): void {
        if (draft.trim().length)
            audio.renameImpulseResponse(editingPath, draft.trim());
        cancelRename();
    }
    onCurrentTabChanged: cancelRename()
    onAboutToHide: {
        cancelRename();
        filePickerOwner.close();
    }
    Connections {
        target: manager.audio
        function onImpulseFilesImported() {
            manager.currentTab = 0;
        }
    }
    body: ColumnLayout {
        spacing: 14
        Item {
            objectName: "settingsEffectFileTabs"
            Layout.fillWidth: true
            Layout.rightMargin: 24
            implicitHeight: 37
            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: Theme.floating.controlBorder
            }
            RowLayout {
                width: parent.width
                spacing: 4
                Repeater {
                    model: manager.labels
                    delegate: Button {
                        id: tab
                        required property string modelData
                        required property int index
                        objectName: "settingsEffectFileTab-" + index
                        Layout.preferredWidth: 0
                        Layout.fillWidth: true
                        Layout.preferredHeight: 36
                        leftPadding: 2
                        rightPadding: 2
                        topPadding: 0
                        bottomPadding: 0
                        hoverEnabled: true
                        Accessible.name: modelData
                        onClicked: manager.currentTab = index
                        contentItem: Item {
                            EchoTextTagRow {
                                anchors.centerIn: parent
                                text: tab.modelData
                                spacing: 5
                                lineHeight: 20
                                font: Theme.font(11, 650)
                                color: manager.currentTab === tab.index || tab.hovered ? Theme.floating.accentText : Theme.floating.secondaryText
                                EchoBadge {
                                    count: manager.audio.impulseResponses.filter(file => file.source === manager.sources[tab.index]).length
                                    colors: Theme.floating
                                    tone: manager.currentTab === tab.index || tab.hovered ? EchoBadge.Accent : EchoBadge.Muted
                                }
                            }
                        }
                        background: Item {
                            Rectangle {
                                anchors.bottom: parent.bottom
                                width: parent.width
                                height: 2
                                color: manager.currentTab === tab.index ? Theme.floating.accent : "transparent"
                            }
                            Rectangle {
                                anchors.fill: parent
                                visible: tab.visualFocus
                                radius: Theme.itemRadius
                                color: "transparent"
                                border.width: 2
                                border.color: Theme.floating.accent
                            }
                        }
                    }
                }
            }
        }
        ListView {
            id: list
            objectName: "settingsEffectFileList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            implicitHeight: count > 0 ? count * 52 + Math.max(0, count - 1) * spacing : emptyState.implicitHeight
            clip: true
            reuseItems: true
            EchoViewPoolGuard {
                targetView: list
            }
            cacheBuffer: 120
            spacing: 8
            model: manager.visible ? manager.files : null
            delegate: AudioEffectFileRow {
                required property var modelData
                file: modelData
                width: list.width - 28
                editing: manager.editingPath === file.path
                draft: editing ? manager.draft : ""
                onRenameRequested: {
                    manager.editingPath = file.path;
                    manager.draft = file.name;
                }
                onDraftEdited: text => manager.draft = text
                onSaveRequested: manager.saveName()
                onCancelRequested: manager.cancelRename()
                onRemoveRequested: {
                    if (editing)
                        manager.cancelRename();
                    manager.audio.removeImpulseResponse(file.path);
                }
            }
            Label {
                id: emptyState
                objectName: "settingsEffectFilesEmpty"
                width: list.width - 28
                visible: list.count === 0
                padding: 24
                text: manager.currentTab === 0 ? qsTr("暂无本地导入音效") : qsTr("此分类暂无已下载音效")
                color: Theme.floating.secondaryText
                font: Theme.font(12, Theme.regularWeight)
                horizontalAlignment: Text.AlignHCenter
                background: Shape {
                    ShapePath {
                        strokeWidth: 1
                        strokeStyle: ShapePath.DashLine
                        dashPattern: [4, 4]
                        strokeColor: Theme.withAlpha(Theme.floating.text, .16)
                        fillColor: "transparent"
                        PathRectangle {
                            x: .5
                            y: .5
                            width: Math.max(0, emptyState.width - 1)
                            height: Math.max(0, emptyState.height - 1)
                            radius: Theme.controlRadius
                        }
                    }
                }
            }
            ScrollBar.vertical: EchoPopupScrollBar {
                surface: manager
                view: list
                resourceActive: manager.visible
            }
        }
    }
    footerContent: Item {
        implicitHeight: manager.currentTab === 0 ? importButton.implicitHeight : hint.implicitHeight
        EchoButton {
            id: importButton
            objectName: "importLocalEffectsAction"
            anchors.right: parent.right
            visible: manager.currentTab === 0
            colors: Theme.floating
            size: EchoControlSize.Standard
            text: qsTr("导入本地文件")
            loading: manager.audio.importingImpulseResponses
            enabled: !manager.audio.importingImpulseResponses
            onClicked: filePickerOwner.open()
        }
        Label {
            id: hint
            objectName: "settingsEffectDownloadHint"
            width: parent.width
            visible: manager.currentTab !== 0
            text: qsTr("请在播放器的「音效广场」中下载对应分类的音效")
            font: Theme.font(11, Theme.regularWeight)
            lineHeightMode: Text.FixedHeight
            lineHeight: 16.5
            color: Theme.floating.secondaryText
            horizontalAlignment: Text.AlignRight
            wrapMode: Text.Wrap
        }
    }
    EchoFileDialogLoader {
        id: filePickerOwner
        objectName: "localEffectFilePickerOwner"
        hostItem: manager.contentItem
        sourceComponent: Component {
            EchoFileDialog {
                id: filePicker
                objectName: "localEffectFilePicker"
                title: qsTr("导入音效文件")
                fileMode: EchoFileDialog.OpenFiles
                parentWindow: manager.parent ? manager.parent.Window.window : null
                nameFilters: [qsTr("音效文件 (*.irs *.wav *.wave *.flac *.aif *.aiff *.caf *.ogg *.oga *.mp3 *.m4a *.aac *.opus)"), qsTr("所有文件 (*)")]
                onAccepted: manager.audio.importImpulseResponseFiles(selectedFiles)
                onVisibleChanged: if (visible)
                    EchoPopupCoordinator.popupOpening(null, manager.parent)
            }
        }
    }
}
