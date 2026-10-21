pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Theme

// Anchored confirmation owned by an external EchoPopupLoader.
// The complete popup is constructed and disposed with each open cycle.
EchoAnchoredPopup {
    id: popup
    property string title: qsTr("确认操作")
    property string description: ""
    property string confirmText: qsTr("确认")
    property string cancelText: qsTr("取消")
    property bool danger: false
    property bool confirmDisabled: false
    property bool confirmLoading: false
    signal confirmed
    signal cancelled
    preferredWidth: Math.min(360, Math.max(260, titleMetrics.advanceWidth + 68))
    preferAbove: true
    contentAlign: "center"
    padding: 16
    leftPadding: 18
    rightPadding: 18
    TextMetrics {
        id: titleMetrics
        text: popup.title
        font: Theme.font(14, Theme.mediumWeight)
    }
    contentItem: ColumnLayout {
        spacing: 14
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: false
            spacing: 12
            Rectangle {
                Layout.preferredWidth: 20
                Layout.preferredHeight: 20
                Layout.alignment: Qt.AlignTop
                Layout.topMargin: 1
                radius: 10
                color: Theme.floating.warning
                EchoTextLine {
                    anchors.fill: parent
                    text: "!"
                    font: Theme.font(13, Theme.extraBoldWeight)
                    color: Theme.floating.elevated
                    fillTextWidth: true
                    horizontalAlignment: Qt.AlignHCenter
                }
            }
            ColumnLayout {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                Layout.fillHeight: false
                spacing: 6
                EchoTextLine {
                    Layout.fillWidth: true
                    wrap: true
                    text: popup.title
                    font: Theme.font(14, Theme.mediumWeight)
                    lineHeight: 20.3
                    color: Theme.floatingText
                }
                Label {
                    Layout.fillWidth: true
                    visible: popup.description.length > 0
                    text: popup.description
                    wrapMode: Text.Wrap
                    font: Theme.font(12, Theme.regularWeight)
                    color: Theme.floatingSecondaryText
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Item {
                Layout.fillWidth: true
            }
            EchoSoftButton {
                objectName: "popconfirmCancel"
                colors: Theme.floating
                size: EchoControlSize.Compact
                font: Theme.font(12, Theme.boldWeight)
                leftPadding: 10
                rightPadding: 10
                text: popup.cancelText
                onClicked: {
                    popup.close();
                    popup.cancelled();
                }
            }
            EchoSoftButton {
                objectName: "popconfirmConfirm"
                colors: Theme.floating
                size: EchoControlSize.Compact
                font: Theme.font(12, Theme.boldWeight)
                leftPadding: 10
                rightPadding: 10
                text: popup.confirmText
                danger: popup.danger
                accent: !popup.danger
                loading: popup.confirmLoading
                enabled: !popup.confirmDisabled && !popup.confirmLoading
                onClicked: {
                    popup.close();
                    popup.confirmed();
                }
            }
        }
    }
}
