pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Controls
import EchoMusic.Theme
import EchoMusic.Account

EchoDialog {
    id: dialog
    objectName: "accountSwitcherDialog"
    required property AccountController service
    property bool managing: false
    property double removalId: 0
    property string removalName: ""
    property url removalAvatar
    property url removalIdentityIcon
    property string removalIdentityLabel: ""
    preferredWidth: 440
    heading: managing ? qsTr("管理账号") : qsTr("切换账号")
    headingCount: service.savedAccounts.count
    onOpened: {
        managing = false;
        removalId = 0;
        service.savedAccounts.beginDurationQueries();
    }
    onAboutToHide: service.savedAccounts.cancelSwitch()
    Component.onDestruction: service.savedAccounts.cancelSwitch()
    onClosed: {
        managing = false;
        removalId = 0;
        removalOwner.close();
        service.savedAccounts.endDurationQueries();
    }
    flushBody: true
    body: ColumnLayout {
        spacing: 8

        EchoBoundedListView {
            id: accounts
            maximumViewportHeight: Math.max(0, dialog.bodyHeightLimit - (accountError.visible ? accountError.implicitHeight + 8 : 0))
            minimumViewportHeight: 72
            objectName: "savedAccountList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            // Account metadata can wrap in Flow, so these rows are not fixed 68px.
            Layout.preferredHeight: implicitHeight
            clip: true
            reuseItems: dialog.visible
            cacheBuffer: 78 * 2
            spacing: 10
            boundsBehavior: Flickable.StopAtBounds
            model: dialog.service.savedAccounts
            ScrollBar.vertical: EchoPopupScrollBar {
                surface: dialog
                view: accounts
                resourceActive: dialog.visible
            }
            delegate: Rectangle {
                id: row
                required property double accountId
                required property string accountName
                required property url avatarUrl
                required property url avatarIdentityIcon
                required property string avatarIdentityLabel
                required property bool isCurrent
                required property string listeningDuration
                required property bool switchChecking
                required property string switchError
                required property bool loginExpired
                required property bool durationLoading
                property bool pooled: false
                ListView.onPooled: pooled = true
                ListView.onReused: pooled = false
                objectName: "savedAccountRow"
                width: accounts.width - 24
                height: Math.max(68, accountInfo.implicitHeight + 24)
                radius: Theme.itemRadius
                color: isCurrent ? Theme.floating.selected : Theme.floating.mutedSurface
                border.width: 1
                border.color: Theme.floating.surfaceOutline
                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 12
                    AccountAvatar {
                        objectName: "savedAccountAvatar"
                        source: row.avatarUrl
                        identityIcon: row.avatarIdentityIcon
                        identityLabel: row.avatarIdentityLabel
                        identityInset: 0
                        resourceActive: dialog.visible && !row.pooled
                        Layout.preferredWidth: 44
                        Layout.preferredHeight: 44
                    }
                    ColumnLayout {
                        id: accountInfo
                        Layout.fillHeight: false
                        Layout.fillWidth: true
                        spacing: 4
                        EchoTextTagRow {
                            Layout.fillWidth: true
                            spacing: 8
                            text: row.accountName || qsTr("账号 ") + row.accountId
                            color: Theme.floatingText
                            font: Theme.font(14, Theme.semiboldWeight)
                            MembershipTag {
                                id: currentMembership
                                visible: row.isCurrent && dialog.service.account.memberships.length > 0
                                colors: Theme.floating
                                kind: dialog.service.account.memberships.length ? dialog.service.account.memberships[0].kind : "none"
                                text: dialog.service.account.membershipLabel
                            }
                            EchoTag {
                                id: currentTag
                                objectName: "currentAccountTag"
                                visible: row.isCurrent
                                text: qsTr("当前登录")
                                tone: EchoTag.Accent
                                colors: Theme.floating
                            }
                        }
                        Flow {

                            Layout.fillWidth: true
                            Layout.topMargin: 2
                            spacing: 12
                            Label {
                                text: "ID: " + row.accountId
                                color: Theme.floatingSecondaryText
                                font: Theme.font(11, Theme.regularWeight)
                            }
                            Label {
                                text: qsTr("听歌 ") + (row.listeningDuration || (row.durationLoading ? qsTr("正在获取…") : qsTr("暂无数据")))
                                color: Theme.floatingSecondaryText
                                font: Theme.font(11, Theme.regularWeight)
                            }
                        }
                        Label {
                            objectName: "savedAccountSwitchError"
                            Layout.fillWidth: true
                            visible: text.length > 0
                            text: row.switchError
                            color: Theme.floating.dangerText
                            font: Theme.font(11, Theme.regularWeight)
                            wrapMode: Text.Wrap
                        }
                    }
                    EchoSoftButton {
                        visible: !row.isCurrent
                        enabled: !dialog.service.savedAccounts.busy
                        loading: row.switchChecking
                        implicitWidth: Math.max(56, contentItem.implicitWidth + 16)
                        implicitHeight: 32
                        leftPadding: 8
                        rightPadding: 8
                        font: Theme.font(12, Theme.blackWeight)
                        colors: Theme.floating
                        danger: dialog.managing
                        objectName: "savedAccountAction"
                        text: dialog.managing ? qsTr("移除") : row.switchChecking ? qsTr("正在验证…") : row.loginExpired ? qsTr("重新登录") : qsTr("切换")
                        onClicked: {
                            if (dialog.managing) {
                                dialog.removalId = row.accountId;
                                dialog.removalName = row.accountName;
                                dialog.removalAvatar = row.avatarUrl;
                                dialog.removalIdentityIcon = row.avatarIdentityIcon;
                                dialog.removalIdentityLabel = row.avatarIdentityLabel;
                                removalOwner.open();
                            } else if (row.loginExpired) {
                                dialog.close();
                                dialog.service.reauthenticateAccount(row.accountId);
                            } else
                                dialog.service.savedAccounts.switchAccount(row.accountId);
                        }
                    }
                }
            }
            Label {
                anchors.centerIn: parent
                visible: dialog.service.savedAccounts.count === 0
                text: qsTr("暂无保存的账号")
                color: Theme.floatingSecondaryText
                font: Theme.font(12, Theme.regularWeight)
            }
        }
        Label {
            id: accountError
            visible: text.length > 0
            Layout.fillWidth: true
            Layout.rightMargin: 24
            text: dialog.service.savedAccounts.errorMessage
            color: Theme.floating.dangerText
            font: Theme.font(12, Theme.regularWeight)
            wrapMode: Text.Wrap
        }
    }
    footerContent: RowLayout {
        spacing: 12
        EchoSoftButton {
            visible: dialog.service.savedAccounts.count > 0
            objectName: "accountManageButton"
            text: dialog.managing ? qsTr("完成管理") : qsTr("管理账号")
            colors: Theme.floating
            implicitHeight: 32
            font: Theme.font(12, Theme.blackWeight)
            leftPadding: 16
            rightPadding: 16
            onClicked: dialog.managing = !dialog.managing
        }
        Item {
            Layout.fillWidth: true
        }
        EchoButton {
            objectName: "accountAddButton"
            text: qsTr("添加账号")
            colors: Theme.floating
            size: EchoControlSize.Standard
            // Preserve the user's 32px exception with the original sm padding/font.
            leftPadding: 16
            rightPadding: 16
            font: Theme.font(12, Theme.blackWeight)
            implicitHeight: 32
            enabled: !dialog.service.savedAccounts.busy
            onClicked: {
                dialog.close();
                dialog.service.openLogin();
            }
        }
    }
    Connections {
        target: dialog.service.savedAccounts
        function onSwitched() {
            dialog.close();
        }
    }
    Connections {
        target: dialog.service
        function onChanged() {
            if (dialog.service.loginOpen)
                dialog.close();
        }
    }
    EchoPopupLoader {
        id: removalOwner
        objectName: "accountRemovalConfirmationOwner"
        hostItem: dialog.contentItem
        sourceComponent: Component {
            EchoDialog {
                id: removal
                objectName: "accountRemovalConfirmation"
                uiScale: dialog.uiScale
                heading: qsTr("移除账号")
                description: qsTr("将清除本机保存的登录信息，再次使用需要重新登录。")
                body: RowLayout {
                    spacing: 5
                    AccountAvatar {
                        source: dialog.removalAvatar
                        identityIcon: dialog.removalIdentityIcon
                        identityLabel: dialog.removalIdentityLabel
                        resourceActive: removal.visible
                        Layout.preferredWidth: 40
                        Layout.preferredHeight: 40
                    }
                    ColumnLayout {
                        Layout.fillHeight: false
                        Layout.fillWidth: true
                        Label {
                            text: dialog.removalName
                            color: Theme.floatingText
                            font: Theme.font(14, Theme.semiboldWeight)
                        }
                        Label {
                            text: "ID: " + dialog.removalId
                            color: Theme.floatingSecondaryText
                            font: Theme.font(11, Theme.regularWeight)
                        }
                    }
                }
                footerContent: RowLayout {
                    spacing: 5
                    Item {
                        Layout.fillWidth: true
                    }
                    EchoSoftButton {
                        text: qsTr("取消")
                        colors: Theme.floating
                        onClicked: removal.close()
                    }
                    EchoSoftButton {
                        text: qsTr("确认移除")
                        danger: true
                        colors: Theme.floating
                        onClicked: {
                            dialog.service.savedAccounts.forgetAccount(dialog.removalId);
                            removal.close();
                        }
                    }
                }
            }
        }
    }
}
