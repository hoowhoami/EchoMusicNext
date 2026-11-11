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
    required property PlaylistImportDraft draft
    signal pickImages
    spacing: 16
    RowLayout {
        Layout.fillHeight: false
        Layout.fillWidth: true
        spacing: 12
        Repeater {
            model: [qsTr("链接导入"), qsTr("截图导入")]
            delegate: EchoActionButton {
                id: choice
                required property int index
                required property string modelData
                Layout.fillWidth: true
                implicitHeight: 64
                padding: 16
                leftPadding: 16
                rightPadding: 16
                onClicked: {
                    pane.draft.mode = index;
                    if (index === 1)
                        pane.importer.refreshTargets();
                }
                background: Rectangle {
                    radius: Theme.cardRadius
                    color: choice.hovered || pane.draft.mode === choice.index ? Theme.withAlpha(Theme.floating.accent, 0.09) : Theme.floating.mutedSurface
                    border.color: pane.draft.mode === choice.index ? Theme.mix(Theme.floating.controlBorder, Theme.floating.accent, 0.5) : Theme.floating.controlBorder
                }
                contentItem: RowLayout {
                    spacing: 12
                    EchoIcon {
                        name: choice.index === 0 ? "external-link" : "playlist-add"
                        Layout.preferredWidth: 19
                        Layout.preferredHeight: 19
                        color: pane.draft.mode === choice.index ? Theme.floating.accentText : Theme.floatingSecondaryText
                    }
                    ColumnLayout {
                        Layout.fillHeight: false
                        Layout.fillWidth: true
                        spacing: 2
                        Label {
                            text: choice.modelData
                            font: Theme.font(13, Theme.boldWeight)
                            color: Theme.floatingText
                        }
                        Label {
                            Layout.fillWidth: true
                            text: choice.index === 0 ? qsTr("粘贴歌单链接，自动完成导入") : qsTr("上传截图，自动识别其中的歌曲")
                            elide: Text.ElideRight
                            font: Theme.font(11, Theme.regularWeight)
                            color: Theme.floatingSecondaryText
                        }
                    }
                }
            }
        }
    }
    ColumnLayout {
        visible: pane.draft.mode === 0
        Layout.fillWidth: true
        spacing: 12
        EchoTextArea {
            id: linkInput
            objectName: "playlistImportLink"
            text: pane.draft.linkText
            onTextChanged: pane.draft.linkText = text
            Layout.fillWidth: true
            implicitHeight: 112
            padding: 16
            leftPadding: 16
            rightPadding: 16
            topPadding: 12
            placeholderText: qsTr("粘贴外部平台的歌单链接")
            color: Theme.floatingText
            placeholderTextColor: Theme.floatingSecondaryText
            font: Theme.font(13, Theme.mediumWeight)
            wrapMode: TextEdit.Wrap
            selectByMouse: true
            colors: Theme.floating
            background: EchoInputBackground {
                colors: linkInput.colors
                focused: linkInput.activeFocus
                radius: Theme.cardRadius
            }
        }
    }
    ColumnLayout {
        visible: pane.draft.mode === 1
        Layout.fillWidth: true
        spacing: 12
        EchoActionButton {
            id: dropzone
            objectName: "playlistImportImages"
            Layout.fillWidth: true
            implicitHeight: 126
            onClicked: pane.pickImages()
            background: Rectangle {
                radius: Theme.cardRadius
                color: dropzone.hovered ? Theme.withAlpha(Theme.floating.accent, 0.07) : Theme.floating.mutedSurface
            }
            contentItem: ColumnLayout {
                spacing: 6
                EchoIcon {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredWidth: 24
                    Layout.preferredHeight: 24
                    name: "playlist-add"
                    color: Theme.floatingSecondaryText
                }
                Label {
                    Layout.alignment: Qt.AlignHCenter
                    text: pane.draft.files.length ? qsTr("已选择 %1 张截图").arg(pane.draft.files.length) : qsTr("选择歌单截图")
                    font: Theme.font(13, Theme.boldWeight)
                    color: Theme.floatingText
                }
                Label {
                    Layout.alignment: Qt.AlignHCenter
                    text: qsTr("JPEG / PNG，最多 9 张，单张不超过 10 MB")
                    font: Theme.font(11, Theme.regularWeight)
                    color: Theme.floatingSecondaryText
                }
            }
        }
        Flow {

            Layout.fillWidth: true
            visible: pane.draft.files.length > 0
            spacing: 6
            Repeater {
                model: pane.draft.files
                delegate: EchoTag {
                    required property url modelData
                    text: decodeURIComponent(String(modelData).split('/').pop())
                    tone: EchoTag.Muted
                    colors: Theme.floating
                    width: Math.min(200, implicitWidth)
                }
            }
        }
        EchoTabBar {
            Layout.fillWidth: true
            labels: [qsTr("选择歌单"), qsTr("新建歌单")]
            colors: Theme.floating
            currentIndex: pane.draft.targetMode
            onPicked: index => pane.draft.targetMode = index
            accessibleName: qsTr("截图导入目标")
        }
        EchoSelect {
            Layout.fillWidth: true
            visible: pane.draft.targetMode === 0
            colors: Theme.floating
            options: pane.importer.targets
            value: pane.draft.targetId
            filterable: pane.importer.targets.length > 8
            enabled: !pane.importer.loadingTargets
            placeholder: pane.importer.loadingTargets ? qsTr("正在加载歌单…") : qsTr("选择要写入的歌单")
            onValueEdited: nextValue => pane.draft.targetId = String(nextValue)
        }
        EchoTextField {
            id: newTitle
            text: pane.draft.newName
            onTextEdited: pane.draft.newName = text
            Layout.fillWidth: true
            visible: pane.draft.targetMode === 1
            implicitHeight: 40
            colors: Theme.floating
            font: Theme.font(13, Theme.regularWeight)
            placeholderText: qsTr("请输入新歌单名称")
        }
        Label {
            Layout.fillWidth: true
            text: qsTr("识别结果会添加到所选歌单，或写入新建歌单。")
            wrapMode: Text.Wrap
            color: Theme.floatingSecondaryText
            font: Theme.font(12, Theme.regularWeight)
        }
    }
}
