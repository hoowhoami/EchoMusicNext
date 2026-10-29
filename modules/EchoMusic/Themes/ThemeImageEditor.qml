pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Theme
import EchoMusic.Controls

ColumnLayout {
    id: editor
    required property real uiScale
    required property size viewport
    required property bool pageActive
    readonly property bool resourceActive: pageActive && visible
    onResourceActiveChanged: {
        if (!resourceActive) {
            fileDialogOwner.close();
            textPickerOwner.close();
            accentPickerOwner.close();
        }
    }
    readonly property EchoColorPickerDialog textColorDialog: textPickerOwner.item
    readonly property bool hasImage: Theme.store.backgroundImage.toString() !== ""
    spacing: 16
    RowLayout {
        Layout.fillWidth: true
        Layout.fillHeight: true
        spacing: 24
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: Theme.mediaRadius
            color: Theme.mutedSurface
            DropArea {
                anchors.fill: parent
                onDropped: drop => {
                    if (editor.resourceActive && drop.hasUrls && drop.urls.length)
                        Theme.store.importBackground(drop.urls[0]);
                }
            }
            RowLayout {
                anchors.fill: parent
                anchors.margins: editor.hasImage ? 12 : 24
                spacing: 16
                ThemeImageCropper {
                    id: cropper
                    visible: editor.hasImage
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    viewport: editor.viewport
                }
                ColumnLayout {
                    visible: editor.hasImage
                    Layout.fillWidth: false
                    Layout.fillHeight: false
                    Layout.alignment: Qt.AlignTop
                    Layout.topMargin: 16
                    spacing: 10
                    EchoButton {
                        Layout.fillWidth: true
                        accent: false
                        size: EchoControlSize.Standard
                        objectName: "selectWholeBackgroundAction"
                        text: qsTr("选取整张图片")
                        enabled: !Theme.store.imageImporting
                        onClicked: cropper.selectWholeImage()
                    }
                    EchoButton {
                        Layout.fillWidth: true
                        accent: false
                        size: EchoControlSize.Standard
                        objectName: "reselectBackgroundAction"
                        text: Theme.store.imageImporting ? qsTr("正在导入…") : qsTr("重新选图")
                        enabled: !Theme.store.imageImporting
                        onClicked: fileDialogOwner.open()
                    }
                }
                ColumnLayout {
                    visible: !editor.hasImage
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignCenter
                    spacing: 12
                    EchoIcon {
                        name: "photo"
                        Layout.preferredWidth: 32
                        Layout.preferredHeight: 32
                        Layout.alignment: Qt.AlignHCenter
                        color: Theme.secondaryText
                    }
                    Label {
                        text: Theme.store.imageImporting ? qsTr("正在加载图片…") : qsTr("选择背景图片")
                        font: Theme.font(16, Theme.semiboldWeight)
                        color: Theme.text
                        Layout.alignment: Qt.AlignHCenter
                    }
                    EchoButton {
                        objectName: "importBackgroundAction"
                        accent: false
                        size: EchoControlSize.Standard
                        text: qsTr("选择本地图片")
                        enabled: !Theme.store.imageImporting
                        Layout.alignment: Qt.AlignHCenter
                        onClicked: fileDialogOwner.open()
                    }
                    Label {
                        text: qsTr("或拖入 JPG、PNG、WebP 图片，不超过 20 MB")
                        color: Theme.secondaryText
                        font: Theme.font(12, Theme.regularWeight)
                        wrapMode: Text.Wrap
                        horizontalAlignment: Text.AlignHCenter
                        Layout.fillWidth: true
                    }
                }
            }
        }
        ScrollView {
            id: options
            Layout.preferredWidth: 200
            Layout.fillHeight: true
            clip: true
            contentWidth: availableWidth
            contentHeight: optionsBody.implicitHeight + 28
            ScrollBar.vertical: EchoPageScrollBar {
                view: options.contentItem as Flickable
                viewActive: editor.resourceActive
            }
            ColumnLayout {
                id: optionsBody
                y: 4
                width: options.availableWidth
                spacing: 20
                enabled: editor.hasImage && !Theme.store.imageImporting
                ColumnLayout {
                    spacing: 4
                    Label {
                        text: qsTr("文字颜色")
                        color: Theme.text
                        font: Theme.font(13, Theme.regularWeight)
                    }
                    EchoButton {
                        accent: false
                        font: Theme.font(13, Theme.regularWeight)
                        background: null
                        objectName: "backgroundTextAction"
                        text: Theme.store.backgroundText.toString().toUpperCase()
                        foreground: Theme.text
                        implicitHeight: 28
                        leftPadding: 32
                        onClicked: textPickerOwner.open()
                        Rectangle {
                            x: 0
                            anchors.verticalCenter: parent.verticalCenter
                            width: 24
                            height: 24
                            radius: Theme.detailRadius
                            color: Theme.store.backgroundText
                            border.color: Theme.subtleBorder
                            border.width: 1
                        }
                    }
                }
                ColumnLayout {
                    spacing: 4
                    Label {
                        text: qsTr("强调色")
                        color: Theme.text
                        font: Theme.font(13, Theme.regularWeight)
                    }
                    EchoButton {
                        accent: false
                        font: Theme.font(13, Theme.regularWeight)
                        background: null
                        objectName: "backgroundAccentAction"
                        text: Theme.store.backgroundAccent.toString().toUpperCase()
                        foreground: Theme.text
                        implicitHeight: 28
                        leftPadding: 32
                        onClicked: accentPickerOwner.open()
                        Rectangle {
                            x: 0
                            anchors.verticalCenter: parent.verticalCenter
                            width: 24
                            height: 24
                            radius: Theme.detailRadius
                            color: Theme.store.backgroundAccent
                            border.color: Theme.subtleBorder
                            border.width: 1
                        }
                    }
                    Label {
                        Layout.fillWidth: true
                        text: Theme.store.accentSource === "cover" ? qsTr("歌曲封面取色优先，切换为跟随主题后使用此颜色。") : Theme.store.accentSource === "custom" ? qsTr("当前使用全局自选颜色，切换为跟随主题后生效。") : qsTr("用于按钮、进度条与选中状态。")
                        font: Theme.font(12, Theme.regularWeight)
                        color: Theme.secondaryText
                        wrapMode: Text.Wrap
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4
                    RowLayout {
                        Layout.fillHeight: false
                        spacing: 5
                        Layout.fillWidth: true
                        Label {
                            text: qsTr("背景遮罩")
                            color: Theme.text
                            font: Theme.font(13, Theme.regularWeight)
                            Layout.fillWidth: true
                        }
                        Label {
                            text: Theme.store.backgroundShade + "%"
                            color: Theme.secondaryText
                            font: Theme.font(12, Theme.regularWeight)
                        }
                    }
                    EchoSlider {
                        objectName: "backgroundShadeControl"
                        Layout.fillWidth: true
                        from: 0
                        to: 85
                        stepSize: 1
                        value: Theme.store.backgroundShade
                        Accessible.name: qsTr("背景遮罩")
                        onMoved: Theme.store.setBackgroundShade(value)
                    }
                }
                Label {
                    text: qsTr("裁剪后的区域将拉伸铺满页面。")
                    font: Theme.font(12, Theme.regularWeight)
                    color: Theme.secondaryText
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }
            }
        }
    }
    Label {
        Layout.fillWidth: true
        visible: text !== ""
        text: Theme.store.imageError
        color: "#ef4444"
        font: Theme.font(12, Theme.regularWeight)
        wrapMode: Text.Wrap
        Accessible.role: Accessible.AlertMessage
    }
    ColumnLayout {
        Layout.fillWidth: true
        spacing: 12
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 1
            color: Theme.subtleBorder
        }
        RowLayout {
            Layout.fillHeight: false
            Layout.fillWidth: true
            spacing: 10
            Item {
                Layout.fillWidth: true
            }
            EchoButton {
                accent: false
                objectName: "resetBackgroundAction"
                text: qsTr("恢复默认")
                size: EchoControlSize.Standard
                Layout.minimumWidth: 76
                enabled: editor.hasImage && !Theme.store.imageImporting
                onClicked: Theme.store.resetBackgroundAdjustments()
            }
            EchoButton {
                objectName: "revertBackgroundAction"
                accent: false
                text: qsTr("还原")
                size: EchoControlSize.Standard
                Layout.minimumWidth: 76
                enabled: Theme.store.backgroundPreview
                onClicked: Theme.store.cancelBackground(true)
            }
            EchoButton {
                objectName: "applyBackgroundAction"
                text: qsTr("应用")
                size: EchoControlSize.Standard
                Layout.minimumWidth: 76
                enabled: editor.hasImage && !Theme.store.imageImporting && (Theme.store.backgroundPreview || Theme.store.themeKey !== "custom")
                onClicked: {
                    cropper.commitSelection();
                    Theme.store.applyBackground();
                }
            }
        }
    }
    EchoFileDialogLoader {
        id: fileDialogOwner
        hostItem: editor
        sourceComponent: Component {
            EchoFileDialog {
                id: fileDialog
                title: qsTr("选择背景图片")
                nameFilters: [qsTr("背景图片 (*.jpg *.jpeg *.png *.webp)")]
                onAccepted: {
                    if (editor.resourceActive)
                        Theme.store.importBackground(selectedFile);
                }
            }
        }
    }
    EchoPopupLoader {
        id: textPickerOwner
        hostItem: editor
        sourceComponent: Component {
            EchoColorPickerDialog {
                id: textPicker
                title: qsTr("文字颜色")
                uiScale: editor.uiScale
                value: Theme.store.backgroundText.toString()
                presets: ["#ffffff", "#000000", "#eeeeee", "#cccccc", "#999999", "#555555", "#ffe3a3", "#ffb8c7", "#d2baff", "#acd8ff", "#a9e3db", "#c3e7af"]
                onConfirmed: color => {
                    if (editor.resourceActive)
                        Theme.store.setBackgroundText(color);
                }
            }
        }
    }
    EchoPopupLoader {
        id: accentPickerOwner
        hostItem: editor
        sourceComponent: Component {
            EchoColorPickerDialog {
                id: accentPicker
                title: qsTr("背景强调色")
                uiScale: editor.uiScale
                value: Theme.store.backgroundAccent.toString()
                presets: ["#00cc65", "#ffdd00", "#ff9f0a", "#ff6b81", "#c4a0ff", "#6ac8ff", "#ffffff", "#000000"]
                onConfirmed: color => {
                    if (editor.resourceActive)
                        Theme.store.setBackgroundAccent(color);
                }
            }
        }
    }
}
