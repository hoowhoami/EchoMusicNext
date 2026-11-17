pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Controls
import EchoMusic.Theme
import EchoMusic.Account

EchoAnchoredPopup {
    id: panel
    objectName: "sidebarAccountPopover"
    required property AccountController service
    signal switchRequested
    signal logoutRequested
    signal profileRequested
    modal: false
    preferredWidth: 320
    maximumHeight: 620
    sideOffset: 14
    padding: 18
    rightPadding: 0
    implicitHeight: content.contentHeight + topPadding + bottomPadding
    onOpened: service.openAccountPanel()
    contentItem: Flickable {
        id: content
        contentWidth: width
        contentHeight: body.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: EchoPopupScrollBar {
            surface: panel
            view: content
            resourceActive: panel.visible
        }
        ColumnLayout {
            id: body
            width: content.width - 18
            spacing: 0
            ColumnLayout {
                visible: !panel.service.account.userId
                Layout.fillWidth: true
                spacing: 14
                Layout.topMargin: 12
                Layout.bottomMargin: 12
                AccountAvatar {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredWidth: 48
                    Layout.preferredHeight: 48
                    resourceActive: panel.visible
                }
                Label {
                    Layout.alignment: Qt.AlignHCenter
                    text: qsTr("登录 EchoMusic")
                    color: Theme.floatingText
                    font: Theme.font(14, Theme.semiboldWeight)
                }
                Label {
                    Layout.fillWidth: true
                    text: qsTr("查看个人资料，同步你的音乐收藏")
                    color: Theme.floatingSecondaryText
                    font: Theme.font(12, Theme.regularWeight)
                    horizontalAlignment: Text.AlignHCenter
                }
                EchoButton {
                    objectName: "accountLoginAction"
                    Layout.alignment: Qt.AlignHCenter
                    // User-requested compact popover action; keep the login
                    // form's 56px default unchanged.
                    size: EchoControlSize.Compact
                    colors: Theme.floating
                    text: qsTr("登录账号")
                    onClicked: {
                        panel.close();
                        panel.service.openLogin();
                    }
                }
                EchoButton {
                    id: signedOutSwitch
                    objectName: "accountSignedOutSwitchAction"
                    visible: panel.service.savedAccounts.count > 0
                    Layout.alignment: Qt.AlignHCenter
                    size: EchoControlSize.Compact
                    accent: false
                    text: qsTr("切换账号")
                    colors: Theme.floating
                    background: Rectangle {
                        radius: Theme.controlRadius
                        color: signedOutSwitch.hovered ? signedOutSwitch.colors.hover : "transparent"
                        EchoDashedFrame {
                            anchors.fill: parent
                            solid: true
                            strokeColor: Theme.accent
                            radius: Theme.controlRadius
                            thickness: 2
                            visible: signedOutSwitch.visualFocus
                        }
                    }
                    onClicked: {
                        panel.close();
                        panel.switchRequested();
                    }
                }
            }
            ColumnLayout {
                visible: panel.service.account.userId > 0
                Layout.fillWidth: true
                spacing: 0
                RowLayout {
                    Layout.fillHeight: false
                    Layout.fillWidth: true
                    spacing: 12
                    AccountAvatar {
                        source: panel.service.account.avatarUrl
                        objectName: "accountPopoverAvatar"
                        identityIcon: panel.service.account.avatarIdentityIcon
                        identityLabel: panel.service.account.avatarIdentityLabel
                        resourceActive: panel.visible
                        Layout.preferredWidth: 44
                        Layout.preferredHeight: 44
                    }
                    ColumnLayout {
                        id: accountName
                        Layout.fillHeight: false
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        spacing: 4
                        Row {
                            Layout.fillWidth: true
                            spacing: 8
                            Label {
                                id: nicknameLabel
                                width: Math.max(0, Math.min(implicitWidth, accountName.width - levelLabel.implicitWidth - 8))
                                text: panel.service.account.nickname
                                color: Theme.floatingText
                                font: Theme.font(16, Theme.semiboldWeight)
                                lineHeightMode: Text.FixedHeight
                                lineHeight: 22.4
                                wrapMode: Text.WrapAnywhere
                            }
                            Label {
                                id: levelLabel
                                y: nicknameLabel.baselineOffset - baselineOffset
                                text: "Lv." + panel.service.account.grade
                                color: Theme.floatingSecondaryText
                                font: Theme.font(11, Theme.mediumWeight)
                            }
                        }
                        Label {
                            text: "ID · " + panel.service.account.userId
                            color: Theme.floatingSecondaryText
                            font: Theme.font(12, Theme.regularWeight)
                            lineHeightMode: Text.FixedHeight
                            lineHeight: 18
                        }
                    }
                }
                Label {
                    Layout.topMargin: visible ? 12 : 0
                    visible: text.length > 0
                    Layout.fillWidth: true
                    text: panel.service.account.biography
                    lineHeightMode: Text.FixedHeight
                    lineHeight: 19.2
                    maximumLineCount: 2
                    elide: Text.ElideRight
                    wrapMode: Text.WrapAnywhere
                    color: Theme.floatingSecondaryText
                    font: Theme.font(12, Theme.regularWeight)
                }
                Rectangle {
                    visible: panel.service.account.statistics.length > 0
                    Layout.fillWidth: true
                    objectName: "accountStatistics"
                    Layout.topMargin: 18
                    implicitHeight: 76.5
                    color: "transparent"
                    EchoDivider {
                        width: parent.width
                        lineColor: Theme.floating.subtleBorder
                    }
                    EchoDivider {
                        width: parent.width
                        anchors.bottom: parent.bottom
                        lineColor: Theme.floating.subtleBorder
                    }
                    RowLayout {
                        anchors.fill: parent
                        anchors.topMargin: 14
                        anchors.bottomMargin: 14
                        spacing: 0
                        Repeater {
                            model: panel.service.account.statistics
                            delegate: Item {
                                id: stat
                                required property int index
                                required property var modelData
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                EchoDivider {
                                    visible: stat.index > 0
                                    anchors.left: parent.left
                                    height: parent.height
                                    orientation: Qt.Vertical
                                    lineColor: Theme.floating.subtleBorder
                                }
                                ColumnLayout {
                                    anchors.centerIn: parent
                                    spacing: 4
                                    EchoRollingNumber {
                                        Layout.alignment: Qt.AlignHCenter
                                        text: Number(stat.modelData.value).toLocaleString(Qt.locale("zh_CN"), "f", 0)
                                        resourceActive: panel.visible
                                        color: Theme.floatingText
                                        font: Theme.font(20, Theme.semiboldWeight)
                                        lineHeightMode: Text.FixedHeight
                                        lineHeight: 26
                                    }
                                    Label {
                                        Layout.alignment: Qt.AlignHCenter
                                        text: stat.modelData.label
                                        color: Theme.floatingSecondaryText
                                        font: Theme.font(11, Theme.regularWeight)
                                        lineHeightMode: Text.FixedHeight
                                        lineHeight: 16.5
                                    }
                                }
                            }
                        }
                    }
                }
                RowLayout {
                    Layout.fillHeight: false
                    spacing: 5
                    // CSS sibling margins collapse: stats bottom=18 and this
                    // row top=16 produce 18, rather than an accumulated 34.
                    Layout.topMargin: visible ? panel.service.account.statistics.length > 0 ? 18 : 16 : 0
                    visible: panel.service.account.listeningDuration.length > 0
                    Layout.fillWidth: true
                    Label {
                        text: qsTr("累计听歌")
                        color: Theme.floatingSecondaryText
                        font: Theme.font(12, Theme.regularWeight)
                    }
                    Item {
                        Layout.fillWidth: true
                    }
                    Label {
                        text: panel.service.account.listeningDuration
                        color: Theme.floatingText
                        font: Theme.font(12, Theme.regularWeight)
                    }
                }
                Repeater {
                    model: panel.service.account.memberships
                    delegate: RowLayout {
                        id: membership
                        required property int index
                        required property var modelData
                        Layout.fillWidth: true
                        Layout.topMargin: index === 0 ? 18 : 12
                        spacing: 10
                        Rectangle {
                            Layout.preferredWidth: 6
                            Layout.preferredHeight: 6
                            radius: 3
                            color: Theme.accent
                        }
                        RowLayout {
                            Layout.fillHeight: false
                            Layout.fillWidth: true
                            spacing: 12
                            Label {
                                text: membership.modelData.label
                                color: Theme.floatingText
                                font: Theme.font(12, Theme.mediumWeight)
                            }
                            Item {
                                Layout.fillWidth: true
                            }
                            Label {
                                text: membership.modelData.expires
                                color: Theme.floatingSecondaryText
                                font: Theme.font(11, Theme.regularWeight)
                            }
                        }
                    }
                }
                RowLayout {
                    Layout.fillHeight: false
                    Layout.fillWidth: true
                    Layout.topMargin: 20
                    spacing: 10
                    EchoSoftButton {
                        id: profileAction
                        objectName: "accountProfileAction"
                        Layout.fillWidth: true
                        Layout.preferredWidth: 0
                        implicitHeight: 34
                        leftPadding: 10
                        rightPadding: 10
                        font: Theme.font(12, Theme.mediumWeight)
                        text: qsTr("个人主页")
                        contentItem: EchoButtonContent {
                            text: qsTr("个人主页")
                            glyph: "chevron-right"
                            glyphAfterText: true
                            glyphSize: 14
                            spacing: 5
                            font: profileAction.font
                            color: Theme.floatingText
                        }
                        colors: Theme.floating
                        onClicked: {
                            panel.close();
                            panel.profileRequested();
                        }
                    }
                    EchoSoftButton {
                        Layout.fillWidth: true
                        Layout.preferredWidth: 0
                        implicitHeight: 34
                        leftPadding: 10
                        rightPadding: 10
                        font: Theme.font(12, Theme.mediumWeight)
                        objectName: "accountSwitchAction"
                        text: qsTr("切换账号")
                        colors: Theme.floating
                        onClicked: {
                            panel.close();
                            panel.switchRequested();
                        }
                    }
                    EchoSoftButton {
                        Layout.fillWidth: true
                        Layout.preferredWidth: 0
                        implicitHeight: 34
                        leftPadding: 10
                        rightPadding: 10
                        font: Theme.font(12, Theme.mediumWeight)
                        objectName: "accountLogoutAction"
                        text: qsTr("退出登录")
                        colors: Theme.floating
                        onClicked: {
                            panel.close();
                            panel.logoutRequested();
                        }
                    }
                }
            }
        }
    }
}
