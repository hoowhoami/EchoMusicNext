pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Controls
import EchoMusic.Theme

// One metadata recipe for album artists, playlist publishers and artist facts.
Item {
    id: metadata
    property var authors: []
    property url avatarSource: ""
    property string secondaryText: ""
    property var labels: []
    property var labelObjectNames: []
    property bool authorLinks: false
    property bool compact: false
    property bool resourceActive: true
    property string leadingText: ""
    property string leadingObjectName: ""
    property string rowObjectName: ""
    property string avatarObjectName: ""
    property string authorObjectName: ""
    property string secondaryObjectName: ""
    property string labelsObjectName: ""
    property string labelObjectName: ""
    signal authorActivated(string id)
    implicitHeight: row.implicitHeight
    RowLayout {
        id: row
        objectName: metadata.rowObjectName
        width: Math.min(parent.width, implicitWidth)
        height: implicitHeight
        spacing: metadata.compact ? 4 : 12
        Loader {
            active: metadata.leadingText.length > 0
            visible: active
            sourceComponent: EchoTextTagRow {
                objectName: metadata.leadingObjectName
                text: metadata.leadingText
                font: Theme.font(11, Theme.semiboldWeight)
                color: Theme.secondaryText
                lineHeight: 20
            }
        }
        RowLayout {
            visible: metadata.authors.length > 0
            Layout.fillWidth: true
            Layout.minimumWidth: 20
            Layout.maximumWidth: implicitWidth
            spacing: 8
            EchoCover {
                objectName: metadata.avatarObjectName
                Layout.preferredWidth: 20
                Layout.preferredHeight: 20
                circular: true
                source: metadata.avatarSource
                placeholderGlyph: "user"
                placeholderGlyphSize: 12
                resourceActive: metadata.resourceActive
            }
            RowLayout {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                spacing: metadata.compact ? 2 : 4
                Repeater {
                    model: metadata.authors
                    delegate: RowLayout {
                        id: author
                        required property var modelData
                        required property int index
                        readonly property string authorId: String(modelData.id || "")
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        spacing: metadata.compact ? 2 : 4
                        Loader {
                            Layout.fillWidth: true
                            Layout.minimumWidth: metadata.authorLinks ? 20 : 0
                            sourceComponent: metadata.authorLinks ? link : caption
                            Component {
                                id: caption
                                EchoTextTagRow {
                                    objectName: metadata.authorObjectName
                                    text: String(author.modelData.name || "")
                                    font: Theme.font(13, Theme.semiboldWeight)
                                    color: Theme.accentText
                                    lineHeight: 20
                                    alignFontBody: true
                                }
                            }
                            Component {
                                id: link
                                Button {
                                    objectName: metadata.authorObjectName
                                    flat: true
                                    padding: 0
                                    background: null
                                    enabled: author.authorId.length > 0 && author.authorId !== "0"
                                    Accessible.name: String(author.modelData.name || "")
                                    onClicked: metadata.authorActivated(author.authorId)
                                    contentItem: EchoTextTagRow {
                                        text: String(author.modelData.name || "")
                                        font: Theme.font(13, Theme.semiboldWeight)
                                        color: Theme.accentText
                                        lineHeight: 20
                                        alignFontBody: true
                                    }
                                    HoverHandler {
                                        enabled: parent.enabled
                                        cursorShape: Qt.PointingHandCursor
                                    }
                                }
                            }
                        }
                        EchoTextTagRow {
                            visible: author.index < metadata.authors.length - 1
                            text: "/"
                            font: Theme.font(13, Theme.semiboldWeight)
                            color: Theme.secondaryText
                            lineHeight: 20
                            alignFontBody: true
                        }
                    }
                }
            }
        }
        EchoTextTagRow {
            objectName: metadata.secondaryObjectName
            visible: text.length > 0
            Layout.fillWidth: metadata.authors.length === 0
            Layout.minimumWidth: metadata.authors.length > 0 ? Math.ceil(implicitWidth) : 0
            Layout.preferredWidth: Math.ceil(implicitWidth)
            Layout.maximumWidth: Math.ceil(implicitWidth)
            text: metadata.secondaryText
            font: Theme.font(11, Theme.semiboldWeight)
            color: Theme.secondaryText
            lineHeight: 20
        }
        RowLayout {
            objectName: metadata.labelsObjectName
            visible: metadata.labels.length > 0
            spacing: metadata.compact ? 4 : 8
            Repeater {
                model: metadata.labels
                delegate: EchoTag {
                    required property string modelData
                    required property int index
                    objectName: metadata.labelObjectNames[index] || metadata.labelObjectName
                    text: modelData
                    tone: EchoTag.Accent
                }
            }
        }
    }
}
