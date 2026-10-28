pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Account
import EchoMusic.Controls
import EchoMusic.Theme

FocusScope {
    id: root
    objectName: "verificationOverlay"
    required property VerificationService service
    property real uiScale: 1
    readonly property real actionHeight: 56
    focus: service.open
    // Verification blocks Escape and outside dismissal. Consume keys so they
    // cannot reach the suspended login page.
    Keys.onEscapePressed: event => event.accepted = true
    Component.onCompleted: forceActiveFocus()
    readonly property bool waitingForBrowser: service.browserOpen && service.state !== VerificationService.Error
    readonly property string provider: service.provider
    readonly property color iconColor: provider === "sms" ? "#07c160" : provider === "bind_phone" ? "#f59e0b" : provider === "real_name" ? "#8b5cf6" : provider === "login" ? Theme.floatingText : Theme.floatingAccentText
    readonly property color iconBackground: provider === "login" ? Theme.withAlpha(Theme.floatingText, 0.10) : Theme.withAlpha(provider === "sms" ? "#07c160" : provider === "bind_phone" ? "#f59e0b" : provider === "real_name" ? "#8b5cf6" : Theme.accent, 0.12)
    readonly property string title: {
        switch (provider) {
        case "sms":
            return qsTr("短信安全验证");
        case "login":
            return qsTr("登录确认");
        case "bind_phone":
            return qsTr("需要绑定手机号");
        case "real_name":
            return qsTr("需要实名认证");
        case "account_risk":
            return qsTr("账号被风控");
        default:
            return qsTr("安全验证");
        }
    }
    readonly property string description: {
        if (service.state === VerificationService.Loading)
            return qsTr("正在准备验证");
        if (service.state === VerificationService.Verifying)
            return qsTr("正在处理验证结果，稍后继续刚才的操作");
        if (!provider.length)
            return qsTr("尚未取得验证码，请重新获取验证信息");
        switch (provider) {
        case "sms":
            return qsTr("请输入酷狗下发的验证码");
        case "login":
            return service.displayMessage || qsTr("请登录账号以确认身份");
        case "bind_phone":
            return service.displayMessage || qsTr("当前账号需绑定手机号后才能继续操作，请前往酷狗客户端完成绑定后重试");
        case "real_name":
            return qsTr("当前账号需要完成实名认证，请前往酷狗客户端完成认证后重试");
        case "account_risk":
            return qsTr("需要在酷狗 APP 发起申诉");
        case "unsupported":
            return qsTr("当前需要%1，暂不支持自动处理").arg(service.providerName);
        default:
            return service.browserOpen ? qsTr("请在浏览器验证窗口中完成验证，完成后会自动回到这里") : qsTr("将在浏览器验证窗口中完成验证，然后自动回到应用");
        }
    }
    EchoPopupLoader {
        id: dialogOwner
        objectName: "verificationDialogOwner"
        hostItem: root
        requestedVisible: root.service.open
        sourceComponent: Component {
            EchoDialog {
                id: dialog
                objectName: "verificationDialog"
                verificationSurface: true
                uiScale: root.uiScale
                preferredWidth: 360
                heading: root.title
                description: root.description
                closable: false
                closePolicy: Popup.NoAutoClose
                body: ColumnLayout {
                    spacing: 16
                    // body is a Component; its ids cannot be resolved outside it.
                    Connections {
                        target: root.service
                        function onChanged() {
                            if (!root.service.open)
                                smsCode.clear();
                        }
                    }
                    Rectangle {
                        Layout.alignment: Qt.AlignHCenter
                        Layout.topMargin: 4
                        Layout.preferredWidth: 58
                        Layout.preferredHeight: 58
                        radius: 29
                        color: root.iconBackground
                        EchoIcon {
                            anchors.centerIn: parent
                            width: 26
                            height: 26
                            name: root.provider === "sms" || root.provider === "bind_phone" ? "device-mobile" : root.provider === "login" || root.provider === "real_name" ? "user" : "shield"
                            color: root.iconColor
                            visible: !root.waitingForBrowser && root.service.state !== VerificationService.Verifying
                        }
                        EchoSpinner {
                            anchors.centerIn: parent
                            visible: root.waitingForBrowser || root.service.state === VerificationService.Verifying
                            resourceActive: visible && root.service.open
                        }
                    }
                    Item {
                        visible: root.service.state === VerificationService.Loading
                        Layout.fillWidth: true
                        Layout.preferredHeight: 56
                        EchoSpinner {
                            anchors.centerIn: parent
                            resourceActive: root.service.open
                        }
                    }
                    EchoTextField {
                        id: smsCode
                        size: EchoControlSize.Large
                        visible: root.provider === "sms"
                        Layout.fillWidth: true
                        colors: Theme.floating
                        horizontalAlignment: Text.AlignHCenter
                        font: Theme.spacedFont(15, Theme.mediumWeight, 4)
                        rightPadding: 24
                        placeholderText: qsTr("验证码")
                        inputMethodHints: Qt.ImhDigitsOnly
                        enabled: root.service.state !== VerificationService.Verifying
                        onAccepted: root.service.submitSms(text)
                    }
                    EchoButton {
                        size: EchoControlSize.Large
                        objectName: "verificationStartButton"
                        Layout.fillWidth: true
                        Layout.minimumHeight: root.actionHeight
                        Layout.preferredHeight: root.actionHeight
                        colors: Theme.floating
                        visible: root.service.state !== VerificationService.Loading && (root.provider === "tencent" || root.provider === "sms" || root.provider === "login" || root.provider.length === 0)
                        text: root.service.state === VerificationService.Verifying ? qsTr("正在验证…") : root.provider === "sms" ? qsTr("提交验证") : root.provider === "login" ? qsTr("去登录确认") : root.provider.length === 0 ? qsTr("重新获取验证信息") : (root.service.browserOpen ? qsTr("重新打开验证窗口") : root.service.errorMessage.length > 0 ? qsTr("重新验证") : qsTr("打开验证窗口"))
                        accent: !root.service.browserOpen
                        enabled: root.service.state !== VerificationService.Loading && root.service.state !== VerificationService.Verifying && (root.provider !== "sms" || smsCode.text.trim().length > 0)
                        onClicked: {
                            if (root.provider === "sms")
                                root.service.submitSms(smsCode.text);
                            else if (root.provider === "login")
                                root.service.beginLogin();
                            else if (!root.service.appId.length)
                                root.service.refresh();
                            else
                                root.service.beginBrowserVerification();
                        }
                    }
                    Label {
                        visible: root.provider === "bind_phone" || root.provider === "real_name" || root.provider === "account_risk"
                        Layout.fillWidth: true
                        text: root.provider === "real_name" ? qsTr("请前往酷狗客户端完成实名认证，完成后返回此处重试刚才的操作") : root.provider === "account_risk" ? qsTr("当前账号被酷狗风控限制，请在酷狗客户端完成申诉流程") : root.description
                        color: Theme.floatingSecondaryText
                        font: Theme.font(13, Theme.semiboldWeight)
                        lineHeightMode: Text.FixedHeight
                        lineHeight: 20.8
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.Wrap
                    }
                }
                // User-requested exception: the error follows Cancel, retaining the
                // reference typography/colors rather than splitting the two actions.
                footerContent: ColumnLayout {
                    spacing: 16
                    EchoButton {
                        size: EchoControlSize.Large
                        objectName: "verificationCancelButton"
                        Layout.fillWidth: true
                        text: qsTr("取消")
                        accent: false
                        colors: Theme.floating
                        enabled: root.service.state !== VerificationService.Verifying
                        onClicked: root.service.cancel()
                    }
                    Label {
                        objectName: "verificationErrorLabel"
                        Layout.fillWidth: true
                        visible: root.service.errorMessage.length > 0 || root.provider === "unsupported"
                        text: root.service.errorMessage || qsTr("验证类型：%1 / v_type=%2").arg(root.service.providerName).arg(root.service.verificationType)
                        textFormat: Text.PlainText
                        color: Theme.floating.dangerText
                        font: Theme.font(12, Theme.boldWeight)
                        lineHeightMode: Text.FixedHeight
                        lineHeight: 18
                        Layout.minimumHeight: 18
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.Wrap
                    }
                }
            }
        }
    }
}
