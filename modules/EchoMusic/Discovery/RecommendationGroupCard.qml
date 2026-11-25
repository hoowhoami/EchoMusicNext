pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Controls
import EchoMusic.Music
import EchoMusic.Theme
import EchoMusic.Shell
import EchoMusic.Playback

Item {
    id: group
    objectName: "recommendationGroupCard"
    required property int index
    required property DiscoveryService service
    required property RecommendationCard card
    required property RouteController router
    required property var entry
    required property SongCatalogActions commands
    property bool resourceActive: true
    property bool promoteMore: false
    readonly property Action moreAction: Action {
        text: qsTr("更多")
        enabled: (group.entry.song_count || 0) > 0 && group.streamable
        onTriggered: {
            const query = {
                group: String(group.index)
            };
            if (group.card.key === "3103") {
                query.tag = String(group.entry.id);
                query.label = group.entry.title || "";
                query.description = group.entry.description || "";
            }
            group.router.navigate("recommendation-detail", query, group.card.key);
        }
    }
    activeFocusOnTab: true
    readonly property bool streamable: card.key !== "3103" || entry.id !== undefined && entry.id !== null && String(entry.id).length > 0
    readonly property string queueKey: card.key === "3103" ? service.feedQueueKey(3103, String(entry.id)) : card.groupQueueKey(index)
    readonly property bool current: playback.queueCatalog.activeKey === queueKey && playback.queueTitle === (entry.title || "")
    required property PlaybackService playback
    readonly property bool pending: (card.key === "3103" ? service.preparingFeed === queueKey : card.preparingGroup === index) || current && commands.playbackLoading
    HoverHandler {
        id: groupHover
        blocking: false
        enabled: group.resourceActive
    }
    readonly property color accent: /^#[0-9a-fA-F]{6}$/.test(entry.color || "") ? entry.color : Theme.accent
    RowLayout {
        anchors.fill: parent
        spacing: 18
        Item {
            Layout.preferredWidth: Math.min(180, group.width * .36)
            Layout.preferredHeight: Math.min(group.height, information.implicitHeight)
            Layout.alignment: Qt.AlignTop
            EchoCover {
                objectName: "recommendationGroupArtwork"
                anchors.fill: parent
                source: group.entry.cover_url || ""
                resourceActive: group.resourceActive
            }
            EchoPlaybackButton {
                objectName: "recommendationGroupPlay-" + group.card.key + "-" + group.index
                anchors.centerIn: parent
                width: 40
                height: 40
                padding: 11
                foreground: "white"
                playing: group.current && group.commands.playing
                loading: group.pending
                visualContentActive: group.resourceActive
                enabled: !loading && (group.entry.song_count || 0) > 0 && group.streamable
                visible: group.resourceActive && (groupHover.hovered || group.activeFocus || hovered || visualFocus || group.pending)
                background: Rectangle {
                    radius: width / 2
                    color: "#66000000"
                }
                onClicked: {
                    if (group.card.key === "3103")
                        group.service.playFeed(3103, String(group.entry.id), group.entry.title || "", group.entry.description || "", group.entry.start_index || 0, group.entry.song_count || 0);
                    else
                        group.card.playGroup(group.index);
                }
            }
        }
        ColumnLayout {
            id: information
            objectName: "recommendationGroupInformation"
            Layout.fillWidth: true
            Layout.fillHeight: false
            Layout.alignment: Qt.AlignTop
            Layout.minimumWidth: 0
            spacing: 9
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                EchoTextLine {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    text: String(group.entry.title || "").replace(/\s+/g, " ").trim()
                    font: Theme.font(21, Theme.semiboldWeight)
                    color: group.accent
                }
                EchoSoftButton {
                    objectName: "recommendationGroupMore-" + group.card.key + "-" + group.index
                    text: qsTr("更多")
                    Accessible.name: qsTr("更多")
                    glyph: "chevron-right"
                    glyphAfterText: true
                    glyphSize: 14
                    plain: true
                    hoverBackground: false
                    leftPadding: 4
                    rightPadding: 0
                    size: EchoControlSize.Small
                    enabled: group.moreAction.enabled
                    onClicked: group.moreAction.trigger()
                    visible: !group.promoteMore
                }
            }
            Label {
                objectName: "recommendationGroupDescription"
                Layout.fillWidth: true
                text: group.entry.description || ""
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
                elide: Text.ElideRight
                maximumLineCount: 2
                font: Theme.font(13, Theme.regularWeight)
                color: Theme.secondaryText
            }
            Repeater {
                // The domain projects only the first three lightweight labels;
                // complete songs remain in the canonical catalog source.
                model: group.entry.preview || []
                delegate: EchoActionButton {
                    id: previewButton
                    objectName: "recommendationGroupSong"
                    required property var modelData
                    required property int index
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    padding: 0
                    text: modelData.title
                    enabled: String(modelData.id || "").length > 0
                    background: null
                    contentItem: ColumnLayout {
                        spacing: 1
                        EchoTextLine {
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            text: previewButton.text.replace(/\s+/g, " ").trim()
                            font: Theme.font(14, Theme.mediumWeight)
                            color: Theme.text
                        }
                        EchoTextLine {
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            visible: (previewButton.modelData.artist || "").length > 0
                            text: String(previewButton.modelData.artist || "").replace(/\s+/g, " ").trim()
                            font: Theme.font(11, Theme.regularWeight)
                            color: Theme.secondaryText
                        }
                    }
                    onClicked: {
                        if (group.card.key === "3103")
                            group.service.playFeed(3103, String(group.entry.id), group.entry.title || "", group.entry.description || "", group.entry.start_index || 0, group.entry.song_count || 0, previewButton.index);
                        else
                            group.commands.play(modelData.id, false);
                    }
                }
            }
        }
    }
}
