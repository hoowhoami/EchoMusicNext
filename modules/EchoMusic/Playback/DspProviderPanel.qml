pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Shapes
import EchoMusic.Controls
import EchoMusic.Theme
import EchoMusic.Playback

GridView {
    id: panel
    required property PlaybackAudioControls audio
    property real uiScale: 1
    property Item editorHost: panel
    readonly property real presetGap: 7
    clip: true
    objectName: "dspPresetGrid"
    leftMargin: 10
    rightMargin: 10
    // The final cell already includes a trailing row gap; retain the outer padding.
    bottomMargin: audio.providerPresets.length > 0 ? 12 - presetGap : 12
    cellWidth: (width - 20) / 2
    cellHeight: 38 + presetGap
    model: visible ? audio.providerPresets : null
    reuseItems: true
    EchoViewPoolGuard {
        targetView: panel
    }
    cacheBuffer: cellHeight * 3
    boundsBehavior: Flickable.StopAtBounds
    ScrollBar.vertical: EchoScrollBar {}
    header: ColumnLayout {
        id: body
        width: panel.width - 20
        spacing: 14
        Rectangle {
            objectName: "dspProviderEngineCard"
            Layout.fillWidth: true
            implicitHeight: engineInfo.implicitHeight + 24
            radius: Theme.cardRadius
            color: "transparent"
            Shape {
                anchors.fill: parent
                ShapePath {
                    strokeWidth: 0
                    fillGradient: LinearGradient {
                        x1: 0
                        y1: 0
                        x2: engineInfo.width + 24
                        y2: engineInfo.implicitHeight + 24
                        GradientStop {
                            position: 0
                            color: Theme.withAlpha(Theme.floating.accent, 0.11)
                        }
                        GradientStop {
                            position: 1
                            color: Theme.withAlpha(Theme.floating.accent, 0.03)
                        }
                    }
                    PathRectangle {
                        width: engineInfo.width + 24
                        height: engineInfo.implicitHeight + 24
                        radius: Theme.cardRadius
                    }
                }
            }
            ColumnLayout {
                id: engineInfo
                x: 12
                y: 12
                width: parent.width - 24
                spacing: 12
                RowLayout {
                    Layout.fillHeight: false
                    Layout.fillWidth: true
                    spacing: 6
                    EchoTextTagRow {
                        objectName: "dspProviderNameRow"
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        text: panel.audio.providerName
                        font: Theme.font(13, Theme.boldWeight)
                        lineHeight: 20
                        color: Theme.floating.text
                        textTagSpacing: 6
                        EchoTextTagRow {
                            objectName: "dspProviderVersion"
                            text: "v" + (panel.audio.providerInfo.version || "")
                            font: Theme.font(9, Theme.semiboldWeight)
                            lineHeight: 20
                            color: Theme.floating.secondaryText
                        }
                    }
                    RowLayout {
                        Layout.fillHeight: false
                        objectName: "dspProviderStatusRow"
                        Layout.fillWidth: false
                        Layout.minimumWidth: implicitWidth
                        Layout.maximumWidth: implicitWidth
                        spacing: 5
                        Rectangle {
                            Layout.preferredWidth: 6
                            Layout.minimumWidth: 6
                            Layout.maximumWidth: 6
                            Layout.preferredHeight: 6
                            Layout.minimumHeight: 6
                            Layout.maximumHeight: 6
                            radius: 3
                            color: "#22a06b"
                        }
                        EchoTextTagRow {
                            objectName: "dspProviderStatusText"
                            text: panel.audio.effectPending ? qsTr("加载中") : panel.audio.graph.provider_id ? qsTr("运行中") : qsTr("已就绪")
                            font: Theme.font(9, Theme.boldWeight)
                            lineHeight: 20
                            color: "#22a06b"
                        }
                    }
                }
                RowLayout {
                    Layout.fillHeight: false
                    spacing: 5
                    Layout.fillWidth: true
                    EchoTextTagRow {
                        Layout.fillWidth: true
                        text: qsTr("输出设备")
                        font: Theme.font(10, Theme.boldWeight)
                        lineHeight: 20
                        color: Theme.floating.secondaryText
                    }
                    EchoTabBar {
                        objectName: "dspOutputDeviceTabs"
                        Layout.fillWidth: false
                        Layout.preferredWidth: 160
                        Layout.minimumWidth: 160
                        Layout.maximumWidth: 160
                        Layout.fillHeight: false
                        implicitWidth: 160
                        compact: true
                        labels: [qsTr("扬声器"), qsTr("耳机")]
                        currentIndex: panel.audio.providerMode === "headphone" ? 1 : 0
                        colors: Theme.floating
                        enabled: !panel.audio.effectPending
                        onPicked: index => panel.audio.setProviderMode(index === 1 ? "headphone" : "speaker")
                    }
                }
                Flow {

                    Layout.fillWidth: true
                    spacing: 5
                    Repeater {
                        model: ((panel.audio.providerInfo.manifest || {}).resources || []).slice(0, 64)
                        delegate: EchoTag {
                            required property var modelData
                            text: modelData.kind === "vpf" ? qsTr("VPF 支持") : modelData.kind === "impulse-response" ? qsTr("脉冲响应") : modelData.kind
                            tone: modelData.kind === "vpf" ? EchoTag.Custom : EchoTag.Muted
                            customTone: "#22a06b"
                            colors: Theme.floating
                        }
                    }
                }
            }
        }
        RowLayout {
            Layout.fillHeight: false
            spacing: 5
            objectName: "dspPresetSectionHeading"
            visible: panel.audio.providerPresets.length > 0
            Layout.fillWidth: true
            Layout.leftMargin: 2
            Layout.rightMargin: 2
            Layout.bottomMargin: 8
            EchoTextTagRow {
                Layout.fillWidth: true
                text: qsTr("引擎预设")
                font: Theme.font(11, Theme.boldWeight)
                lineHeight: 17
                color: Theme.floating.text
            }
            EchoTextTagRow {
                Layout.fillWidth: false
                Layout.maximumWidth: implicitWidth
                text: qsTr("由当前音效引擎提供")
                font: Theme.font(9, Theme.semiboldWeight)
                lineHeight: 17
                color: Theme.floating.secondaryText
            }
        }
    }
    delegate: Item {
        id: preset
        required property int index
        required property var modelData
        width: panel.cellWidth
        height: 38
        // GridView owns cell coordinates. Split the sole 7px column gap inside
        // the cells so both outside edges stay flush with the engine card.
        Item {
            id: presetOption
            objectName: "dspPresetOption-" + preset.modelData.id
            x: preset.index % 2 * panel.presetGap / 2
            width: preset.width - panel.presetGap / 2
            height: preset.height
            EchoActionBackground {
                anchors.fill: parent
                radius: Theme.itemRadius
                colors: Theme.floating
                accent: panel.audio.providerPresetId === preset.modelData.id
                highlighted: presetHover.hovered
                pressed: presetButton.down
            }
            HoverHandler {
                id: presetHover
            }
            Button {
                id: presetButton
                objectName: "dspPresetButton-" + preset.modelData.id
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                anchors.right: presetSettings.visible ? presetSettings.left : parent.right
                leftPadding: 9
                rightPadding: 9
                topPadding: 7
                bottomPadding: 7
                text: preset.modelData.label
                font: Theme.font(11, Theme.boldWeight)
                enabled: !panel.audio.effectPending
                opacity: enabled ? 1 : 0.45
                onClicked: panel.audio.selectProviderPreset(preset.modelData.id)
                Accessible.description: preset.modelData.description || ""
                EchoToolTip {
                    parent: preset
                    text: preset.modelData.label
                    description: preset.modelData.description !== preset.modelData.label ? preset.modelData.description || "" : ""
                    requestedVisible: presetHover.hovered && (description.length > 0 || presetText.truncated)
                }
                contentItem: Item {
                    implicitHeight: presetText.implicitHeight
                    EchoTextTagRow {
                        id: presetText
                        objectName: "dspPresetLabel"
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width
                        text: presetButton.text
                        font: presetButton.font
                        color: panel.audio.providerPresetId === preset.modelData.id ? Theme.floating.accentText : Theme.floating.text
                        lineHeight: 16
                        fillTextWidth: true
                        textTagSpacing: 6
                        EchoTag {
                            objectName: "dspPresetDeviceTag"
                            visible: preset.modelData.recommendedDevice === "headphone"
                            text: qsTr("耳机")
                            tone: EchoTag.Accent
                            colors: Theme.floating
                        }
                    }
                }
                background: Rectangle {
                    color: "transparent"
                    radius: Theme.controlRadius
                    border.width: presetButton.visualFocus ? 2 : 0
                    border.color: Theme.floating.accent
                }
            }
            EchoIconButton {
                id: presetSettings
                objectName: "dspPresetSettings-" + preset.modelData.id
                visible: (preset.modelData.controls || []).length > 0
                glyph: "settings"
                text: preset.modelData.label + qsTr("设置")
                colors: Theme.floating
                implicitWidth: 28
                implicitHeight: 28
                anchors.right: parent.right
                anchors.rightMargin: 4
                anchors.verticalCenter: parent.verticalCenter
                padding: 6.5
                enabled: !panel.audio.effectPending
                onClicked: {
                    panel.audio.editProviderPreset(preset.modelData.id);
                    controlDialog.open();
                }
            }
        }
    }
    footer: ColumnLayout {
        width: panel.width - 20
        spacing: 14
        Label {
            id: emptyPresets
            visible: panel.audio.providerPresets.length === 0
            Layout.fillWidth: true
            text: qsTr("当前引擎没有声明额外预设。")
            padding: 12
            horizontalAlignment: Text.AlignHCenter
            color: Theme.floating.secondaryText
            font: Theme.font(10, Theme.semiboldWeight)
            background: Shape {
                ShapePath {
                    strokeWidth: 1
                    strokeStyle: ShapePath.DashLine
                    strokeColor: Theme.floating.controlBorder
                    fillColor: "transparent"
                    PathRectangle {
                        x: 0.5
                        y: 0.5
                        width: emptyPresets.width - 1
                        height: emptyPresets.height - 1
                        radius: Theme.controlRadius
                    }
                }
            }
        }
        Label {
            Layout.fillWidth: true
            visible: text.length > 0
            text: panel.audio.errorMessage
            font: Theme.font(12, Theme.regularWeight)
            color: Theme.floating.dangerText
            wrapMode: Text.Wrap
        }
    }
    Rectangle {
        id: controlDialog
        objectName: "dspPresetControlsPanel"
        parent: panel.editorHost === panel ? panel : panel.editorHost.parent
        x: panel.editorHost === panel ? 0 : panel.editorHost.x
        y: panel.editorHost === panel ? 0 : panel.editorHost.y
        width: panel.editorHost.width
        height: panel.editorHost.height
        z: 4
        visible: false
        color: "transparent"
        Shape {
            anchors.fill: parent
            ShapePath {
                strokeWidth: 0
                fillColor: Theme.floating.elevated
                PathRectangle {
                    width: controlDialog.width
                    height: controlDialog.height
                    topLeftRadius: 0
                    bottomLeftRadius: 0
                    topRightRadius: Theme.popoverRadius
                    bottomRightRadius: Theme.popoverRadius
                }
            }
        }
        property string heading: panel.audio.providerPresets.filter(p => p.id === panel.audio.providerEditingPreset).map(p => p.label).join("") + qsTr("设置")
        function open(): void {
            visible = true;
            forceActiveFocus();
        }
        function close(): void {
            visible = false;
        }
        Keys.onEscapePressed: event => {
            close();
            event.accepted = true;
        }
        MouseArea {
            anchors.fill: parent
        }
        RowLayout {
            id: editorHeader
            x: 10
            y: 12
            width: parent.width - 20
            spacing: 8
            EchoIconButton {
                objectName: "dspPresetEditorBack"
                Layout.fillWidth: false
                Layout.fillHeight: false
                Layout.minimumWidth: 28
                Layout.maximumWidth: 28
                Layout.minimumHeight: 28
                Layout.maximumHeight: 28
                glyph: "arrow-left"
                text: qsTr("返回引擎预设")
                colors: Theme.floating
                implicitWidth: 28
                implicitHeight: 28
                padding: 5
                onClicked: controlDialog.close()
            }
            ColumnLayout {
                Layout.fillHeight: false
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                spacing: 3
                EchoTextTagRow {
                    Layout.fillWidth: true
                    text: controlDialog.heading
                    font: Theme.font(13, Theme.boldWeight)
                    lineHeight: 20
                    color: Theme.floating.text
                }
                EchoTextTagRow {
                    Layout.fillWidth: true
                    text: panel.audio.providerName + " · " + (panel.audio.providerMode === "headphone" ? qsTr("耳机") : qsTr("扬声器"))
                    font: Theme.font(10, Theme.regularWeight)
                    lineHeight: 15
                    color: Theme.floating.secondaryText
                }
            }
            EchoButton {
                text: qsTr("恢复默认")
                Layout.fillWidth: false
                Layout.fillHeight: false
                Layout.maximumWidth: implicitWidth
                size: EchoControlSize.Compact
                accent: false
                colors: Theme.floating
                enabled: !panel.audio.effectPending
                onClicked: panel.audio.resetProviderControls()
            }
        }
        ListView {
            id: controls
            y: editorHeader.y + editorHeader.height + 12
            width: parent.width
            height: parent.height - y
            reuseItems: true
            EchoViewPoolGuard {
                targetView: controls
            }
            clip: true
            model: panel.audio.providerControls
            leftMargin: 14
            rightMargin: 14
            bottomMargin: 14
            spacing: 7
            ScrollBar.vertical: EchoScrollBar {}
            header: Label {
                width: controls.width - 28
                topPadding: 6
                bottomPadding: 12
                text: panel.audio.providerEditingPreset === panel.audio.providerPresetId ? panel.audio.effectPending ? qsTr("等待应用 · 滑杆松手后生效") : qsTr("已应用 · 滑杆松手后生效") : qsTr("参数自动保存，选用此音效时生效")
                font: Theme.font(11, Theme.regularWeight)
                color: Theme.floating.secondaryText
                wrapMode: Text.Wrap
            }
            delegate: DspProviderControlCard {
                width: controls.width - 28
                height: implicitHeight
                audio: panel.audio
            }
        }
    }
    onVisibleChanged: if (!visible) {
        controlDialog.close();
    }
    Connections {
        target: panel.audio
        function onChanged() {
            if (!panel.audio.providerEditingPreset.length)
                controlDialog.close();
        }
    }
    Component.onDestruction: {
        controlDialog.close();
    }
}
