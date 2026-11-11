pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Templates as T
import QtQuick.Layouts
import EchoMusic.Controls
import EchoMusic.Theme
import EchoMusic.Playback

Rectangle {
    id: group
    required property CastService output
    required property string heading
    required property var deviceModel
    property string pinTarget: ""
    property real maximumListHeight: 320
    property bool compact: false
    property T.Popup popupSurface: null
    signal choose(string id, bool needsPin)
    signal submitPin(string value)
    implicitHeight: content.implicitHeight + (compact ? 0 : 20)
    color: compact ? "transparent" : Theme.withAlpha(Theme.floating.tone, 0.032)
    border.color: compact ? "transparent" : Theme.floating.controlBorder
    radius: Theme.cardRadius
    ColumnLayout {
        id: content
        x: group.compact ? 0 : 10
        y: group.compact ? 0 : 10
        width: parent.width - x * 2
        spacing: group.compact ? 5 : 8
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: false
            EchoTextTagRow {
                text: group.heading
                font: Theme.font(13, 800)
                color: Theme.floatingText
            }
            Item {
                Layout.fillWidth: true
            }
            Label {
                text: qsTr("%1 台").arg(devices.count)
                font: Theme.font(group.compact ? 11 : 12, Theme.boldWeight)
                color: Theme.floatingSecondaryText
            }
        }
        EchoBoundedListView {
            id: devices
            maximumViewportHeight: group.maximumListHeight
            objectName: "settingsCastDevices" + group.heading
            Layout.fillWidth: true
            minimumViewportHeight: 0
            emptyViewportHeight: emptyState.implicitHeight + (group.compact ? 10 : 18)
            Layout.preferredHeight: implicitHeight
            model: group.deviceModel
            clip: true
            reuseItems: true
            spacing: group.compact ? 5 : 8
            boundsBehavior: Flickable.StopAtBounds
            delegate: EchoActionButton {
                id: device
                required property string targetId
                required property string deviceTitle
                required property string deviceDetail
                required property bool needsPin
                required property bool deviceAvailable
                required property bool deviceActive
                required property bool deviceConnecting
                width: ListView.view.width
                readonly property bool expanded: group.compact && group.pinTarget === targetId && needsPin
                height: expanded ? 89 : group.compact ? 50 : 54
                leftPadding: group.compact ? deviceActive ? 14 : 9 : deviceActive ? 16 : 10
                rightPadding: group.compact ? 9 : 10
                topPadding: 8
                bottomPadding: 8
                enabled: group.output.enabled && deviceAvailable && !group.output.busy
                hoverEnabled: true
                opacity: deviceAvailable ? 1 : 0.5
                Accessible.name: deviceTitle
                Accessible.description: deviceDetail
                onClicked: group.choose(targetId, needsPin)
                background: Rectangle {
                    radius: Theme.controlRadius
                    color: group.compact && device.deviceActive ? Theme.withAlpha(Theme.floating.accent, 0.06) : device.hovered || device.deviceActive || device.deviceConnecting || device.expanded ? Theme.floating.hover : group.compact ? "transparent" : Theme.floating.elevated
                    border.color: device.hovered || device.deviceActive || device.deviceConnecting || device.expanded ? Theme.withAlpha(Theme.floating.accent, group.compact ? 0.20 : 0.24) : "transparent"
                    Rectangle {
                        visible: device.deviceActive
                        x: group.compact ? 4 : 5
                        y: group.compact ? 8 : 10
                        width: 3
                        height: parent.height - y * 2
                        radius: 1.5
                        color: Theme.floating.accent
                    }
                }
                contentItem: ColumnLayout {
                    spacing: 3
                    EchoTextTagRow {
                        Layout.fillWidth: true
                        text: device.deviceTitle
                        font: Theme.font(12, 650)
                        color: Theme.floatingText
                        EchoTag {
                            visible: device.needsPin || device.deviceConnecting
                            text: device.deviceConnecting ? qsTr("连接中") : qsTr("需验证")
                            colors: Theme.floating
                            tone: EchoTag.Muted
                        }
                    }
                    Label {
                        Layout.fillWidth: true
                        visible: device.deviceDetail.length > 0
                        text: device.deviceDetail
                        textFormat: Text.PlainText
                        font: Theme.font(11, Theme.regularWeight)
                        color: Theme.floatingSecondaryText
                        elide: Text.ElideRight
                    }
                    RowLayout {
                        visible: device.expanded
                        Layout.fillWidth: true
                        spacing: 6
                        EchoTextField {
                            id: pin
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            size: EchoControlSize.Small
                            maximumLength: 32
                            echoMode: TextInput.Normal
                            colors: Theme.floating
                            placeholderText: qsTr("输入密码/验证码")
                            Accessible.name: placeholderText
                            onAccepted: group.submitPin(text)
                        }
                        EchoSoftButton {
                            text: qsTr("连接")
                            size: EchoControlSize.Small
                            colors: Theme.floating
                            enabled: !group.output.busy
                            onClicked: group.submitPin(pin.text)
                        }
                    }
                }
                onExpandedChanged: pin.clear()
            }
            footer: Item {
                width: devices.width
                height: !group.compact && group.pinTarget.length ? 44 : 0
                visible: height > 0
                Connections {
                    target: group
                    function onPinTargetChanged() {
                        settingsPin.clear();
                    }
                }
                RowLayout {
                    anchors.fill: parent
                    anchors.topMargin: parent.visible ? 8 : 0
                    spacing: 8
                    EchoTextTagRow {
                        text: qsTr("密码/验证码")
                        font: Theme.font(12, Theme.strongWeight)
                        color: Theme.floatingSecondaryText
                    }
                    EchoTextField {
                        id: settingsPin
                        Layout.fillWidth: true
                        Layout.minimumWidth: 32
                        Layout.preferredWidth: 96
                        Layout.maximumWidth: 96
                        maximumLength: 32
                        colors: Theme.floating
                        Accessible.name: qsTr("密码/验证码")
                        onAccepted: group.submitPin(text)
                    }
                    EchoSoftButton {
                        text: qsTr("连接")
                        colors: Theme.floating
                        enabled: !group.output.busy
                        onClicked: group.submitPin(settingsPin.text)
                    }
                }
            }
            Label {
                id: emptyState
                anchors.centerIn: parent
                visible: devices.count === 0
                text: group.output.searching ? qsTr("正在搜索设备") : qsTr("没有找到设备")
                font: Theme.font(group.compact ? 11 : 12, group.compact ? Theme.semiboldWeight : Theme.strongWeight)
                lineHeightMode: Text.FixedHeight
                lineHeight: group.compact ? 16.5 : 18
                color: Theme.floatingSecondaryText
            }
            ScrollBar.vertical: EchoPopupScrollBar {
                surface: group.popupSurface
                view: devices
                resourceActive: group.visible
            }
        }
    }
}
