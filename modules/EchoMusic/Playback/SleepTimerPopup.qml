pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Theme
import EchoMusic.Controls
import EchoMusic.Playback

EchoAnchoredPopup {
    id: popup
    objectName: "sleepTimerPopup"
    required property PlaybackService playback
    readonly property SleepTimer timer: playback.sleepTimer
    property int selectedMinutes: 30
    property bool custom: false
    property string selectedAction: "pause"
    readonly property string actionLabel: timer.action === "pause" ? qsTr("暂停播放") : timer.action === "quit" ? qsTr("退出应用") : qsTr("关闭电脑")
    readonly property bool stacked: width < 516
    readonly property color choiceBorder: Theme.withAlpha(Theme.floating.text, Theme.dark ? .32 : .26)
    preferredWidth: 596
    maximumHeight: 650
    preferAbove: true
    alignEnd: false
    sideOffset: 8
    showArrow: false
    padding: 20
    implicitHeight: panel.implicitHeight + 40
    onOpened: {
        selectedAction = timer.action;
        selectedMinutes = timer.durationMinutes;
        custom = [10, 20, 30, 60].indexOf(selectedMinutes) < 0;
    }
    function chooseDuration(minutes: int) {
        selectedMinutes = minutes;
        custom = false;
        if (timer.active)
            timer.start(minutes, timer.action);
    }
    contentItem: Flickable {
        id: scroll
        ScrollBar.vertical: EchoPopupScrollBar {
            surface: popup
            view: scroll
            resourceActive: popup.visible
        }
        clip: true
        contentHeight: panel.implicitHeight
        boundsBehavior: Flickable.StopAtBounds
        ColumnLayout {
            id: panel
            width: parent.width
            spacing: 0
            RowLayout {
                Layout.fillHeight: false
                Layout.fillWidth: true
                spacing: 12
                Rectangle {
                    Layout.preferredWidth: 42
                    Layout.preferredHeight: 42
                    radius: Theme.cardRadius
                    color: Theme.withAlpha(Theme.accent, .10)
                    EchoIcon {
                        anchors.centerIn: parent
                        width: 22
                        height: 22
                        name: "moon"
                        color: Theme.floating.accentText
                    }
                }
                ColumnLayout {
                    Layout.fillHeight: false
                    Layout.fillWidth: true
                    spacing: 2
                    Label {
                        text: qsTr("定时关闭")
                        font: Theme.font(15, Theme.boldWeight)
                        color: Theme.floating.text
                    }
                    Label {
                        text: qsTr("让音乐陪你入睡")
                        font: Theme.font(11, Theme.regularWeight)
                        color: Theme.floating.secondaryText
                    }
                }
            }
            GridLayout {
                Layout.fillWidth: true
                Layout.topMargin: 18
                columns: popup.stacked ? 1 : 2
                columnSpacing: 22
                rowSpacing: 16
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignTop
                    spacing: 12
                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: 108
                        radius: Theme.controlRadius
                        color: popup.timer.active ? Theme.mix(Theme.elevated, Theme.accent, .07) : Theme.floating.mutedSurface
                        Column {
                            anchors.centerIn: parent
                            spacing: 4
                            Label {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: popup.timer.active ? popup.timer.waiting ? qsTr("时间到了，晚安") : qsTr("距离%1还有").arg(popup.actionLabel) : qsTr("今晚，听多久？")
                                font: Theme.font(popup.timer.active ? 11 : 13, popup.timer.active ? Theme.regularWeight : Theme.semiboldWeight)
                                color: popup.timer.active ? Theme.floating.secondaryText : Theme.floating.text
                            }
                            Label {
                                visible: popup.timer.active
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: popup.timer.waiting ? qsTr("等待本首结束") : Math.floor(popup.timer.remainingSeconds / 60).toString().padStart(2, "0") + ":" + (popup.timer.remainingSeconds % 60).toString().padStart(2, "0")
                                font: Theme.spacedFont(popup.timer.waiting ? 21 : 36, Theme.strongWeight, popup.timer.waiting ? 0 : 1)
                                color: Theme.floating.accentText
                            }
                            Label {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: !popup.timer.active ? qsTr("选择时长与到时动作") : popup.timer.waiting ? qsTr("本首结束后%1").arg(popup.actionLabel) : qsTr("预计 %1%2%3").arg(popup.timer.endTime).arg(popup.timer.finishTrack ? qsTr(" 后，播完本首") : " ").arg(popup.actionLabel)
                                font: Theme.font(11, Theme.regularWeight)
                                color: Theme.floating.secondaryText
                            }
                        }
                    }
                    RowLayout {
                        Layout.fillHeight: false
                        Layout.fillWidth: true
                        Layout.topMargin: 4
                        spacing: 8
                        Repeater {
                            model: [10, 20, 30, 60]
                            delegate: Button {
                                id: durationButton
                                required property int modelData
                                objectName: "sleepDuration-" + modelData
                                readonly property bool selected: !popup.custom && popup.selectedMinutes === modelData
                                Layout.fillWidth: true
                                implicitHeight: 60
                                enabled: !popup.timer.executing
                                hoverEnabled: true
                                Accessible.name: qsTr("%1 分钟").arg(modelData)
                                onClicked: popup.chooseDuration(modelData)
                                // Button stretches its contentItem; center the natural-height
                                // number/unit group inside it instead of stretching the Column.
                                contentItem: Item {
                                    Column {
                                        objectName: "sleepDurationContent"
                                        anchors.centerIn: parent
                                        spacing: 2
                                        Label {
                                            anchors.horizontalCenter: parent.horizontalCenter
                                            text: durationButton.modelData === 60 ? "1" : durationButton.modelData
                                            font: Theme.font(17, Theme.boldWeight)
                                            color: durationButton.selected ? Theme.floating.accentText : Theme.floating.text
                                        }
                                        Label {
                                            anchors.horizontalCenter: parent.horizontalCenter
                                            text: durationButton.modelData === 60 ? qsTr("小时") : qsTr("分钟")
                                            font: Theme.font(10, Theme.regularWeight)
                                            color: Theme.floating.secondaryText
                                        }
                                    }
                                }
                                background: Rectangle {
                                    radius: Theme.controlRadius
                                    color: durationButton.selected ? Theme.withAlpha(Theme.accent, .09) : durationButton.hovered ? Theme.floating.hover : "transparent"
                                    border.width: 1
                                    border.color: durationButton.selected ? Theme.mix(Theme.elevated, Theme.accent, .65) : popup.choiceBorder
                                }
                            }
                        }
                    }
                    Button {
                        id: customButton
                        Layout.fillWidth: true
                        implicitHeight: 38
                        leftPadding: 12
                        rightPadding: 12
                        enabled: !popup.timer.executing
                        hoverEnabled: true
                        onClicked: popup.custom = true
                        contentItem: RowLayout {
                            spacing: 5
                            Label {
                                Layout.fillWidth: true
                                text: qsTr("自定义时长")
                                font: Theme.font(12, Theme.regularWeight)
                                color: popup.custom ? Theme.floating.accentText : Theme.floating.text
                            }
                            EchoIcon {
                                visible: popup.custom
                                Layout.preferredWidth: 16
                                Layout.preferredHeight: 16
                                name: "check"
                                color: Theme.floating.accentText
                            }
                            Label {
                                visible: !popup.custom
                                text: qsTr("1–180 分钟")
                                font: Theme.font(11, Theme.regularWeight)
                                color: Theme.floating.secondaryText
                            }
                        }
                        background: Rectangle {
                            radius: Theme.controlRadius
                            color: popup.custom ? Theme.withAlpha(Theme.accent, .09) : customButton.hovered ? Theme.floating.hover : "transparent"
                            border.width: 1
                            border.color: popup.custom ? Theme.mix(Theme.elevated, Theme.accent, .65) : popup.choiceBorder
                        }
                    }
                    RowLayout {
                        Layout.fillHeight: false
                        visible: popup.custom
                        spacing: 10
                        EchoSpinBox {
                            colors: Theme.floating
                            from: 1
                            to: 180
                            value: popup.selectedMinutes
                            onValueModified: {
                                popup.selectedMinutes = value;
                                if (popup.timer.active)
                                    popup.timer.start(value, popup.timer.action);
                            }
                            Accessible.name: qsTr("自定义分钟数")
                        }
                        Label {
                            text: qsTr("分钟后执行")
                            font: Theme.font(12, Theme.regularWeight)
                            color: Theme.floating.secondaryText
                        }
                    }
                }
                RowLayout {
                    Layout.fillHeight: false
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignTop
                    spacing: popup.stacked ? 0 : 22
                    Rectangle {
                        visible: !popup.stacked
                        Layout.fillHeight: true
                        implicitWidth: 1
                        color: Theme.withAlpha(Theme.floating.text, Theme.dark ? .22 : .16)
                    }
                    ColumnLayout {
                        Layout.fillHeight: false
                        Layout.fillWidth: true
                        spacing: 8
                        Label {
                            text: qsTr("到时动作")
                            font: Theme.font(11, Theme.semiboldWeight)
                            color: Theme.floating.secondaryText
                        }
                        Repeater {
                            model: [
                                {
                                    action: "pause",
                                    icon: "player-pause",
                                    label: qsTr("暂停播放"),
                                    description: qsTr("保留当前歌曲与播放进度")
                                },
                                {
                                    action: "quit",
                                    icon: "logout",
                                    label: qsTr("退出应用"),
                                    description: qsTr("停止音乐并退出应用")
                                },
                                {
                                    action: "shutdown",
                                    icon: "power",
                                    label: qsTr("关闭电脑"),
                                    description: qsTr("停止音乐并关闭电脑")
                                }
                            ]
                            delegate: Button {
                                id: actionButton
                                required property var modelData
                                objectName: "sleepAction-" + modelData.action
                                readonly property bool selected: popup.selectedAction === modelData.action
                                Layout.fillWidth: true
                                implicitHeight: 58
                                leftPadding: 12
                                rightPadding: 12
                                hoverEnabled: true
                                enabled: !popup.timer.executing
                                onClicked: {
                                    popup.selectedAction = modelData.action;
                                    if (popup.timer.active)
                                        popup.timer.setAction(modelData.action);
                                }
                                // Keep the 18px icon, 10px gap and text left-aligned.
                                // The optional trailing check must not move the text.
                                contentItem: Item {
                                    EchoIcon {
                                        anchors.left: parent.left
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: 18
                                        height: 18
                                        name: actionButton.modelData.icon
                                        color: actionButton.selected ? Theme.floating.accentText : Theme.floating.text
                                    }
                                    ColumnLayout {
                                        objectName: "sleepActionContent"
                                        anchors.left: parent.left
                                        anchors.leftMargin: 28
                                        anchors.right: parent.right
                                        anchors.rightMargin: actionButton.selected ? 26 : 0
                                        anchors.verticalCenter: parent.verticalCenter
                                        spacing: 4
                                        Label {
                                            Layout.fillWidth: true
                                            text: actionButton.modelData.label
                                            font: Theme.font(12, Theme.semiboldWeight)
                                            color: actionButton.selected ? Theme.floating.accentText : Theme.floating.text
                                        }
                                        Label {
                                            Layout.fillWidth: true
                                            text: actionButton.modelData.description
                                            font: Theme.font(10, Theme.regularWeight)
                                            color: Theme.floating.secondaryText
                                        }
                                    }
                                    EchoIcon {
                                        visible: actionButton.selected
                                        anchors.right: parent.right
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: 16
                                        height: 16
                                        name: "check"
                                        color: Theme.floating.accentText
                                    }
                                }
                                background: Rectangle {
                                    radius: Theme.controlRadius
                                    color: actionButton.selected ? Theme.withAlpha(Theme.accent, .09) : actionButton.hovered ? Theme.floating.hover : "transparent"
                                    border.width: 1
                                    border.color: actionButton.selected ? Theme.mix(Theme.elevated, Theme.accent, .65) : popup.choiceBorder
                                }
                            }
                        }
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.topMargin: 6
                            implicitHeight: 1
                            color: Theme.withAlpha(Theme.floating.text, Theme.dark ? .22 : .16)
                        }
                        RowLayout {
                            Layout.fillHeight: false
                            Layout.fillWidth: true
                            Layout.topMargin: 6
                            Layout.bottomMargin: 14
                            spacing: 10
                            ColumnLayout {
                                Layout.fillHeight: false
                                Layout.fillWidth: true
                                spacing: 4
                                Label {
                                    text: qsTr("播完整首歌再执行")
                                    font: Theme.font(12, Theme.semiboldWeight)
                                    color: Theme.floating.text
                                }
                                Label {
                                    text: qsTr("到时不打断正在播放的歌曲")
                                    font: Theme.font(10, Theme.regularWeight)
                                    color: Theme.floating.secondaryText
                                }
                            }
                            EchoSwitch {
                                colors: Theme.floating
                                checked: popup.timer.finishTrack
                                enabled: !popup.timer.executing
                                onToggled: popup.timer.finishTrack = checked
                                Accessible.name: qsTr("播完整首歌再执行")
                            }
                        }
                    }
                }
            }
            Label {
                Layout.fillWidth: true
                Layout.topMargin: 12
                visible: text.length > 0
                text: popup.timer.errorMessage
                color: Theme.floating.dangerText
                font: Theme.font(11, Theme.regularWeight)
                wrapMode: Text.Wrap
            }
            Rectangle {
                Layout.fillWidth: true
                Layout.topMargin: 16
                implicitHeight: 1
                color: Theme.withAlpha(Theme.floating.text, Theme.dark ? .22 : .16)
            }
            RowLayout {
                Layout.fillHeight: false
                Layout.fillWidth: true
                Layout.topMargin: 14
                spacing: 8
                Label {
                    Layout.fillWidth: true
                    text: popup.timer.executing ? qsTr("正在请求%1…").arg(popup.actionLabel) : popup.timer.active ? qsTr("当前定时：%1").arg(popup.actionLabel) : qsTr("设置好后，安心听歌")
                    font: Theme.font(11, Theme.regularWeight)
                    color: Theme.floating.secondaryText
                }
                EchoSoftButton {
                    colors: Theme.floating
                    visible: popup.timer.active
                    text: qsTr("取消定时")
                    enabled: !popup.timer.executing
                    onClicked: popup.timer.cancel()
                }
                EchoSoftButton {
                    colors: Theme.floating
                    text: popup.timer.active ? qsTr("重新计时") : qsTr("开启定时")
                    accent: true
                    enabled: !popup.timer.executing
                    onClicked: popup.timer.start(popup.selectedMinutes, popup.selectedAction)
                }
            }
        }
    }
}
