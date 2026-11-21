pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import EchoMusic.Theme
import EchoMusic.Controls
import EchoMusic.Search

Item {
    id: root
    required property SearchModel service
    property real listPadding: 24
    property bool continuousSongs: false
    readonly property bool failed: service.state === SearchModel.Error
    implicitHeight: failed ? listPadding + errorLoader.implicitHeight : continuousSongs ? 0 : listPadding + (service.count > 0 || service.hasMore ? 50 : 0) + 40
    Loader {
        id: errorLoader
        anchors.left: parent.left
        anchors.right: parent.right
        y: root.listPadding
        active: root.visible && root.failed
        sourceComponent: EchoLoadError {
            message: root.service.errorMessage
            onRetryRequested: root.service.retry()
        }
    }
    RowLayout {
        anchors.horizontalCenter: parent.horizontalCenter
        y: root.listPadding + 8
        visible: !root.failed && !root.continuousSongs
        Text {
            text: root.service.state === SearchModel.LoadingMore ? qsTr("加载更多中...") : root.service.hasMore ? qsTr("继续下滑加载更多") : root.service.count > 0 ? qsTr("没有更多结果了") : ""
            color: Theme.secondaryText
            font: Theme.font(13, Theme.mediumWeight)
        }
    }
}
