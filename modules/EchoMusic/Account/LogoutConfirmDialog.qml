pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import EchoMusic.Controls
import EchoMusic.Theme
import EchoMusic.Account

EchoDialog {
    id: dialog
    objectName: "logoutConfirmation"
    required property AccountController service
    heading: qsTr("退出登录")
    description: qsTr("确定要退出当前账号吗？")
    footerContent: RowLayout {
        spacing: 5
        Item {
            Layout.fillWidth: true
        }
        EchoSoftButton {
            text: qsTr("取消")
            background: EchoActionBackground {
                colors: Theme.floating
                color: "transparent"
                EchoDashedFrame {
                    anchors.fill: parent
                    solid: true
                    strokeColor: Theme.floatingBorder
                    radius: Theme.controlRadius
                }
            }
            colors: Theme.floating
            onClicked: dialog.close()
        }
        EchoSoftButton {
            text: qsTr("确认退出")
            danger: true
            colors: Theme.floating
            onClicked: {
                dialog.close();
                dialog.service.savedAccounts.logout();
            }
        }
    }
}
