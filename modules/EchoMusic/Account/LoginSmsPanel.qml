pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Account
import EchoMusic.Controls
import EchoMusic.Theme

ColumnLayout {
    id: panel
    objectName: "loginSmsPanel"
    required property AccountController service
    spacing: 0
    Label {
        Layout.alignment: Qt.AlignHCenter
        Layout.bottomMargin: 4
        text: qsTr("验证码登录")
        color: Theme.text
        font: Theme.font(26, Theme.blackWeight)
    }
    Label {
        Layout.alignment: Qt.AlignHCenter
        Layout.bottomMargin: 16
        text: qsTr("无需密码，快捷安全")
        color: Theme.secondaryText
        font: Theme.spacedFont(13, Theme.boldWeight, 1.5)
    }
    ColumnLayout {
        Layout.fillWidth: true
        spacing: 0
        visible: panel.service.smsCandidates.count === 0
        EchoTextField {
            id: mobile
            size: EchoControlSize.Large
            objectName: "loginMobile"
            Layout.fillWidth: true
            Layout.bottomMargin: 16
            placeholderText: qsTr("手机号码")
            inputMethodHints: Qt.ImhDialableCharactersOnly
            clearable: true
        }
        RowLayout {
            Layout.fillHeight: false
            Layout.fillWidth: true
            Layout.bottomMargin: 4
            spacing: 12
            EchoTextField {
                id: code
                size: EchoControlSize.Large
                objectName: "loginSmsCode"
                Layout.fillWidth: true
                placeholderText: qsTr("验证码")
                inputMethodHints: Qt.ImhDigitsOnly
                clearable: true
                onAccepted: panel.service.submitSms(mobile.text, code.text)
            }
            EchoButton {
                size: EchoControlSize.Large
                objectName: "loginSendSms"
                accent: false
                text: panel.service.smsCountdown > 0 ? panel.service.smsCountdown + "s" : qsTr("获取验证码")
                enabled: !panel.service.busy && !panel.service.sendingSms && panel.service.smsCountdown === 0
                onClicked: panel.service.sendSms(mobile.text)
            }
        }
        Label {
            Layout.fillWidth: true
            Layout.preferredHeight: 20
            Layout.bottomMargin: 4
            text: panel.service.errorMessage
            textFormat: Text.PlainText
            color: Theme.colors.dangerText
            font: Theme.font(11, Theme.boldWeight)
            wrapMode: Text.Wrap
        }
        EchoButton {
            size: EchoControlSize.Large
            objectName: "loginSmsSubmit"
            Layout.fillWidth: true
            text: panel.service.busy ? qsTr("登录中…") : qsTr("立即登录")
            enabled: !panel.service.busy && !panel.service.sendingSms
            onClicked: panel.service.submitSms(mobile.text, code.text)
        }
    }
    ColumnLayout {
        Layout.fillWidth: true
        visible: panel.service.smsCandidates.count > 0
        spacing: 0
        RowLayout {
            Layout.fillHeight: false
            objectName: "smsCandidateHeader"
            Layout.fillWidth: true
            Layout.bottomMargin: 12
            spacing: 12
            Label {
                Layout.fillWidth: true
                text: qsTr("选择账号")
                font: Theme.font(13, Theme.blackWeight)
                color: Theme.text
            }
            EchoSoftButton {
                objectName: "smsCandidatesBack"
                text: qsTr("返回")
                implicitHeight: 32
                font: Theme.font(12, Theme.semiboldWeight)
                enabled: !panel.service.busy
                onClicked: panel.service.resetSmsCandidates()
            }
        }
        ListView {
            id: candidates
            objectName: "smsCandidateList"
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(192, count * 72 - 8)
            Layout.bottomMargin: 12
            clip: true
            reuseItems: true
            EchoViewPoolGuard {
                targetView: candidates
            }
            cacheBuffer: 144
            spacing: 8
            boundsBehavior: Flickable.StopAtBounds
            model: panel.service.smsCandidates
            ScrollBar.vertical: EchoScrollBar {
                resourceActive: panel.visible
            }
            delegate: Button {
                id: candidate
                objectName: "smsCandidateRow-" + index
                required property int index
                required property double userId
                required property string nickname
                required property string avatarUrl
                required property string username
                property bool pooled: false
                ListView.onPooled: pooled = true
                ListView.onReused: pooled = false
                width: candidates.width
                height: 64
                padding: 12
                hoverEnabled: true
                enabled: !panel.service.busy
                onClicked: panel.service.selectSmsAccount(index)
                background: Rectangle {
                    radius: Theme.itemRadius
                    color: candidate.hovered ? Theme.hover : Theme.colors.mutedSurface
                    border.width: 1
                    border.color: candidate.hovered ? Theme.withAlpha(Theme.accent, 0.4) : Theme.subtleBorder
                }
                contentItem: RowLayout {
                    spacing: 12
                    AccountAvatar {
                        Layout.preferredWidth: 44
                        Layout.preferredHeight: 44
                        source: candidate.avatarUrl
                        resourceActive: panel.visible && !candidate.pooled
                    }
                    ColumnLayout {
                        Layout.fillHeight: false
                        Layout.fillWidth: true
                        spacing: 2
                        Label {
                            Layout.fillWidth: true
                            text: candidate.nickname
                            elide: Text.ElideRight
                            color: Theme.text
                            font: Theme.font(13, Theme.blackWeight)
                        }
                        Label {
                            Layout.fillWidth: true
                            text: candidate.username || String(candidate.userId)
                            elide: Text.ElideRight
                            color: Theme.secondaryText
                            font: Theme.font(11, Theme.boldWeight)
                        }
                    }
                    Label {
                        visible: panel.service.pendingSmsUser === candidate.userId
                        text: qsTr("登录中")
                        color: Theme.accentText
                        font: Theme.font(11, Theme.blackWeight)
                    }
                }
            }
        }
        Label {
            Layout.fillWidth: true
            Layout.preferredHeight: 20
            Layout.bottomMargin: 4
            text: panel.service.errorMessage
            textFormat: Text.PlainText
            color: Theme.colors.dangerText
            font: Theme.font(11, Theme.boldWeight)
            wrapMode: Text.Wrap
        }
    }
}
