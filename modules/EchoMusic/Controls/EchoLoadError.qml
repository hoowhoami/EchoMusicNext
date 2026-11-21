pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import EchoMusic.Theme

// A recoverable content-read failure. Callers own the retry operation and
// instantiate this only while an error is presented.
Item {
    id: root
    required property string message
    signal retryRequested
    implicitWidth: 320
    implicitHeight: line.implicitHeight + 24
    RowLayout {
        id: line
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.verticalCenter: parent.verticalCenter
        width: Math.min(Math.max(0, root.width - 24), Math.max(280, messageText.implicitWidth + retryButton.implicitWidth + 42))
        spacing: 12
        EchoIcon {
            Layout.preferredWidth: 18
            Layout.preferredHeight: 18
            name: "alert-circle"
            color: Theme.secondaryText
        }
        Text {
            id: messageText
            objectName: "loadErrorMessage"
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            text: root.message
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            color: Theme.secondaryText
            font: Theme.font(12, Theme.regularWeight)
        }
        EchoSoftButton {
            id: retryButton
            objectName: "loadErrorRetry"
            text: qsTr("重试")
            onClicked: root.retryRequested()
        }
    }
}
