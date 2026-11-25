pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Shell
import EchoMusic.Controls
import EchoMusic.Music
import EchoMusic.Theme

RoutePage {
    id: page
    objectName: "artistRecommendationsPage"
    required property ExploreService service
    EchoPageScrollContainer {
        id: viewport
        anchors.fill: parent
        pageActive: page.pageActive
        dataRecovering: page.service.artists.cacheRecovering
        scrollView: directory
        stickyHeaders: [heading]
        ArtistDirectory {
            id: directory
            objectName: "exploreArtistDirectory"
            x: 40
            width: Math.max(0, parent.width - 80 + 20)
            height: parent.height
            topMargin: 168
            bottomMargin: 40
            collection: page.service.artists
            errorMessage: page.service.errorMessage
            avatarLimit: 20
            pageContainer: viewport
            pageActive: page.pageActive
            navigationAvailable: page.router.available("artist-detail")
            onOpenRequested: id => page.router.navigate("artist-detail", {}, id)
            navigationHeader: Component {
                Flickable {
                    height: 36
                    width: directory.width - 20
                    contentWidth: letters.implicitWidth
                    clip: true
                    Row {
                        id: letters
                        spacing: 2
                        Repeater {
                            model: [qsTr("全部")].concat(page.service.artistLetters)
                            delegate: EchoSoftButton {
                                required property string modelData
                                text: modelData
                                size: EchoControlSize.Small
                                accent: page.service.artistLetter === modelData
                                onClicked: page.service.selectLetter(modelData)
                            }
                        }
                    }
                }
            }
            emptyFooter: Component {
                ColumnLayout {
                    width: directory.width - 20
                    height: 220
                    Label {
                        Layout.alignment: Qt.AlignHCenter
                        text: page.service.loading ? qsTr("正在发现歌手…") : page.service.errorMessage || (page.service.sex !== "0" || page.service.artistType !== "0:0" || page.service.artists.totalCount > 0 ? qsTr("当前筛选下暂无歌手") : qsTr("暂无歌手"))
                        font: Theme.font(13, Theme.regularWeight)
                        color: Theme.secondaryText
                    }
                    EchoSoftButton {
                        Layout.alignment: Qt.AlignHCenter
                        visible: page.service.errorMessage.length > 0
                        text: qsTr("重试")
                        onClicked: page.service.retry()
                    }
                }
            }
        }
        EchoPageStickyHeader {
            id: heading
            container: viewport
            flowPosition: 16
            height: 136
            ExploreHeader {
                anchors.fill: parent
                service: page.service
                onPickRequested: kind => picker.open({
                        kind: kind
                    })
            }
        }
    }
    EchoPopupLoader {
        id: picker
        hostItem: page
        sourceComponent: EchoOptionPicker {
            property string kind: "sex"
            uiScale: page.settings.uiScale
            heading: kind === "sex" ? qsTr("性别筛选") : qsTr("类型筛选")
            preferredWidth: 360
            groups: [
                {
                    label: "",
                    options: kind === "sex" ? page.service.sexFilters : page.service.typeFilters
                }
            ]
            selectedId: kind === "sex" ? page.service.sex : page.service.artistType
            onChosen: id => {
                if (kind === "sex")
                    page.service.selectSex(id);
                else
                    page.service.selectArtistType(id);
            }
        }
    }
}
