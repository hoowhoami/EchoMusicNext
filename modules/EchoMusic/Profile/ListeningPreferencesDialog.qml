pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Account
import EchoMusic.Controls
import EchoMusic.Theme

EchoDialog {
    id: dialog
    objectName: "listeningPreferencesDialog"
    required property ListeningPreferencesService service
    preferredWidth: 640
    width: Math.min(640, parent ? parent.width / uiScale * 0.92 : 640)
    windowHeightMargin: 100
    maximumHeight: 860
    heading: qsTr("听歌偏好设置")
    closable: !service.saving
    managedDismiss: true
    closePolicy: Popup.NoAutoClose
    signal blacklistRequested
    property bool moreExpanded: false
    property int moreTab: 0
    onOpened: {
        service.load();
        moreExpanded = false;
        moreTab = 0;
    }
    function attemptClose(): void {
        if (service.saving)
            return;
        if (service.dirty)
            discardConfirmOwner.open();
        else
            close();
    }
    onDismissRequested: attemptClose()
    headerActions: EchoIconButton {
        glyph: "refresh"
        text: qsTr("刷新听歌偏好")
        colors: Theme.floating
        implicitWidth: 32
        implicitHeight: 32
        enabled: dialog.service.loaded && !dialog.service.loading && !dialog.service.saving && !dialog.service.dirty
        onClicked: dialog.service.load()
    }
    flushBody: true
    body: Item {
        implicitHeight: body.implicitHeight + 12
        Flickable {
            id: formView
            ScrollBar.vertical: EchoPopupScrollBar {
                surface: dialog
                view: formView
                resourceActive: dialog.visible
            }
            objectName: "listeningPreferencesScroll"
            anchors.fill: parent
            clip: true
            contentWidth: width
            contentHeight: body.implicitHeight + 12
            boundsBehavior: Flickable.StopAtBounds
            ColumnLayout {
                id: body
                y: 12
                width: formView.width - 24
                spacing: 14
                RowLayout {
                    Layout.fillHeight: false
                    spacing: 5
                    Layout.fillWidth: true
                    visible: dialog.service.error.length > 0
                    Label {
                        Layout.fillWidth: true
                        text: dialog.service.error
                        wrapMode: Text.Wrap
                        color: "#ef4444"
                        font: Theme.font(12, Theme.regularWeight)
                    }
                    EchoSoftButton {
                        visible: !dialog.service.loaded
                        text: qsTr("重新加载")
                        colors: Theme.floating
                        enabled: !dialog.service.loading
                        onClicked: dialog.service.load()
                    }
                }
                PreferenceCard {
                    Layout.fillWidth: true
                    RowLayout {
                        spacing: 12
                        Layout.fillWidth: true
                        Label {
                            Layout.fillWidth: true
                            text: qsTr("推荐偏好")
                            color: Theme.floatingText
                            font: Theme.font(13, Theme.extraBoldWeight)
                        }
                        EchoSoftButton {
                            id: blacklistButton
                            objectName: "preferencesBlacklist"
                            text: qsTr("黑名单")
                            Accessible.name: qsTr("打开黑名单管理")
                            glyph: "shield"
                            glyphSize: 14
                            colors: Theme.floating
                            size: EchoControlSize.Standard
                            font: Theme.font(11, Theme.boldWeight)
                            enabled: !dialog.service.saving
                            contentItem: EchoButtonContent {
                                spacing: 6
                                glyph: blacklistButton.glyph
                                glyphSize: blacklistButton.glyphSize
                                text: blacklistButton.text
                                color: blacklistButton.foreground
                                font: blacklistButton.font
                            }
                            onClicked: dialog.blacklistRequested()
                        }
                    }
                    Label {
                        visible: !dialog.service.loaded
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                        text: dialog.service.loading ? qsTr("正在读取你的听歌偏好") : qsTr("尚未读取听歌偏好")
                        color: Theme.floatingSecondaryText
                        font: Theme.font(12, Theme.regularWeight)
                    }
                    EchoTabBar {
                        colors: Theme.floating
                        visible: dialog.service.loaded
                        Layout.fillWidth: true
                        labels: [qsTr("熟悉模式"), qsTr("默认模式"), qsTr("尝鲜模式")]
                        currentIndex: dialog.service.modeIndex
                        enabled: !dialog.service.loading && !dialog.service.saving
                        onPicked: index => dialog.service.setMode(index)
                    }
                    Label {
                        visible: dialog.service.loaded
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.Wrap
                        text: dialog.service.modeDescription
                        color: Theme.floatingSecondaryText
                        font: Theme.font(12, Theme.regularWeight)
                    }
                }
                PreferenceCard {
                    visible: dialog.service.loaded
                    Layout.fillWidth: true
                    Label {
                        text: qsTr("语种与内容偏好")
                        color: Theme.floatingText
                        font: Theme.font(13, Theme.extraBoldWeight)
                    }
                    Label {
                        Layout.fillWidth: true
                        text: qsTr("拖动调整推荐强度，居中为默认，最左侧为屏蔽。")
                        color: Theme.floatingSecondaryText
                        font: Theme.font(12, Theme.regularWeight)
                        wrapMode: Text.Wrap
                    }
                    GridLayout {
                        Layout.fillWidth: true
                        columns: dialog.width <= 520 ? 1 : 2
                        columnSpacing: 10
                        rowSpacing: 10
                        // Ten fixed preference choices; no unbounded catalog.
                        Repeater {
                            model: dialog.service.strengths
                            delegate: PreferenceStrengthControl {
                                objectName: "preferenceStrength" + entryId
                                required property string entryId
                                required property string choiceLabel
                                required property int strengthValue
                                required property string strengthStatus
                                required property bool strengthEditable
                                Layout.fillWidth: true
                                label: choiceLabel
                                status: strengthStatus
                                value: strengthValue
                                enabled: strengthEditable && !dialog.service.loading && !dialog.service.saving
                                onMoved: dialog.service.setStrength(entryId, value)
                            }
                        }
                    }
                    Label {
                        visible: !dialog.service.recommendationEditable
                        Layout.fillWidth: true
                        text: qsTr("这组偏好暂时无法解析，已保留原值；其他偏好仍可编辑。")
                        color: Theme.floatingSecondaryText
                        font: Theme.font(12, Theme.regularWeight)
                        wrapMode: Text.Wrap
                    }
                }
                PreferenceCard {
                    visible: dialog.service.loaded
                    Layout.fillWidth: true
                    EchoActionButton {
                        Layout.fillWidth: true
                        implicitHeight: 28
                        padding: 0
                        background: Item {}
                        contentItem: RowLayout {
                            spacing: 8
                            Label {
                                text: qsTr("更多偏好")
                                font: Theme.font(13, Theme.extraBoldWeight)
                                color: Theme.floatingText
                            }
                            Label {
                                Layout.fillWidth: true
                                text: qsTr("细化你的音乐口味")
                                font: Theme.font(11, Theme.mediumWeight)
                                color: Theme.floatingSecondaryText
                            }
                            EchoIcon {
                                name: "chevron-down"
                                Layout.preferredWidth: 16
                                Layout.preferredHeight: 16
                                color: Theme.floatingSecondaryText
                                rotation: dialog.moreExpanded ? 180 : 0
                                Behavior on rotation {
                                    NumberAnimation {
                                        duration: 200
                                    }
                                }
                            }
                        }
                        onClicked: dialog.moreExpanded = !dialog.moreExpanded
                    }
                    EchoDivider {
                        visible: dialog.moreExpanded
                        lineColor: Theme.floatingBorder
                        Layout.fillWidth: true
                        Layout.topMargin: 6
                        Layout.bottomMargin: 6
                    }
                    EchoTabBar {
                        colors: Theme.floating
                        Layout.fillWidth: true
                        visible: dialog.moreExpanded
                        labels: [qsTr("语言"), qsTr("风格"), qsTr("关于你")]
                        currentIndex: dialog.moreTab
                        onPicked: index => dialog.moreTab = index
                    }
                    Label {
                        visible: dialog.moreExpanded && dialog.moreTab < 2
                        Layout.fillWidth: true
                        text: dialog.moreTab === 0 ? qsTr("喜欢的语言") + " · " + dialog.service.languagesHint : qsTr("喜欢的风格") + " · " + dialog.service.stylesHint
                        color: Theme.floatingSecondaryText
                        wrapMode: Text.Wrap
                        font: Theme.font(12, Theme.regularWeight)
                    }
                    GridLayout {
                        objectName: "listeningPreferenceOptions"
                        visible: dialog.moreExpanded && dialog.moreTab < 2
                        Layout.fillWidth: true
                        columns: dialog.width <= 400 ? 2 : dialog.width <= 520 ? 3 : 5
                        uniformCellWidths: true
                        columnSpacing: 8
                        rowSpacing: 8
                        Repeater {
                            model: dialog.moreTab === 0 ? dialog.service.languageOptions : dialog.service.styleOptions
                            delegate: PreferenceOptionButton {
                                required property var modelData
                                objectName: "listeningPreferenceOption-" + modelData.id
                                Layout.fillWidth: true
                                text: modelData.label
                                selected: modelData.selected
                                colors: Theme.floating
                                enabled: modelData.editable && !dialog.service.loading && !dialog.service.saving
                                onClicked: if (dialog.moreTab === 0)
                                    dialog.service.toggleLanguage(modelData.id)
                                else
                                    dialog.service.toggleStyle(modelData.id)
                            }
                        }
                    }
                    RowLayout {
                        spacing: 5
                        visible: dialog.moreExpanded && dialog.moreTab === 2
                        Layout.fillWidth: true
                        Label {
                            Layout.fillWidth: true
                            text: qsTr("性别")
                            color: Theme.floatingText
                            font: Theme.font(12, Theme.boldWeight)
                        }
                        EchoSelect {
                            implicitWidth: 160
                            model: dialog.service.genderOptions
                            textRole: "label"
                            colors: Theme.floating
                            currentIndex: dialog.service.genderIndex
                            enabled: !dialog.service.loading && !dialog.service.saving
                            onActivated: dialog.service.setGender(currentIndex)
                        }
                    }
                    RowLayout {
                        spacing: 5
                        visible: dialog.moreExpanded && dialog.moreTab === 2
                        Layout.fillWidth: true
                        Label {
                            Layout.fillWidth: true
                            text: qsTr("年龄段")
                            color: Theme.floatingText
                            font: Theme.font(12, Theme.boldWeight)
                        }
                        EchoSelect {
                            implicitWidth: 160
                            model: dialog.service.ageOptions
                            textRole: "label"
                            colors: Theme.floating
                            currentIndex: dialog.service.ageIndex
                            enabled: !dialog.service.loading && !dialog.service.saving
                            onActivated: dialog.service.setAge(currentIndex)
                        }
                    }
                }
            }
        }
    }
    footerContent: ColumnLayout {
        spacing: 10
        RowLayout {
            Layout.fillWidth: true
            spacing: 12
            EchoSoftButton {
                Layout.fillWidth: true
                text: qsTr("恢复默认")
                colors: Theme.floating
                enabled: dialog.service.loaded && dialog.service.canReset && !dialog.service.loading && !dialog.service.saving
                onClicked: dialog.service.resetRecommendation()
            }
            EchoButton {
                Layout.fillWidth: true
                text: dialog.service.saving ? qsTr("正在保存…") : qsTr("保存偏好")
                size: EchoControlSize.Standard
                colors: Theme.floating
                enabled: dialog.service.loaded && !dialog.service.loading && !dialog.service.saving && dialog.service.dirty
                onClicked: dialog.service.save()
            }
        }
        Label {
            Layout.fillWidth: true
            text: qsTr("恢复默认仅重置推荐模式与强度，点击保存后生效。")
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            color: Theme.floatingSecondaryText
            font: Theme.font(10, Theme.regularWeight)
        }
    }
    EchoPopupLoader {
        id: discardConfirmOwner
        objectName: "listeningDiscardConfirmOwner"
        hostItem: dialog.contentItem
        sourceComponent: Component {
            EchoDialog {
                id: discardConfirm
                objectName: "listeningDiscardConfirm"
                uiScale: dialog.uiScale
                heading: qsTr("放弃未保存的偏好？")
                description: qsTr("你的选择还没有保存，关闭后将丢弃本次修改。")
                footerContent: RowLayout {
                    spacing: 5
                    Item {
                        Layout.fillWidth: true
                    }
                    EchoSoftButton {
                        text: qsTr("继续编辑")
                        colors: Theme.floating
                        onClicked: discardConfirm.close()
                    }
                    EchoSoftButton {
                        text: qsTr("放弃修改")
                        danger: true
                        colors: Theme.floating
                        onClicked: {
                            dialog.service.discard();
                            discardConfirm.close();
                            dialog.close();
                        }
                    }
                }
            }
        }
    }
    onClosed: discardConfirmOwner.close()
}
