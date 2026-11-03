pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Controls
import EchoMusic.Theme
import EchoMusic.Library

EchoDialog {
    id: dialog
    objectName: "playlistRemoveDialog"
    required property LibraryController service
    property double cloudId: 0
    property string playlistTitle: ""
    property bool owned: true
    heading: owned ? qsTr("删除歌单") : qsTr("取消收藏")
    description: qsTr("确定要") + (owned ? qsTr("删除") : qsTr("取消收藏")) + "「" + playlistTitle + "」吗？"
    closePolicy: service.busy ? Popup.NoAutoClose : Popup.CloseOnEscape | Popup.CloseOnPressOutside
    closable: !service.busy
    body: Label {
        text: dialog.service.errorMessage
        visible: text.length > 0
        color: Theme.floating.dangerText
        font: Theme.font(12, Theme.regularWeight)
        wrapMode: Text.Wrap
    }
    footerContent: RowLayout {
        spacing: 5
        Item {
            Layout.fillWidth: true
        }
        EchoSoftButton {
            objectName: "playlistRemoveCancel"
            text: qsTr("取消")
            enabled: !dialog.service.busy
            colors: Theme.floating
            onClicked: dialog.close()
        }
        EchoSoftButton {
            objectName: "playlistRemoveConfirm"
            text: dialog.service.busy ? qsTr("处理中…") : dialog.owned ? qsTr("删除") : qsTr("取消收藏")
            danger: true
            enabled: !dialog.service.busy
            colors: Theme.floating
            onClicked: dialog.service.remove(dialog.cloudId)
        }
    }
    Connections {
        target: dialog.service
        function onRemoved(resourceId: string) {
            dialog.close();
        }
    }
}
