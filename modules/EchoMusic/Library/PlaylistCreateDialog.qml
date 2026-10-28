pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Controls
import EchoMusic.Theme
import EchoMusic.Library

EchoDialog {
    id: dialog
    objectName: "playlistCreateDialog"
    required property LibraryController service
    property string playlistTitle: ""
    property bool privatePlaylist: false
    heading: qsTr("新建歌单")
    description: qsTr("输入歌单名称，可选设为隐私歌单。")
    closePolicy: service.busy ? Popup.NoAutoClose : Popup.CloseOnEscape | Popup.CloseOnPressOutside
    closable: !service.busy
    onOpened: {
        playlistTitle = "";
        privatePlaylist = false;
    }
    body: ColumnLayout {
        spacing: 16
        EchoTextField {
            Layout.fillWidth: true
            colors: Theme.floating
            placeholderText: qsTr("请输入歌单名称")
            enabled: !dialog.service.busy
            clearable: true
            text: dialog.playlistTitle
            onTextEdited: dialog.playlistTitle = text
            onAccepted: dialog.service.create(dialog.playlistTitle, dialog.privatePlaylist)
        }
        Rectangle {
            objectName: "playlistPrivacyCard"
            Layout.fillWidth: true
            implicitHeight: 66
            radius: Theme.cardRadius
            color: Theme.floating.mutedSurface
            RowLayout {
                spacing: 5
                anchors.fill: parent
                anchors.margins: 12
                ColumnLayout {
                    Layout.fillHeight: false
                    Layout.fillWidth: true
                    spacing: 4
                    Label {
                        text: qsTr("设为隐私歌单")
                        color: Theme.floatingText
                        font: Theme.font(14, Theme.mediumWeight)
                    }
                    Label {
                        text: qsTr("仅自己可见")
                        color: Theme.floatingSecondaryText
                        font: Theme.font(12, Theme.regularWeight)
                    }
                }
                EchoSwitch {
                    objectName: "playlistPrivateSwitch"
                    Layout.fillWidth: false
                    Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                    checked: dialog.privatePlaylist
                    enabled: !dialog.service.busy
                    onToggled: dialog.privatePlaylist = checked
                }
            }
        }
        Label {
            visible: text.length > 0
            Layout.fillWidth: true
            text: dialog.service.errorMessage
            color: Theme.floating.dangerText
            font: Theme.font(12, Theme.regularWeight)
            wrapMode: Text.Wrap
        }
    }
    footerContent: RowLayout {
        spacing: 5
        Item {
            Layout.fillWidth: true
        }
        EchoButton {
            size: EchoControlSize.Standard
            accent: false
            text: qsTr("取消")
            enabled: !dialog.service.busy
            colors: Theme.floating
            onClicked: dialog.close()
        }
        EchoButton {
            size: EchoControlSize.Standard
            colors: Theme.floating
            text: dialog.service.busy ? qsTr("创建中…") : qsTr("创建")
            enabled: !dialog.service.busy
            onClicked: dialog.service.create(dialog.playlistTitle, dialog.privatePlaylist)
        }
    }
    Connections {
        target: dialog.service
        function onCreated() {
            dialog.close();
        }
    }
}
