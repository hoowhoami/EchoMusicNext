pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Controls
import EchoMusic.Music
import EchoMusic.Theme

Item {
    id: group
    objectName: "recommendationGroupCard"
    required property var entry
    required property SongCatalogActions commands
    property bool resourceActive: true
    readonly property color accent: /^#[0-9a-fA-F]{6}$/.test(entry.color || "") ? entry.color : Theme.accent
    RowLayout {
        anchors.fill: parent
        spacing: 18
        EchoCover {
            objectName: "recommendationGroupArtwork"
            Layout.preferredWidth: Math.min(180, group.width * .36)
            Layout.preferredHeight: Math.min(group.height, information.implicitHeight)
            Layout.alignment: Qt.AlignTop
            source: group.entry.cover_url || ""
            resourceActive: group.resourceActive
        }
        ColumnLayout {
            id: information
            objectName: "recommendationGroupInformation"
            Layout.fillWidth: true
            Layout.fillHeight: false
            Layout.alignment: Qt.AlignTop
            Layout.minimumWidth: 0
            spacing: 9
            EchoTextLine {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                text: group.entry.title || ""
                font: Theme.font(21, Theme.semiboldWeight)
                color: group.accent
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
                            text: previewButton.text
                            font: Theme.font(14, Theme.mediumWeight)
                            color: Theme.text
                        }
                        EchoTextLine {
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            visible: (previewButton.modelData.artist || "").length > 0
                            text: previewButton.modelData.artist || ""
                            font: Theme.font(11, Theme.regularWeight)
                            color: Theme.secondaryText
                        }
                    }
                    onClicked: group.commands.play(modelData.id, false)
                }
            }
        }
    }
}
