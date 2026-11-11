pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Controls
import EchoMusic.Theme

EchoDialog {
    id: dialog
    required property ListenTogetherUi service
    property ListenSongPicker picker: null
    objectName: "listenSongPickerDialog"
    heading: service.room.owner ? qsTr("添加歌曲") : qsTr("点歌")
    preferredWidth: 780
    Component.onCompleted: picker = service.createPicker(dialog)
    Connections {
        target: dialog.picker
        function onSubmitted(): void {
            dialog.close();
        }
    }
    body: Component {
        ColumnLayout {
            spacing: 16
            EchoTabBar {
                Layout.fillWidth: true
                labels: [qsTr("最近播放"), qsTr("我喜欢"), qsTr("自建歌单"), qsTr("收藏歌单"), qsTr("搜索")]
                currentIndex: dialog.picker?.source ?? 0
                onPicked: index => {
                    if (dialog.picker)
                        dialog.picker.source = index;
                }
            }
            RowLayout {
                visible: dialog.picker?.source === 4
                Layout.fillWidth: true
                EchoTextField {
                    id: keyword
                    Layout.fillWidth: true
                    colors: Theme.floating
                    placeholderText: qsTr("搜索歌曲")
                    onAccepted: dialog.picker.search(text)
                    onTextEdited: if (dialog.picker)
                        dialog.picker.invalidateSearch()
                }
                EchoButton {
                    text: qsTr("搜索")
                    onClicked: dialog.picker.search(keyword.text)
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Layout.preferredHeight: 340
                spacing: 14
                ListView {
                    id: playlistList
                    visible: dialog.picker?.source === 2 || dialog.picker?.source === 3
                    Layout.preferredWidth: 180
                    Layout.fillHeight: true
                    clip: true
                    model: dialog.picker?.playlists ?? null
                    delegate: EchoActionButton {
                        id: playlistRow
                        required property var rowData
                        width: playlistList.width
                        height: 48
                        padding: 8
                        hoverEnabled: true
                        background: Rectangle {
                            radius: Theme.cardRadius
                            color: playlistRow.rowData.id === dialog.picker?.playlistId || playlistRow.hovered ? Theme.floating.hover : "transparent"
                        }
                        contentItem: RowLayout {
                            spacing: 8
                            EchoCover {
                                Layout.preferredWidth: 28
                                Layout.preferredHeight: 28
                                source: playlistRow.rowData.cover_url || ""
                            }
                            Label {
                                Layout.fillWidth: true
                                text: playlistRow.rowData.name || ""
                                elide: Text.ElideRight
                                color: Theme.floatingText
                                font: Theme.font(12, 600)
                            }
                        }
                        onClicked: dialog.picker.selectPlaylist(String(rowData.id))
                    }
                    ScrollBar.vertical: EchoPageScrollBar {
                        view: playlistList
                    }
                }
                ListView {
                    id: songList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: dialog.picker?.songs ?? null
                    EchoListInsertMotion {
                        view: songList
                        active: dialog.visible
                        rowHeight: 62
                    }
                    delegate: EchoActionButton {
                        id: songRow
                        required property string resourceId
                        required property string title
                        required property string artistText
                        required property string coverUrl
                        required property bool selected
                        width: songList.width
                        height: 62
                        padding: 8
                        hoverEnabled: true
                        background: Rectangle {
                            radius: Theme.cardRadius
                            color: songRow.hovered || songRow.selected ? Theme.floating.hover : "transparent"
                        }
                        contentItem: RowLayout {
                            spacing: 12
                            EchoCheckBox {
                                visible: dialog.service.room.owner
                                checked: songRow.selected
                                colors: Theme.floating
                                onClicked: dialog.picker.chooseSong(songRow.resourceId)
                            }
                            EchoCover {
                                Layout.preferredWidth: 42
                                Layout.preferredHeight: 42
                                source: songRow.coverUrl
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 4
                                Label {
                                    Layout.fillWidth: true
                                    text: songRow.title
                                    font: Theme.font(12, 750)
                                    color: Theme.floatingText
                                    elide: Text.ElideRight
                                }
                                Label {
                                    Layout.fillWidth: true
                                    text: songRow.artistText
                                    font: Theme.font(10, 600)
                                    color: Theme.floatingSecondaryText
                                    elide: Text.ElideRight
                                }
                            }
                        }
                        onClicked: dialog.picker.chooseSong(resourceId)
                    }
                    Label {
                        anchors.centerIn: parent
                        visible: songList.count === 0
                        text: dialog.picker?.loading ? qsTr("正在加载歌曲…") : dialog.picker?.errorMessage || qsTr("请选择歌曲来源")
                        color: Theme.floatingSecondaryText
                        font: Theme.font(12, 500)
                    }
                    ScrollBar.vertical: EchoPageScrollBar {
                        view: songList
                    }
                }
            }
        }
    }
    footerContent: Component {
        RowLayout {
            Label {
                Layout.fillWidth: true
                text: qsTr("已选择 %1 首").arg(dialog.picker?.songs.selectedCount ?? 0)
                color: Theme.floatingSecondaryText
                font: Theme.font(12, 600)
            }
            EchoButton {
                accent: false
                text: qsTr("取消")
                onClicked: dialog.close()
            }
            EchoButton {
                objectName: "listenPickerSubmit"
                visible: dialog.service.room.owner
                text: dialog.service.room.owner ? qsTr("添加到房间") : qsTr("点歌")
                enabled: (dialog.picker?.songs.selectedCount ?? 0) > 0 && !dialog.service.room.addingSongs
                onClicked: dialog.picker.submit()
            }
        }
    }
}
