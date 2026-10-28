pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Controls
import EchoMusic.Theme
import EchoMusic.Account

Item {
    id: page
    objectName: "loginOverlay"
    required property AccountController service
    // The card must sample only the login background. Sampling the enclosing
    // shell also captures this card and recursively feeds its effect graph.
    EchoBackdropRoot {
        objectName: "loginBackdropScene"
        anchors.fill: parent
        samplingActive: page.visible
        Rectangle {
            anchors.fill: parent
            color: Theme.main
        }
    }
    focus: true
    Keys.onEscapePressed: service.closeLogin()
    MouseArea {
        anchors.fill: parent
    } // Fullscreen overlay blocks shell inputs underneath.
    EchoIconButton {
        objectName: "loginBackButton"
        x: 24
        y: 48
        width: 40
        height: 40
        glyph: "chevron-left"
        text: qsTr("返回")
        onClicked: page.service.closeLogin()
    }
    ColumnLayout {
        anchors.centerIn: parent
        width: 420
        spacing: 16
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: banner.implicitHeight + 24
            radius: Theme.cardRadius
            color: "#1af59e0b"
            border.width: 1
            border.color: "#40f59e0b"
            RowLayout {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 10
                EchoIcon {
                    Layout.alignment: Qt.AlignTop
                    Layout.preferredWidth: 20
                    Layout.preferredHeight: 20
                    name: "info-circle"
                    color: Theme.dark ? "#fcd34d" : "#b45309"
                }
                Label {
                    id: banner
                    Layout.fillWidth: true
                    text: qsTr("首次使用 EchoMusic？请先在手机端下载《酷狗概念版》，完成账号注册并成功登录后，再返回这里登录。")
                    color: Theme.dark ? "#fcd34d" : "#b45309"
                    font: Theme.font(13, Theme.boldWeight)
                    wrapMode: Text.Wrap
                }
            }
        }
        Label {
            objectName: "accountReloginPrompt"
            visible: text.length > 0
            Layout.fillWidth: true
            text: page.service.loginPrompt
            color: Theme.text
            font: Theme.font(13, Theme.semiboldWeight)
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
        }
        Label {
            objectName: "loginAccountError"
            visible: text.length > 0 && (page.service.method === 0 || page.service.method === 3 || page.service.method === 4)
            Layout.fillWidth: true
            text: page.service.errorMessage
            textFormat: Text.PlainText
            color: Theme.colors.dangerText
            font: Theme.font(12, Theme.semiboldWeight)
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
        }
        EchoFloatingSurface {
            objectName: "loginCard"
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(510, page.height - 180)
            radius: Theme.popoverRadius
            color: Theme.elevated
            Item {
                anchors.fill: parent
                anchors.margins: 1 // CSS card border is outside its content box.
                Item {
                    id: loginContent
                    objectName: "loginContent"
                    anchors.top: parent.top
                    anchors.bottom: loginMethods.top
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.topMargin: 32
                    anchors.leftMargin: 40
                    anchors.rightMargin: 40
                    LoginQrPanel {
                        anchors.centerIn: parent
                        width: parent.width
                        visible: page.service.method === 0
                        service: page.service.qr
                    }
                    LoginSmsPanel {
                        anchors.centerIn: parent
                        width: parent.width
                        visible: page.service.method === 1
                        service: page.service
                    }
                    LoginQrPanel {
                        anchors.centerIn: parent
                        width: parent.width
                        visible: page.service.method === 3
                        service: page.service.qq
                        provider: 3
                    }
                    LoginQrPanel {
                        anchors.centerIn: parent
                        width: parent.width
                        visible: page.service.method === 4
                        service: page.service.wechat
                        provider: 4
                    }
                    ColumnLayout {
                        anchors.centerIn: parent
                        width: parent.width
                        visible: page.service.method === 2
                        spacing: 0
                        Label {
                            Layout.alignment: Qt.AlignHCenter
                            Layout.bottomMargin: 4
                            text: qsTr("账号登录")
                            color: Theme.text
                            font: Theme.font(26, Theme.blackWeight)
                        }
                        Label {
                            Layout.alignment: Qt.AlignHCenter
                            Layout.bottomMargin: 16
                            text: qsTr("可能需要安全验证")
                            color: Theme.secondaryText
                            font: Theme.spacedFont(13, Theme.boldWeight, 1.5)
                        }
                        EchoTextField {
                            id: username
                            size: EchoControlSize.Large
                            objectName: "loginUsername"
                            Layout.fillWidth: true
                            Layout.bottomMargin: 16
                            placeholderText: qsTr("用户名")
                            clearable: true
                        }
                        EchoTextField {
                            id: password
                            size: EchoControlSize.Large
                            objectName: "loginPassword"
                            Layout.fillWidth: true
                            Layout.bottomMargin: 4
                            clearable: true
                            echoMode: TextInput.Password
                            placeholderText: qsTr("密码")
                            onAccepted: page.service.submitPassword(username.text, password.text)
                        }
                        Label {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 20
                            Layout.bottomMargin: 4
                            text: page.service.errorMessage
                            textFormat: Text.PlainText
                            color: Theme.colors.dangerText
                            font: Theme.font(11, Theme.boldWeight)
                            wrapMode: Text.Wrap
                        }
                        EchoButton {
                            size: EchoControlSize.Large
                            objectName: "loginSubmit"
                            Layout.fillWidth: true
                            text: page.service.busy ? qsTr("登录中…") : qsTr("立即登录")
                            enabled: !page.service.busy
                            onClicked: page.service.submitPassword(username.text, password.text)
                        }
                    }
                }
                Item {
                    id: loginMethods
                    objectName: "loginMethodsFooter"
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.leftMargin: 32
                    anchors.rightMargin: 32
                    anchors.bottomMargin: 28
                    // Reserve the caption, spacing and large form action in the panel height.
                    height: 1 + 20 + 16.5 + 12 + 56
                    Rectangle {
                        anchors.top: parent.top
                        width: parent.width
                        height: 1
                        color: Theme.subtleBorder
                    }
                    Label {
                        objectName: "loginMethodsTitle"
                        anchors.top: parent.top
                        anchors.topMargin: 21
                        anchors.horizontalCenter: parent.horizontalCenter
                        height: 16.5
                        text: qsTr("选择登录方式")
                        color: Theme.secondaryText
                        font: Theme.spacedFont(11, Theme.blackWeight, 3)
                    }
                    RowLayout {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: 56
                        spacing: 8
                        Repeater {
                            // Display order is independent of the service's stable method IDs.
                            model: [
                                {
                                    method: 0,
                                    label: "酷狗",
                                    glyph: "qrcode"
                                },
                                {
                                    method: 4,
                                    label: "微信",
                                    glyph: "brand-wechat"
                                },
                                {
                                    method: 3,
                                    label: "QQ",
                                    glyph: "brand-qq"
                                },
                                {
                                    method: 1,
                                    label: "验证码",
                                    glyph: "device-mobile"
                                },
                                {
                                    method: 2,
                                    label: "账号",
                                    glyph: "user"
                                }
                            ]
                            delegate: Button {
                                id: methodButton
                                objectName: "loginMethod-" + method
                                required property var modelData
                                readonly property int method: modelData.method
                                readonly property bool selected: page.service.method === methodButton.method
                                Layout.fillWidth: true
                                Layout.preferredHeight: 56
                                padding: 0
                                hoverEnabled: true
                                opacity: enabled ? 1 : 0.6
                                Accessible.name: modelData.label
                                onClicked: page.service.method = methodButton.method
                                background: Rectangle {
                                    radius: Theme.cardRadius
                                    color: methodButton.selected ? Theme.withAlpha(Theme.accent, 0.11) : methodButton.hovered ? Theme.withAlpha(Theme.accent, 0.07) : "transparent"
                                    border.width: 1
                                    border.color: methodButton.selected ? Theme.withAlpha(Theme.accent, 0.28) : "transparent"
                                }
                                contentItem: Item {
                                    // Button stretches its contentItem to 56px. Center
                                    // the intrinsic group so spare height cannot be
                                    // distributed between the icon and caption.
                                    Column {
                                        anchors.centerIn: parent
                                        spacing: 4 // 4px gap between the 20px icon and caption.
                                        EchoIcon {
                                            objectName: "loginMethodIcon-" + methodButton.method
                                            anchors.horizontalCenter: parent.horizontalCenter
                                            width: 20
                                            height: 20
                                            name: methodButton.modelData.glyph
                                            color: methodButton.selected ? Theme.accentText : Theme.secondaryText
                                        }
                                        Label {
                                            objectName: "loginMethodCaption-" + methodButton.method
                                            anchors.horizontalCenter: parent.horizontalCenter
                                            height: 10
                                            topPadding: 0
                                            bottomPadding: 0
                                            verticalAlignment: Text.AlignVCenter
                                            text: methodButton.modelData.label
                                            color: methodButton.selected ? Theme.accentText : Theme.secondaryText
                                            font: Theme.font(10, Theme.blackWeight)
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
