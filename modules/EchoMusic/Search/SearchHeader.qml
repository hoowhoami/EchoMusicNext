pragma ComponentBehavior: Bound
import QtQuick
import EchoMusic.Theme
import EchoMusic.Controls
import EchoMusic.Search

Item {
    id: header
    required property SearchModel service
    property bool pinned: false
    signal kindPicked(int kind)
    readonly property list<int> kinds: [SearchModel.Song, SearchModel.Playlist, SearchModel.Album, SearchModel.Artist, SearchModel.Lyric, SearchModel.Mv]
    readonly property list<string> labels: [qsTr("单曲"), qsTr("歌单"), qsTr("专辑"), qsTr("歌手"), qsTr("歌词"), qsTr("MV")]
    implicitHeight: pinned ? 56 : heading.height + 16 + (service.keyword.length ? 68 : 0)
    Text {
        id: heading
        x: Theme.pageHorizontalInset
        y: 16
        width: parent.width - Theme.pageHorizontalInset * 2
        text: header.service.keyword.length ? qsTr("搜索「%1」").arg(header.service.keyword) : qsTr("搜索")

        textFormat: Text.PlainText
        color: Theme.text
        font: Theme.font(22, Theme.semiboldWeight)
        Accessible.role: Accessible.Heading
        Accessible.name: text
        wrapMode: Text.Wrap
        visible: !header.pinned
    }
    EchoTabBar {
        objectName: header.pinned ? "pinnedSearchTypeTabs" : "searchTypeTabs"
        x: Theme.pageHorizontalInset
        y: header.pinned ? 6 : heading.y + heading.height + 24
        width: parent.width - Theme.pageHorizontalInset * 2
        labels: header.labels
        accessibleName: qsTr("搜索类型")
        currentIndex: header.kinds.indexOf(header.service.kind)
        visible: header.service.keyword.length > 0
        onPicked: index => header.kindPicked(header.kinds[index])
    }
}
