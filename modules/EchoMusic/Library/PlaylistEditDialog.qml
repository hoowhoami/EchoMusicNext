pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs as NativeDialogs
import EchoMusic.Controls
import EchoMusic.Theme
import EchoMusic.Library

EchoDialog {
    id: dialog
    objectName: "playlistEditDialog"
    required property PlaylistEditModel editor
    property double cloudId: 0
    heading: qsTr("编辑歌单")
    preferredWidth: Math.min(480, parent ? Math.max(0, parent.width / uiScale - 48) : 480)
    windowHeightMargin: 96
    closable: !editor.saving
    closePolicy: editor.saving ? Popup.NoAutoClose : Popup.CloseOnEscape | Popup.CloseOnPressOutside
    onOpened: editor.open(cloudId)
    onClosed: {
        pickerOwner.close();
        editor.close();
    }
    Connections {
        target: dialog.editor
        function onSaved(): void {
            dialog.close();
        }
        function onInvalidated(): void {
            dialog.close();
        }
    }
    EchoFileDialogLoader {
        id: pickerOwner
        hostItem: dialog.parent
        sourceComponent: Component {
            NativeDialogs.FileDialog {
                title: qsTr("选择歌单封面")
                fileMode: NativeDialogs.FileDialog.OpenFile
                nameFilters: [qsTr("图片 (*.jpg *.jpeg *.png *.webp)")]
                onAccepted: dialog.editor.selectCover(selectedFile)
            }
        }
    }
    flushBody: true
    body: Flickable {
        id: scroll
        implicitHeight: fields.implicitHeight
        contentWidth: width
        contentHeight: fields.implicitHeight
        boundsBehavior: Flickable.StopAtBounds
        flickableDirection: Flickable.VerticalFlick
        clip: true
        ScrollBar.vertical: EchoPopupScrollBar {
            surface: dialog
            view: scroll
        }
        ColumnLayout {
            id: fields
            width: Math.max(0, scroll.width - 24)
            spacing: 14
            Label {
                visible: dialog.editor.loading
                text: qsTr("正在加载歌单…")
                color: Theme.floatingSecondaryText
                font: Theme.font(13, Theme.regularWeight)
            }
            ColumnLayout {
                Layout.fillWidth: true
                visible: dialog.editor.loaded
                enabled: !dialog.editor.saving
                spacing: 14
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 14
                    EchoCover {
                        objectName: "playlistEditCover"
                        Layout.preferredWidth: 96
                        Layout.preferredHeight: 96
                        source: dialog.editor.coverUrl
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 10
                        Label {
                            Layout.fillWidth: true
                            text: qsTr("支持 JPG、PNG、WebP，最大 8 MB")
                            wrapMode: Text.Wrap
                            font: Theme.font(12, Theme.regularWeight)
                            color: Theme.floatingSecondaryText
                        }
                        Label {
                            Layout.fillWidth: true
                            text: qsTr("保存后仅可更换，无法恢复自动封面")
                            wrapMode: Text.Wrap
                            font: Theme.font(12, Theme.regularWeight)
                            color: Theme.floating.warning
                        }
                        EchoButton {
                            objectName: "playlistEditPickCover"
                            text: dialog.editor.selectingCover ? qsTr("正在读取…") : qsTr("更换封面")
                            size: EchoControlSize.Standard
                            accent: false
                            colors: Theme.floating
                            enabled: !dialog.editor.selectingCover
                            onClicked: pickerOwner.open()
                        }
                        Label {
                            Layout.fillWidth: true
                            visible: text.length > 0
                            text: dialog.editor.coverName
                            elide: Text.ElideRight
                            font: Theme.font(12, Theme.regularWeight)
                            color: Theme.floatingText
                        }
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 7
                    Label {
                        text: qsTr("名称")
                        font: Theme.font(12, Theme.regularWeight)
                        color: Theme.floatingSecondaryText
                    }
                    EchoTextField {
                        objectName: "playlistEditTitle"
                        Layout.fillWidth: true
                        colors: Theme.floating
                        text: dialog.editor.title
                        Accessible.name: qsTr("歌单名称")
                        onTextEdited: dialog.editor.title = text
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 7
                    Label {
                        text: qsTr("标签")
                        font: Theme.font(12, Theme.regularWeight)
                        color: Theme.floatingSecondaryText
                    }
                    PlaylistEditTags {
                        Layout.fillWidth: true
                        editor: dialog.editor
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 7
                    Label {
                        text: qsTr("简介")
                        font: Theme.font(12, Theme.regularWeight)
                        color: Theme.floatingSecondaryText
                    }
                    Flickable {
                        objectName: "playlistEditDescriptionScroll"
                        Layout.fillWidth: true
                        Layout.preferredHeight: 88
                        clip: true
                        boundsBehavior: Flickable.StopAtBounds
                        flickableDirection: Flickable.VerticalFlick
                        TextArea.flickable: EchoTextArea {
                            objectName: "playlistEditDescription"
                            implicitHeight: Math.max(88, contentHeight + topPadding + bottomPadding)
                            colors: Theme.floating
                            text: dialog.editor.description
                            placeholderText: qsTr("写一点关于这个歌单的介绍")
                            Accessible.name: qsTr("歌单简介")
                            onTextChanged: if (activeFocus)
                                dialog.editor.description = text
                        }
                        ScrollBar.vertical: EchoScrollBar {}
                    }
                }
            }
            Label {
                Layout.fillWidth: true
                visible: text.length > 0
                text: dialog.editor.errorMessage
                wrapMode: Text.Wrap
                color: Theme.floating.dangerText
                font: Theme.font(12, Theme.regularWeight)
            }
        }
    }
    footerContent: RowLayout {
        spacing: 8
        EchoButton {
            visible: dialog.editor.errorMessage.length > 0
            enabled: !dialog.editor.saving && !dialog.editor.loading
            text: qsTr("重新加载")
            accent: false
            size: EchoControlSize.Standard
            colors: Theme.floating
            onClicked: dialog.editor.reload()
        }
        Item {
            Layout.fillWidth: true
        }
        EchoButton {
            text: qsTr("取消")
            enabled: !dialog.editor.saving
            accent: false
            size: EchoControlSize.Standard
            colors: Theme.floating
            onClicked: dialog.close()
        }
        EchoButton {
            objectName: "playlistEditSave"
            text: dialog.editor.saving ? qsTr("正在保存…") : qsTr("保存")
            enabled: dialog.editor.dirty && !dialog.editor.loading && !dialog.editor.saving && !dialog.editor.selectingCover
            size: EchoControlSize.Standard
            colors: Theme.floating
            onClicked: dialog.editor.save()
        }
    }
}
