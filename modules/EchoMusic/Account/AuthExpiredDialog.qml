pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import EchoMusic.Controls
import EchoMusic.Theme
import EchoMusic.Account

EchoDialog {
    id: dialog
    objectName: "authExpiredDialog"
    required property AccountController service
    heading: qsTr("登录已过期")
    description: qsTr("您的登录信息已失效，为了您的账号安全，请重新登录。")
    onClosed: service.dismissSessionExpired()
    footerContent: RowLayout {
        spacing: 8
        Item {
            Layout.fillWidth: true
        }
        EchoSoftButton {
            objectName: "authExpiredLater"
            text: qsTr("稍后再说")
            colors: Theme.floating
            onClicked: dialog.close()
        }
        EchoButton {
            objectName: "authExpiredLogin"
            text: qsTr("去登录")
            colors: Theme.floating
            onClicked: dialog.service.openLogin()
        }
    }
}
