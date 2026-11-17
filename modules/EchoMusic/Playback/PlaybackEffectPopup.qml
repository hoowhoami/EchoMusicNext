pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Shapes
import EchoMusic.Theme
import EchoMusic.Controls
import EchoMusic.Playback

EchoAnchoredPopup {
    id: popup
    objectName: "playbackEffectPopup"
    required property PlaybackService playback
    readonly property PlaybackAudioControls audio: playback.audioControls
    readonly property AudioEffectCatalog catalog: playback.effectCatalog
    property int tab: 0
    property int libraryTab: 0
    readonly property bool spatialEffectActive: audio.impulseEnabled ? audio.impulseResponses.some(file => file.path === audio.impulseResponse) : audio.providerEffectActive
    preferredWidth: 540
    maximumHeight: 460
    padding: 0
    showArrow: false
    sideOffset: 8
    preferAbove: true
    alignEnd: false
    contentItem: Item {
        implicitHeight: 460
        clip: true
        Item {
            id: sidebar
            objectName: "effectSidebar"
            width: 88
            height: parent.height
            // Clip to the popover's rounded outline.
            // A single path avoids alpha seams from overlapping rectangles.
            Shape {
                anchors.fill: parent
                ShapePath {
                    strokeWidth: 0
                    fillColor: Theme.floating.mutedSurface
                    PathRectangle {
                        width: sidebar.width
                        height: sidebar.height
                        topLeftRadius: Theme.popoverRadius
                        bottomLeftRadius: Theme.popoverRadius
                        topRightRadius: 0
                        bottomRightRadius: 0
                    }
                }
            }
            EchoDivider {
                anchors.right: parent.right
                height: parent.height
                orientation: Qt.Vertical
                lineColor: Theme.floating.subtleBorder
            }
            Column {
                x: 6
                y: 12
                width: 76
                spacing: 4
                Repeater {
                    model: [qsTr("歌曲音效"), qsTr("均衡器"), qsTr("音效"), qsTr("音效广场")]
                    delegate: EchoActionButton {
                        id: tabButton
                        required property string modelData
                        required property int index
                        objectName: "effectTab-" + index
                        width: 76
                        height: 32
                        text: modelData
                        font: Theme.font(13, Theme.semiboldWeight)
                        hoverEnabled: true
                        padding: 0
                        Accessible.role: Accessible.PageTab
                        Accessible.selected: popup.tab === index
                        contentItem: Item {
                            EchoTextTagRow {
                                anchors.centerIn: parent
                                text: tabButton.text
                                font: tabButton.font
                                lineHeight: 20
                                color: popup.tab === tabButton.index ? Theme.accentForeground : tabButton.hovered ? Theme.floating.text : Theme.floating.secondaryText
                            }
                            EchoStatusDot {
                                objectName: "effectSidebarCurrentDot"
                                visible: tabButton.index === 2 && popup.spatialEffectActive
                                anchors.right: parent.right
                                anchors.rightMargin: 8
                                anchors.verticalCenter: parent.verticalCenter
                                width: 5
                                height: 5
                                color: popup.tab === 2 ? "white" : Theme.floating.accent
                                haloColor: Theme.withAlpha(color, popup.tab === 2 ? 0.18 : 0.14)
                            }
                        }
                        background: Rectangle {
                            radius: Theme.itemRadius
                            color: popup.tab === tabButton.index ? Theme.floating.accent : tabButton.hovered ? Theme.floating.hover : "transparent"
                            EchoDashedFrame {
                                anchors.fill: parent
                                solid: true
                                strokeColor: Theme.floating.accent
                                radius: Theme.itemRadius
                                thickness: 2
                                visible: tabButton.visualFocus
                            }
                        }
                        onClicked: {
                            popup.tab = index;
                        }
                    }
                }
            }
        }
        Item {
            x: 88
            width: parent.width - 88
            height: parent.height
            Item {
                id: heading
                width: parent.width
                height: 48
                visible: popup.tab !== 3
                EchoTextTagRow {
                    x: 16
                    anchors.verticalCenter: parent.verticalCenter
                    objectName: "playbackEffectHeading"
                    text: popup.tab === 0 ? qsTr("歌曲音效") : popup.tab === 1 ? qsTr("自定义调节") : qsTr("音效")
                    font: Theme.font(14, Theme.boldWeight)
                    lineHeight: 21
                    color: Theme.floating.text
                }
                EchoButton {
                    size: EchoControlSize.Compact
                    accent: false
                    colors: Theme.floating
                    anchors.right: parent.right
                    anchors.rightMargin: 16
                    anchors.verticalCenter: parent.verticalCenter
                    implicitHeight: 32
                    enabled: !equalizer.locked
                    visible: popup.tab === 1
                    text: qsTr("重置")
                    onClicked: popup.audio.resetEqualizer()
                }
            }
            PlaybackSongEffectPane {
                y: 48
                width: parent.width
                height: parent.height - y
                visible: popup.tab === 0
                playback: popup.playback
                surface: popup
            }
            PlaybackEqualizerPane {
                id: equalizer
                y: 48
                width: parent.width
                height: parent.height - y
                visible: popup.tab === 1
                audio: popup.audio
            }
            PlaybackImpulseEffectPane {
                y: 48
                width: parent.width
                height: parent.height - y
                visible: popup.tab === 2
                audio: popup.audio
                surface: popup
                libraryTab: popup.libraryTab
                onLibraryPicked: index => popup.libraryTab = index
                onPlazaRequested: source => {
                    popup.tab = 3;
                    popup.catalog.selectCategory(source);
                }
                uiScale: popup.uiScale
            }
            PlaybackEffectPlaza {
                anchors.fill: parent
                visible: popup.tab === 3
                resourceActive: popup.visible && visible
                catalog: popup.catalog
                surface: popup
            }
            Label {
                anchors.bottom: parent.bottom
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.margins: 12
                visible: popup.tab === 1 && popup.audio.errorMessage.length > 0
                text: popup.audio.errorMessage
                color: Theme.floating.dangerText
                font: Theme.font(11, Theme.regularWeight)
                wrapMode: Text.Wrap
            }
        }
    }
}
