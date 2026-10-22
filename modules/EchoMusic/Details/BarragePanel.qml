pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Controls
import EchoMusic.Theme
import EchoMusic.Shell

EchoAnchoredPopup {
    id: popup
    required property BarrageController controller
    required property ShellSettings settings
    property bool lyric: false
    preferredWidth: 632
    maximumHeight: 600
    padding: 16
    contentAlign: "center"
    readonly property var config: lyric ? settings.lyricBarrageConfig : settings.mvBarrageConfig
    function setOption(option: int, value: real): void {
        if (lyric)
            settings.setLyricBarrageOption(option, value);
        else
            settings.setMvBarrageOption(option, value);
    }
    Connections {
        target: popup.controller
        function onSubmitted(): void {
            popup.close();
        }
    }
    contentItem: Flickable {
        id: viewport
        implicitHeight: panel.implicitHeight
        contentHeight: panel.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: EchoPopupScrollBar {
            surface: popup
            view: viewport
        }
        ColumnLayout {
            id: panel
            width: viewport.width
            spacing: 14
            RowLayout {
                Layout.fillWidth: true
                spacing: 12
                EchoIcon {
                    name: "barrage"
                    color: Theme.floatingSecondaryText
                    Layout.preferredWidth: 18
                    Layout.preferredHeight: 18
                }
                EchoTextTagRow {
                    Layout.fillWidth: true
                    text: qsTr("弹幕")
                    color: Theme.floatingText
                    font: Theme.font(14, Theme.semiboldWeight)
                }
                EchoSwitch {
                    objectName: "barrageEnabledSwitch"
                    checked: popup.lyric ? popup.settings.lyricBarrageEnabled : popup.settings.mvBarrageEnabled
                    Accessible.name: qsTr("显示弹幕")
                    onToggled: if (popup.lyric)
                        popup.settings.lyricBarrageEnabled = checked
                    else
                        popup.settings.mvBarrageEnabled = checked
                }
                EchoIconButton {
                    objectName: "barrageClose"
                    implicitWidth: 26
                    implicitHeight: 26
                    padding: 5
                    glyph: "x"
                    text: qsTr("关闭弹幕面板")
                    tooltipText: ""
                    onClicked: popup.close()
                }
            }
            GridLayout {
                id: settingsGrid
                readonly property bool wide: popup.parent && popup.parent.width / popup.uiScale > 620
                Layout.fillWidth: true
                columns: wide ? 2 : 1
                columnSpacing: 20
                rowSpacing: 16
                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumWidth: 0
                    Layout.preferredWidth: 0
                    Layout.horizontalStretchFactor: 20
                    implicitHeight: settingsColumn.implicitHeight + (settingsGrid.wide ? 0 : 16)
                    ColumnLayout {
                        id: settingsColumn
                        width: Math.max(0, parent.width - (settingsGrid.wide ? 20 : 0))
                        spacing: 12
                        Repeater {
                            model: [
                                {
                                    label: qsTr("透明度"),
                                    key: "opacity",
                                    option: ShellSettings.BarrageOpacity,
                                    minimum: 20,
                                    maximum: 100,
                                    step: 5,
                                    suffix: "%"
                                },
                                {
                                    label: qsTr("字号"),
                                    key: "fontSize",
                                    option: ShellSettings.BarrageFontSize,
                                    minimum: 12,
                                    maximum: 28,
                                    step: 1,
                                    suffix: " px"
                                },
                                {
                                    label: qsTr("速度"),
                                    key: "speed",
                                    option: ShellSettings.BarrageSpeed,
                                    minimum: .5,
                                    maximum: 2,
                                    step: .25,
                                    suffix: "×"
                                }
                            ]
                            delegate: ColumnLayout {
                                id: setting
                                required property var modelData
                                Layout.fillWidth: true
                                spacing: 0
                                RowLayout {
                                    Layout.fillWidth: true
                                    Label {
                                        Layout.fillWidth: true
                                        text: setting.modelData.label
                                        color: Theme.floatingText
                                        font: Theme.font(12, Theme.regularWeight)
                                    }
                                    Label {
                                        text: popup.config[setting.modelData.key] + setting.modelData.suffix
                                        color: Theme.floatingSecondaryText
                                        font: Theme.font(12, Theme.regularWeight)
                                    }
                                }
                                EchoSlider {
                                    colors: Theme.floating
                                    Layout.fillWidth: true
                                    from: setting.modelData.minimum
                                    to: setting.modelData.maximum
                                    stepSize: setting.modelData.step
                                    value: popup.config[setting.modelData.key]
                                    Accessible.name: qsTr("弹幕%1").arg(setting.modelData.label)
                                    onMoved: popup.setOption(setting.modelData.option, value)
                                }
                            }
                        }
                        Label {
                            text: qsTr("密度")
                            color: Theme.floatingText
                            font: Theme.font(12, Theme.regularWeight)
                        }
                        EchoTabBar {
                            colors: Theme.floating
                            Layout.fillWidth: true
                            compact: true
                            labels: [qsTr("稀疏"), qsTr("适中"), qsTr("密集")]
                            currentIndex: popup.config.density - 1
                            onPicked: index => popup.setOption(ShellSettings.BarrageDensity, index + 1)
                        }
                        Label {
                            text: qsTr("显示区域")
                            color: Theme.floatingText
                            font: Theme.font(12, Theme.regularWeight)
                        }
                        EchoTabBar {
                            colors: Theme.floating
                            Layout.fillWidth: true
                            compact: true
                            labels: [qsTr("顶部"), qsTr("上半屏"), qsTr("全屏")]
                            currentIndex: [25, 50, 100].indexOf(popup.config.area)
                            onPicked: index => popup.setOption(ShellSettings.BarrageArea, [25, 50, 100][index])
                        }
                    }
                    Rectangle {
                        x: settingsGrid.wide ? parent.width - 1 : 0
                        y: settingsGrid.wide ? 0 : parent.height - 1
                        width: settingsGrid.wide ? 1 : parent.width
                        height: settingsGrid.wide ? parent.height : 1
                        color: Theme.subtleBorder
                    }
                }
                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumWidth: 0
                    Layout.preferredWidth: 0
                    Layout.horizontalStretchFactor: 23
                    implicitHeight: composerColumn.implicitHeight + 20
                    radius: Theme.cardRadius
                    color: Theme.elevated
                    border.width: 1
                    border.color: draft.activeFocus ? Theme.withAlpha(Theme.accent, .55) : Theme.border
                    ColumnLayout {
                        id: composerColumn
                        anchors.fill: parent
                        anchors.margins: 10
                        anchors.leftMargin: 12
                        anchors.rightMargin: 12
                        spacing: 12
                        EchoTextArea {
                            id: draft
                            colors: Theme.floating
                            objectName: popup.lyric ? "lyricBarrageDraft" : "mvBarrageDraft"
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            Layout.minimumHeight: 112
                            text: popup.controller.draft
                            placeholderText: popup.controller.signedIn ? qsTr("写下此刻的感受…") : qsTr("登录后即可发送")
                            Accessible.name: qsTr("发送弹幕")
                            font: Theme.font(13, Theme.regularWeight)
                            padding: 4
                            topPadding: 6
                            bottomPadding: 6
                            background: null
                            enabled: !popup.controller.busy
                            onTextChanged: popup.controller.draft = text
                            Keys.onReturnPressed: event => {
                                if (inputMethodComposing || event.modifiers !== Qt.NoModifier)
                                    event.accepted = false;
                                else {
                                    event.accepted = true;
                                    if (!event.isAutoRepeat)
                                        popup.controller.send();
                                }
                            }
                            Keys.onEnterPressed: event => {
                                if (inputMethodComposing || event.modifiers !== Qt.NoModifier)
                                    event.accepted = false;
                                else {
                                    event.accepted = true;
                                    if (!event.isAutoRepeat)
                                        popup.controller.send();
                                }
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            Label {
                                Layout.fillWidth: true
                                text: Array.from(draft.text).length + " / 100"
                                color: Array.from(draft.text).length > 100 ? Theme.floating.dangerText : Theme.floatingSecondaryText
                                font: Theme.font(12, Theme.regularWeight)
                            }
                            EchoSoftButton {
                                colors: Theme.floating
                                text: popup.controller.busy ? qsTr("发送中…") : qsTr("发送")
                                enabled: popup.controller.signedIn && !popup.controller.busy && draft.text.trim().length > 0 && Array.from(draft.text).length <= 100
                                onClicked: popup.controller.send()
                            }
                        }
                    }
                }
            }
        }
    }
}
