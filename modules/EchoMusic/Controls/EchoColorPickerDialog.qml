pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Shapes
import EchoMusic.Theme

EchoDialog {
    id: picker
    property string value: "#31cfa1"
    property list<string> presets: []
    // A presentation draft only: the caller owns the persisted theme command.
    property string draft: "#31cfa1"
    readonly property string normalized: dynamicDraft ? EchoColorValue.normalize(dynamicColor.toString()) : EchoColorValue.normalize(draft)
    readonly property alias spectrumPopup: spectrumSurfaceOwner.item
    property string dynamicLabel: ""
    property color dynamicColor: "white"
    property bool dynamicSelected: false
    property bool dynamicDraft: false
    signal dynamicConfirmed
    signal confirmed(string color)
    heading: title
    preferredWidth: 380
    windowHeightMargin: 48
    width: Math.min(preferredWidth, parent ? parent.width / uiScale * 0.92 : preferredWidth)
    flushBody: true
    closeButton.objectName: "colorPickerClose"
    closeButton.text: qsTr("关闭颜色选择")
    onAboutToShow: {
        draft = value;
        dynamicDraft = dynamicSelected;
    }
    onValueChanged: draft = value
    onAboutToHide: spectrumSurfaceOwner.close()
    body: ScrollView {
        id: scroll
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.minimumHeight: 0
        implicitHeight: paletteBody.implicitHeight
        contentWidth: availableWidth
        clip: true
        ScrollBar.vertical: EchoPopupScrollBar {
            surface: picker
            view: scroll
            resourceActive: picker.visible
        }
        ColumnLayout {
            id: paletteBody
            width: Math.max(0, scroll.availableWidth - 24)
            spacing: 14
            RowLayout {
                Layout.fillHeight: false
                Layout.fillWidth: true
                Layout.leftMargin: 12
                Layout.rightMargin: 12
                spacing: 10
                Button {
                    id: spectrumAction
                    objectName: "colorSpectrumAction"
                    implicitWidth: 36
                    implicitHeight: 36
                    Accessible.name: qsTr("打开颜色选择器")
                    onClicked: {
                        spectrumSurfaceOwner.open({
                            anchorItem: spectrumAction
                        });
                    }
                    background: Rectangle {
                        radius: Theme.cardRadius
                        color: picker.normalized
                        border.width: 1
                        border.color: Theme.floatingBorder
                    }
                }
                EchoTextField {
                    id: hexField
                    objectName: "colorHexInput"
                    implicitHeight: 36
                    leftPadding: 12
                    rightPadding: 12
                    Layout.fillWidth: true
                    text: picker.dynamicDraft ? picker.dynamicLabel : picker.draft
                    clearable: !picker.dynamicDraft
                    colors: Theme.floating
                    // Hex values intentionally use the system fixed-width face.
                    font: picker.dynamicDraft ? Theme.font(13, Theme.mediumWeight) : Theme.store.styledFont(EchoColorValue.fixedFont, 13, Theme.mediumWeight, 0, false, false)
                    color: Theme.floatingText
                    Accessible.name: qsTr("十六进制颜色")
                    onTextEdited: {
                        picker.draft = text;
                        picker.dynamicDraft = false;
                    }
                    background: Rectangle {
                        radius: Theme.controlRadius
                        color: Theme.withAlpha(Theme.store.floatingTone, hexField.activeFocus || hexField.hovered ? 0.07 : 0.05)
                    }
                }
            }
            GridLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 12
                Layout.rightMargin: 12
                columns: 6
                rowSpacing: 8
                columnSpacing: 8
                // The finite preset palette is not a scrolling business model.
                Repeater {
                    model: picker.presets
                    delegate: Button {
                        id: swatch
                        required property string modelData
                        required property int index
                        objectName: "colorPreset-" + index
                        Layout.fillWidth: true
                        Layout.preferredHeight: width
                        Accessible.name: qsTr("颜色 ") + modelData
                        Accessible.role: Accessible.CheckBox
                        Accessible.checked: !picker.dynamicDraft && picker.normalized === EchoColorValue.normalize(modelData)
                        hoverEnabled: true
                        scale: hovered ? 1.06 : 1
                        Behavior on scale {
                            NumberAnimation {
                                duration: Theme.fastDuration > 0 ? 150 : 0
                                easing.type: Easing.BezierSpline
                                easing.bezierCurve: [0.25, 0.1, 0.25, 1, 1, 1]
                            }
                        }
                        onClicked: {
                            picker.draft = modelData;
                            picker.dynamicDraft = false;
                        }
                        background: Rectangle {
                            radius: Theme.cardRadius
                            color: swatch.modelData
                            border.width: swatch.visualFocus ? 2 : 1
                            // A white-only outline disappears on light swatches/surfaces.
                            border.color: swatch.visualFocus ? Theme.accent : Theme.floating.controlBorder
                        }
                        contentItem: Item {
                            EchoIcon {
                                anchors.centerIn: parent
                                width: 16
                                height: 16
                                name: "check"
                                visible: !picker.dynamicDraft && picker.normalized === EchoColorValue.normalize(swatch.modelData)
                                color: EchoColorValue.contrast(swatch.modelData, "white") >= 3 ? "white" : "#1d1d1f"
                            }
                        }
                    }
                }
            }
            Button {
                id: dynamicAction
                objectName: "colorDynamicAction"
                visible: picker.dynamicLabel.length > 0
                Layout.fillWidth: true
                Layout.leftMargin: 12
                Layout.rightMargin: 12
                implicitHeight: contentItem.implicitHeight + topPadding + bottomPadding
                leftPadding: 12
                rightPadding: 12
                topPadding: 10
                bottomPadding: 10
                hoverEnabled: true
                text: picker.dynamicLabel
                Accessible.name: text + qsTr("，自动使用当前封面提取色")
                Accessible.role: Accessible.CheckBox
                Accessible.checked: picker.dynamicDraft
                onClicked: picker.dynamicDraft = true
                transform: Translate {
                    y: dynamicAction.hovered && Theme.store.motionEnabled ? -1 : 0
                    Behavior on y {
                        NumberAnimation {
                            duration: Theme.fastDuration
                        }
                    }
                }
                background: Rectangle {
                    radius: Theme.cardRadius
                    color: picker.dynamicDraft ? Theme.withAlpha(Theme.accent, .1) : Theme.withAlpha(Theme.floating.tone, .05)
                    border.width: dynamicAction.visualFocus ? 2 : 1
                    border.color: picker.dynamicDraft ? Theme.withAlpha(Theme.accent, .6) : dynamicAction.hovered || dynamicAction.visualFocus ? Theme.withAlpha(Theme.accent, .4) : Theme.floating.controlBorder
                    Behavior on color {
                        ColorAnimation {
                            duration: Theme.fastDuration
                        }
                    }
                    Behavior on border.color {
                        ColorAnimation {
                            duration: Theme.fastDuration
                        }
                    }
                }
                contentItem: RowLayout {
                    spacing: 12
                    Item {
                        objectName: "colorDynamicSwatch"
                        Layout.preferredWidth: 34
                        Layout.preferredHeight: 34
                        Shape {
                            anchors.fill: parent
                            ShapePath {
                                strokeWidth: 1
                                strokeColor: Qt.rgba(1, 1, 1, .28)
                                fillGradient: LinearGradient {
                                    x1: 0
                                    y1: 0
                                    x2: 34
                                    y2: 34
                                    GradientStop {
                                        position: 0
                                        color: "#ff5f6d"
                                    }
                                    GradientStop {
                                        position: .28
                                        color: "#ffc371"
                                    }
                                    GradientStop {
                                        position: .6
                                        color: "#24c6dc"
                                    }
                                    GradientStop {
                                        position: 1
                                        color: picker.dynamicColor
                                    }
                                }
                                PathRectangle {
                                    x: .5
                                    y: .5
                                    width: 33
                                    height: 33
                                    radius: Theme.cardRadius
                                }
                            }
                        }
                        EchoIcon {
                            anchors.centerIn: parent
                            width: picker.dynamicDraft ? 16 : 12
                            height: width
                            name: picker.dynamicDraft ? "check" : "palette"
                            color: "white"
                        }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.fillHeight: false
                        spacing: 0
                        EchoTextLine {
                            objectName: "colorDynamicTitle"
                            Layout.fillWidth: true
                            text: picker.dynamicLabel
                            font: Theme.font(13, Theme.semiboldWeight)
                            lineHeight: 19.5
                            color: Theme.floating.text
                        }
                        EchoTextLine {
                            objectName: "colorDynamicDescription"
                            Layout.fillWidth: true
                            text: qsTr("自动使用当前封面提取色")
                            font: Theme.font(11, Theme.regularWeight)
                            lineHeight: 16.5
                            color: Theme.floating.secondaryText
                        }
                    }
                }
            }
        }
    }
    footerContent: Component {
        RowLayout {
            Layout.fillHeight: false
            Layout.fillWidth: true
            spacing: 8
            Item {
                Layout.fillWidth: true
            }
            EchoButton {
                objectName: "colorCancelAction"
                accent: false
                implicitHeight: 36
                text: qsTr("取消")
                colors: Theme.floating
                onClicked: picker.reject()
            }
            EchoButton {
                objectName: "colorConfirmAction"
                implicitHeight: 36
                text: qsTr("确定")
                colors: Theme.floating
                accent: true
                onClicked: {
                    if (picker.dynamicDraft)
                        picker.dynamicConfirmed();
                    else
                        picker.confirmed(picker.normalized);
                    picker.accept();
                }
            }
        }
    }
    EchoPopupLoader {
        id: spectrumSurfaceOwner
        hostItem: picker.contentItem
        sourceComponent: Component {
            EchoAnchoredPopup {
                id: spectrumSurface
                anchorItem: null
                contentAlign: "start"
                uiScale: picker.uiScale
                preferredWidth: 264
                implicitHeight: 214
                padding: 12
                contentItem: EchoColorPicker {
                    value: picker.normalized
                    onColorEdited: color => {
                        picker.draft = color;
                        picker.dynamicDraft = false;
                    }
                }
            }
        }
    }
}
