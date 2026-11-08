pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Theme
import EchoMusic.Controls

Item {
    id: header
    objectName: "songListHeader"
    required property SongListGeometry geometry
    property bool lyricColumn: false
    property bool showAlbum: true
    property int sortField: 0
    property int sortDirection: 0
    signal sortPicked(int field)
    implicitHeight: 44
    // The shared view contract uses Index/Title/Artist/Album/Duration = 1..5.
    component SortChoice: Button {
        id: choice
        required property int field
        required property string label
        required property Item owner
        padding: 0
        hoverEnabled: true
        Accessible.name: field === 1 ? qsTr("按序号排序") : qsTr("按%1排序").arg(label)
        contentItem: EchoButtonContent {
            spacing: 3
            text: choice.label
            color: choice.hovered || header.sortField === choice.field ? Theme.accentText : Theme.secondaryText

            iconColor: choice.hovered || header.sortField === choice.field ? Theme.accentText : Theme.iconMain
            glyphSize: 14
            glyphAfterText: true
            glyph: header.sortField === choice.field ? header.sortDirection === 2 ? "arrow-narrow-down" : "arrow-narrow-up" : "switch-vertical"
            font: Theme.font(12, Theme.boldWeight)
        }
        background: null
        onClicked: header.sortPicked(field)
    }
    RowLayout {
        EchoUiColumnGroup.controller: header.geometry
        EchoUiColumnGroup.keys: header.geometry.uiColumnKeys
        EchoUiColumnGroup.header: true
        anchors.fill: parent
        spacing: 0
        Item {
            objectName: "songHeaderIndexColumn"
            Layout.preferredWidth: header.geometry.indexWidth
            Layout.minimumWidth: header.geometry.indexWidth
            Layout.maximumWidth: header.geometry.indexWidth
            Layout.fillHeight: true
            SortChoice {
                x: 8
                anchors.verticalCenter: parent.verticalCenter
                field: 1
                label: "#"
                owner: header
            }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            Item {
                Layout.preferredWidth: 52
            }
            SortChoice {
                field: 2
                label: qsTr("歌曲")
                owner: header
                Accessible.name: qsTr("按歌曲名排序")
            }
            Text {
                text: "/"
                color: Theme.secondaryText
                font: Theme.font(12, Theme.regularWeight)
            }
            SortChoice {
                field: 3
                label: qsTr("歌手")
                owner: header
            }
            Item {
                Layout.fillWidth: true
            }
        }
        Item {
            visible: header.showAlbum
            Layout.preferredWidth: header.lyricColumn ? header.geometry.lyricWidth : header.geometry.albumWidth
            Layout.fillHeight: true
            SortChoice {
                anchors.verticalCenter: parent.verticalCenter
                field: 4
                label: header.lyricColumn ? qsTr("歌词") : qsTr("专辑")
                owner: header
            }
        }
        Item {
            Layout.preferredWidth: header.geometry.durationWidth
            Layout.fillHeight: true
            SortChoice {
                x: 8
                anchors.verticalCenter: parent.verticalCenter
                field: 5
                label: qsTr("时长")
                owner: header
            }
        }
    }
    Rectangle {
        objectName: "songListHeaderDivider"
        anchors.bottom: parent.bottom
        width: parent.width
        height: 1
        color: Theme.subtleBorder
    }
}
