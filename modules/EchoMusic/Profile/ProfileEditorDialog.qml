pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Account
import EchoMusic.Controls
import EchoMusic.Theme

EchoDialog {
    id: dialog
    objectName: "profileEditorDialog"
    required property ProfileService profile
    preferredWidth: 520
    heading: qsTr("编辑个人资料")
    description: qsTr("修改后的资料会同步到当前酷狗账号。")
    closable: !profile.saving
    closePolicy: profile.saving ? Popup.NoAutoClose : Popup.CloseOnEscape | Popup.CloseOnPressOutside
    property string draftName: ""
    property int draftGender: 2
    property string draftBirthday: ""
    property string draftSignature: ""
    property string draftProvince: ""
    property string draftCity: ""
    onAboutToShow: {
        profile.beginEdit();
        draftName = profile.account.nickname;
        draftGender = profile.account.gender;
        draftBirthday = profile.account.birthday;
        draftSignature = profile.account.biography;
        draftProvince = profile.account.province;
        draftCity = profile.account.city;
    }
    Connections {
        target: dialog.profile
        function onProfileSaved(): void {
            dialog.close();
        }
    }
    flushBody: true
    body: Item {
        implicitHeight: form.implicitHeight
        Flickable {
            id: formView
            objectName: "profileEditorScroll"
            ScrollBar.vertical: EchoPopupScrollBar {
                surface: dialog
                view: formView
                resourceActive: dialog.visible
            }
            anchors.fill: parent
            clip: true
            contentWidth: width
            contentHeight: form.implicitHeight
            boundsBehavior: Flickable.StopAtBounds
            ColumnLayout {
                id: form
                width: formView.width - 24
                spacing: 16
                enabled: !dialog.profile.saving
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 7
                    Label {
                        text: qsTr("昵称")
                        font: Theme.font(12, Theme.extraBoldWeight)
                        color: Theme.floatingSecondaryText
                    }
                    EchoTextField {
                        objectName: "profileEditNickname"
                        Layout.fillWidth: true
                        text: dialog.draftName
                        placeholderText: qsTr("请输入昵称")
                        colors: Theme.floating
                        onTextEdited: dialog.draftName = text
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    // Two equal-width columns with a zero minimum width.
                    uniformCellSizes: true
                    spacing: 12
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        spacing: 7
                        Label {
                            text: qsTr("性别")
                            font: Theme.font(12, Theme.extraBoldWeight)
                            color: Theme.floatingSecondaryText
                        }
                        EchoSelect {
                            objectName: "profileEditGender"
                            Layout.fillWidth: true
                            model: [qsTr("女"), qsTr("男"), qsTr("保密")]
                            currentIndex: dialog.draftGender
                            colors: Theme.floating
                            onActivated: dialog.draftGender = currentIndex
                        }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        spacing: 7
                        Label {
                            text: qsTr("生日")
                            font: Theme.font(12, Theme.extraBoldWeight)
                            color: Theme.floatingSecondaryText
                        }
                        EchoDatePicker {
                            objectName: "profileEditBirthday"
                            Layout.fillWidth: true
                            value: dialog.draftBirthday
                            placeholderText: qsTr("请选择生日")
                            accessibleName: qsTr("生日")
                            colors: Theme.floating
                            uiScale: dialog.uiScale
                            onPicked: date => dialog.draftBirthday = date
                        }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    uniformCellSizes: true
                    spacing: 12
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        spacing: 7
                        Label {
                            text: qsTr("省份")
                            font: Theme.font(12, Theme.extraBoldWeight)
                            color: Theme.floatingSecondaryText
                        }
                        EchoTextField {
                            Layout.fillWidth: true
                            text: dialog.draftProvince
                            placeholderText: qsTr("例如：广东")
                            colors: Theme.floating
                            onTextEdited: dialog.draftProvince = text
                        }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        spacing: 7
                        Label {
                            text: qsTr("城市")
                            font: Theme.font(12, Theme.extraBoldWeight)
                            color: Theme.floatingSecondaryText
                        }
                        EchoTextField {
                            Layout.fillWidth: true
                            text: dialog.draftCity
                            placeholderText: qsTr("例如：广州")
                            colors: Theme.floating
                            onTextEdited: dialog.draftCity = text
                        }
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 7
                    Label {
                        text: qsTr("个性签名")
                        font: Theme.font(12, Theme.extraBoldWeight)
                        color: Theme.floatingSecondaryText
                    }
                    EchoTextArea {
                        objectName: "profileEditSignature"
                        Layout.fillWidth: true
                        implicitHeight: 84
                        text: dialog.draftSignature
                        placeholderText: qsTr("写下一句想说的话")
                        colors: Theme.floating
                        onTextChanged: if (activeFocus)
                            dialog.draftSignature = text
                        Accessible.description: qsTr("个性签名最多120个字符")
                    }
                    RowLayout {
                        Layout.fillHeight: false
                        spacing: 5
                        Layout.fillWidth: true
                        Label {
                            Layout.fillWidth: true
                            text: qsTr("留空保存可清除个性签名")
                            font: Theme.font(11, Theme.semiboldWeight)
                            color: Theme.floatingSecondaryText
                        }
                        Label {
                            text: dialog.draftSignature.length + " / 120"
                            font: Theme.font(11, Theme.semiboldWeight)
                            color: dialog.draftSignature.length > 120 ? "#ef4444" : Theme.floatingSecondaryText
                        }
                    }
                }
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
            enabled: !dialog.profile.saving
            background: EchoActionBackground {
                colors: Theme.floating
                color: "transparent"
                border.width: 1
                border.color: Theme.floatingBorder
            }
            onClicked: dialog.close()
        }
        EchoButton {
            objectName: "profileSave"
            text: dialog.profile.saving ? qsTr("正在保存…") : qsTr("保存修改")
            size: EchoControlSize.Standard
            colors: Theme.floating
            enabled: !dialog.profile.saving && dialog.draftSignature.length <= 120
            onClicked: dialog.profile.saveProfile(dialog.draftName, dialog.draftGender, dialog.draftBirthday, dialog.draftSignature, dialog.draftProvince, dialog.draftCity)
        }
    }
}
