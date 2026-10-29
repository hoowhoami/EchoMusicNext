pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Shell
import EchoMusic.Controls
import EchoMusic.Theme
import EchoMusic.Account

RoutePage {
    id: page
    objectName: "profilePage"
    required property AccountController service
    required property ProfileService profile
    readonly property AccountService account: service.account
    onPageActiveChanged: {
        if (!pageActive) {
            switcherOwner.close();
            logoutOwner.close();
            gradeOwner.close();
            editorOwner.close();
            socialOwner.close();
            devicesOwner.close();
            preferencesOwner.close();
            blacklistOwner.close();
            avatarPickerOwner.close();
            details.closePopups();
        }
    }
    EchoPageScrollContainer {
        anchors.fill: parent
        pageActive: page.pageActive
        scrollView: view
        Flickable {
            id: view
            objectName: "profileScrollView"
            anchors.fill: parent
            contentWidth: width
            contentHeight: Math.max(height, body.implicitHeight + 40)
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ColumnLayout {
                id: body
                width: Math.max(0, Math.min(1000, view.width) - (page.Window.window && page.Window.window.width <= 600 ? 32 : 48))
                x: (view.width - width) / 2
                y: 16
                visible: page.account.userId > 0
                spacing: 20
                GridLayout {
                    id: header
                    Layout.fillHeight: false
                    Layout.fillWidth: true
                    columns: width >= heading.implicitWidth + actions.implicitWidth + 16 ? 2 : 1
                    columnSpacing: 16
                    rowSpacing: 16
                    Label {
                        id: heading
                        Layout.fillWidth: true
                        text: qsTr("个人中心")
                        color: Theme.text
                        font: Theme.spacedFont(20, Theme.semiboldWeight, -0.5)
                    }
                    RowLayout {
                        id: actions
                        Layout.fillWidth: false
                        Layout.fillHeight: false
                        Layout.alignment: Qt.AlignRight
                        spacing: 8
                        ProfileActionButton {
                            objectName: "profileSwitchAccount"
                            glyph: "users"
                            text: qsTr("切换账号")
                            tooltipText: text
                            implicitWidth: 36
                            implicitHeight: 36
                            padding: 8
                            onClicked: switcherOwner.open()
                        }
                        ProfileActionButton {
                            objectName: "profileEdit"
                            glyph: "pencil"
                            text: qsTr("编辑个人资料")
                            tooltipText: text
                            implicitWidth: 36
                            implicitHeight: 36
                            padding: 8.5
                            enabled: page.account.userId > 0 && !page.account.profileLoading
                            onClicked: editorOwner.open()
                        }
                        ProfileActionButton {
                            objectName: "profilePreferences"
                            glyph: "headphones"
                            text: qsTr("听歌偏好")
                            tooltipText: text
                            implicitWidth: 36
                            implicitHeight: 36
                            padding: 8
                            enabled: page.account.userId > 0
                            onClicked: preferencesOwner.open()
                        }
                        ProfileActionButton {
                            objectName: "profileDevices"
                            glyph: "device-mobile"
                            text: qsTr("登录设备")
                            tooltipText: text
                            implicitWidth: 36
                            implicitHeight: 36
                            padding: 8
                            enabled: page.account.userId > 0
                            onClicked: devicesOwner.open()
                        }
                        ProfileActionButton {
                            objectName: "profileLogout"
                            danger: true
                            glyph: "logout"
                            text: qsTr("退出登录")
                            tooltipText: text
                            implicitWidth: 36
                            implicitHeight: 36
                            padding: 8
                            onClicked: logoutOwner.open()
                        }
                    }
                }
                ProfileOverview {
                    Layout.fillWidth: true
                    account: page.account
                    uploadingAvatar: page.profile.uploadingAvatar
                    onAvatarRequested: avatarPickerOwner.open()
                    onGradeRequested: {
                        gradeOwner.open();
                        page.account.loadGrade();
                    }
                    onSocialRequested: tab => {
                        page.profile.openSocial(tab);
                        socialOwner.open();
                    }
                }
                ProfileDetails {
                    id: details
                    Layout.fillWidth: true
                    account: page.account
                    uiScale: page.settings.uiScale
                }
                RowLayout {
                    Layout.fillHeight: false
                    spacing: 5
                    visible: page.account.profileError.length > 0
                    Layout.fillWidth: true
                    Label {
                        Layout.fillWidth: true
                        text: page.account.profileError
                        color: Theme.secondaryText
                        font: Theme.font(12, Theme.regularWeight)
                    }
                    EchoSoftButton {
                        text: qsTr("重试")
                        enabled: !page.account.profileLoading
                        onClicked: {
                            page.account.reloadProfile();
                            page.account.loadGrade();
                        }
                    }
                }
            }
        }
    }
    EchoFileDialogLoader {
        id: avatarPickerOwner
        hostItem: page
        sourceComponent: Component {
            EchoFileDialog {
                id: avatarPicker
                title: qsTr("修改头像")
                nameFilters: [qsTr("头像图片 (*.jpg *.jpeg *.png *.gif)")]
                onAccepted: page.profile.uploadAvatar(selectedFile)
            }
        }
    }
    ColumnLayout {
        anchors.centerIn: parent
        visible: !page.account.userId
        spacing: 16
        EchoIcon {
            Layout.alignment: Qt.AlignHCenter
            name: "user"
            Layout.preferredWidth: 64
            Layout.preferredHeight: 64
            color: Theme.secondaryText
        }
        Label {
            text: qsTr("请先登录以查看个人中心")
            color: Theme.secondaryText
            // The empty-state caption uses regular type.
            font: Theme.font(16, Theme.boldWeight)
        }
        EchoButton {
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: 8
            text: qsTr("立即登录")
            size: EchoControlSize.Standard
            // Keep the shared primary action's palette, states and control radius.
            onClicked: page.service.openLogin()
        }
    }
    EchoPopupLoader {
        id: switcherOwner
        objectName: "accountSwitcherDialogOwner"
        hostItem: page
        sourceComponent: Component {
            AccountSwitcherDialog {
                id: switcher
                service: page.service
                uiScale: page.settings.uiScale
            }
        }
    }
    EchoPopupLoader {
        id: gradeOwner
        objectName: "profileGradeDialogOwner"
        hostItem: page
        sourceComponent: Component {
            GradeDialog {
                id: grade
                account: page.account
                uiScale: page.settings.uiScale
            }
        }
    }
    EchoPopupLoader {
        id: logoutOwner
        objectName: "logoutConfirmationOwner"
        hostItem: page
        sourceComponent: Component {
            LogoutConfirmDialog {
                id: logout
                service: page.service
                uiScale: page.settings.uiScale
            }
        }
    }
    EchoPopupLoader {
        id: editorOwner
        objectName: "profileEditorDialogOwner"
        hostItem: page
        sourceComponent: Component {
            ProfileEditorDialog {
                id: editor
                profile: page.profile
                uiScale: page.settings.uiScale
            }
        }
    }
    EchoPopupLoader {
        id: socialOwner
        objectName: "profileSocialDrawerOwner"
        hostItem: page
        sourceComponent: Component {
            SocialDrawer {
                id: social
                profile: page.profile
                uiScale: page.settings.uiScale
            }
        }
    }
    EchoPopupLoader {
        id: devicesOwner
        objectName: "profileDeviceDialogOwner"
        hostItem: page
        sourceComponent: Component {
            DeviceManagerDialog {
                id: devices
                profile: page.profile
                uiScale: page.settings.uiScale
            }
        }
    }
    EchoPopupLoader {
        id: preferencesOwner
        objectName: "listeningPreferencesDialogOwner"
        hostItem: page
        sourceComponent: Component {
            ListeningPreferencesDialog {
                id: preferences
                service: page.profile.preferences
                uiScale: page.settings.uiScale
                onBlacklistRequested: blacklistOwner.open()
            }
        }
    }
    EchoPopupLoader {
        id: blacklistOwner
        objectName: "contentBlacklistDialogOwner"
        hostItem: page
        sourceComponent: Component {
            ContentBlacklistDialog {
                id: blacklist
                service: page.profile.blacklist
                uiScale: page.settings.uiScale
            }
        }
    }
}
