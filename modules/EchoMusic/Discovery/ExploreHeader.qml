pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Controls
import EchoMusic.Theme

Item {
    id: header
    required property ExploreService service
    signal pickRequested(string kind)
    implicitHeight: 136
    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: 40
        anchors.rightMargin: 40
        spacing: 16
        Label {
            Layout.fillWidth: true
            text: qsTr("歌手推荐")
            font: Theme.spacedFont(24, Theme.semiboldWeight, -.6)
            color: Theme.text
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 12
            EchoSoftButton {
                text: qsTr("性别: %1").arg(header.service.sexFilters.find(option => option.id === header.service.sex)?.label || qsTr("全部"))
                glyph: "chevron-down"
                onClicked: header.pickRequested("sex")
            }
            EchoSoftButton {
                text: qsTr("类型: %1").arg(header.service.typeFilters.find(option => option.id === header.service.artistType)?.label || qsTr("全部"))
                glyph: "chevron-down"
                onClicked: header.pickRequested("artistType")
            }
            Item {
                Layout.fillWidth: true
            }
        }
    }
}
