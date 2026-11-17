pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Account
import EchoMusic.Controls
import EchoMusic.Theme

EchoDialog {
    id: dialog
    objectName: "profileDeviceDialog"
    required property ProfileService profile
    preferredWidth: 480
    maximumHeight: 720
    windowHeightMargin: 140
    heading: qsTr("登录设备管理")
    onOpened: profile.activateDevices()
    onAboutToHide: profile.deactivateDevices()
    flushBody: true
    body: Item {
        implicitHeight: bodyLayout.implicitHeight
        ColumnLayout {
            id: bodyLayout
            anchors.fill: parent
            spacing: 16
            RowLayout {
                id: summary
                Layout.fillHeight: false
                spacing: 12
                Layout.fillWidth: true
                Layout.rightMargin: 24
                Label {
                    objectName: "profileDeviceSummary"
                    Layout.fillWidth: true
                    text: dialog.profile.devices.count > 0 ? qsTr("当前账号已登录 %1 台设备").arg(dialog.profile.devices.count) : dialog.profile.devicesLoading ? qsTr("正在同步登录设备") : dialog.profile.devicesError.length > 0 ? dialog.profile.devicesError : qsTr("暂无登录设备记录")
                    color: Theme.floatingText
                    font: Theme.font(13, Theme.boldWeight)
                }
                EchoIconButton {
                    id: refresh
                    objectName: "profileDeviceRefresh"
                    Layout.preferredWidth: 32
                    Layout.maximumWidth: 32
                    Layout.preferredHeight: 32
                    glyph: "refresh"
                    text: qsTr("刷新登录设备")
                    implicitWidth: 32
                    implicitHeight: 32
                    padding: 8.5
                    colors: Theme.floating
                    enabled: !dialog.profile.devicesLoading && !dialog.profile.removingDevice
                    contentItem: EchoIcon {
                        name: "refresh"
                        color: refresh.foreground
                        opacity: refresh.enabled ? 1 : refresh.disabledOpacity
                        RotationAnimator on rotation {
                            from: 0
                            to: 360
                            duration: 1000
                            loops: Animation.Infinite
                            running: dialog.visible && dialog.profile.devicesLoading && Theme.store.motionEnabled && refresh.Window.window !== null && refresh.Window.window.visible && refresh.Window.window.visibility !== Window.Minimized
                        }
                    }
                    onClicked: dialog.profile.loadDevices(true)
                }
            }
            Label {
                id: errorLabel
                visible: dialog.profile.devicesError.length > 0
                Layout.fillWidth: true
                Layout.rightMargin: 24
                text: dialog.profile.devicesError
                color: "#ef4444"
                font: Theme.font(12, Theme.boldWeight)
            }
            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true
                implicitHeight: list.implicitHeight
                EchoBoundedListView {
                    id: list
                    ScrollBar.vertical: EchoPopupScrollBar {
                        surface: dialog
                        view: list
                        resourceActive: dialog.visible
                    }
                    objectName: "profileDeviceList"
                    anchors.fill: parent
                    maximumViewportHeight: Math.max(0, dialog.bodyHeightLimit - summary.implicitHeight - 16 - (errorLabel.visible ? errorLabel.implicitHeight + 16 : 0))
                    emptyViewportHeight: 99.5
                    clip: true
                    model: dialog.visible ? dialog.profile.devices : null
                    reuseItems: dialog.visible
                    spacing: 8
                    cacheBuffer: dialog.visible ? 152 : 0
                    boundsBehavior: Flickable.StopAtBounds
                    delegate: DeviceSessionRow {
                        width: list.width - 24
                        removalBusy: dialog.profile.removingDevice
                        removing: removalBusy && deviceKey === dialog.profile.removingDeviceKey
                        onRemovalRequested: key => dialog.profile.requestDeviceRemoval(key)
                    }
                }
                Label {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.rightMargin: 24
                    anchors.verticalCenter: parent.verticalCenter
                    horizontalAlignment: Text.AlignHCenter
                    visible: dialog.profile.devices.count === 0
                    text: dialog.profile.devicesLoading ? qsTr("正在获取登录设备") : qsTr("暂无登录设备记录")
                    color: Theme.floatingSecondaryText
                    font: Theme.font(13, Theme.boldWeight)
                }
            }
        }
    }
    EchoPopupLoader {
        id: cancelRemovalOwner
        objectName: "profileDeviceConfirmationOwner"
        hostItem: dialog.contentItem
        requestedVisible: dialog.visible && dialog.profile.pendingDeviceRemoval
        sourceComponent: Component {
            EchoDialog {
                objectName: "profileDeviceConfirmation"
                uiScale: dialog.uiScale
                heading: qsTr("移除登录设备")
                description: qsTr("移除“%1”后，该设备需要重新登录。").arg(dialog.profile.pendingDeviceTitle)
                closable: !dialog.profile.removingDevice
                managedDismiss: true
                closePolicy: Popup.NoAutoClose
                onDismissRequested: dialog.profile.cancelDeviceRemoval()
                footerContent: RowLayout {
                    spacing: 8
                    Item {
                        Layout.fillWidth: true
                    }
                    EchoSoftButton {
                        id: cancelRemoval
                        text: qsTr("取消")
                        colors: Theme.floating
                        background: EchoActionBackground {
                            colors: Theme.floating
                            color: "transparent"
                            EchoDashedFrame {
                                anchors.fill: parent
                                solid: true
                                strokeColor: cancelRemoval.visualFocus ? Theme.accent : Theme.floatingBorder
                                radius: Theme.controlRadius
                            }
                            highlighted: cancelRemoval.enabled && cancelRemoval.hovered
                            pressed: cancelRemoval.down
                        }
                        enabled: !dialog.profile.removingDevice
                        onClicked: dialog.profile.cancelDeviceRemoval()
                    }
                    EchoSoftButton {
                        objectName: "profileDeviceConfirmRemove"
                        text: qsTr("确认移除")
                        colors: Theme.floating
                        danger: true
                        enabled: !dialog.profile.removingDevice
                        contentItem: EchoButtonContent {
                            text: qsTr("确认移除")
                            font: Theme.font(12, Theme.semiboldWeight)
                            color: Theme.floating.dangerText
                            loading: dialog.profile.removingDevice
                        }
                        onClicked: dialog.profile.confirmDeviceRemoval()
                    }
                }
            }
        }
    }
}
