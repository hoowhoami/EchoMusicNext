pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import EchoMusic.Controls
import EchoMusic.Shell
import EchoMusic.Theme

// Supplement the existing song header with the same label/value typography
// and soft surface used by the application's detail and profile sections.
ColumnLayout {
    id: section
    required property SongDetailContext detailContext
    required property RouteController router
    property ThemeColors colors: Theme.colors
    property bool compact: false
    signal navigated
    readonly property var information: detailContext.information
    objectName: "songInformationSection"
    spacing: 12

    function text(value: var): string {
        return value === undefined || value === null ? "" : String(value).trim();
    }
    function fieldValue(key: string): string {
        if (key === "artist")
            return text(detailContext.detail.artist);
        if (key === "album")
            return text(detailContext.detail.album);
        if (key === "duration") {
            const milliseconds = Number(information.duration_ms);
            if (!Number.isFinite(milliseconds) || milliseconds <= 0)
                return "";
            const seconds = Math.floor(milliseconds / 1000);
            return Math.floor(seconds / 60) + ":" + String(seconds % 60).padStart(2, "0");
        }
        if (key === "tempo") {
            const description = text(information.tempo_description);
            const bpm = Number(information.bpm);
            const tempo = Number.isFinite(bpm) && bpm > 0 ? bpm + " BPM" : "";
            return description ? description + (tempo && !/bpm/i.test(description) ? " · " + tempo : "") : tempo;
        }
        return text(information[key]);
    }
    EchoTextLine {
        Layout.fillWidth: true
        text: qsTr("歌曲资料")
        font: Theme.font(14, Theme.boldWeight)
        color: section.colors.text
    }
    Rectangle {
        id: informationCard
        Layout.fillWidth: true
        readonly property real contentInset: section.compact ? 16 : 20
        implicitHeight: fields.implicitHeight + contentInset * 2
        color: section.colors.mutedSurface
        radius: Theme.cardRadius
        GridLayout {
            id: fields
            x: informationCard.contentInset
            y: informationCard.contentInset
            width: informationCard.width - informationCard.contentInset * 2
            columns: section.compact ? width < 320 ? 1 : 2 : width < 480 ? 1 : width < 760 ? 2 : 3
            uniformCellWidths: true
            columnSpacing: section.compact ? 20 : 24
            rowSpacing: section.compact ? 16 : 20
            Repeater {
                model: [
                    {
                        key: "artist",
                        label: qsTr("歌手")
                    },
                    {
                        key: "album",
                        label: qsTr("专辑")
                    },
                    {
                        key: "lyricist",
                        label: qsTr("作词")
                    },
                    {
                        key: "composer",
                        label: qsTr("作曲")
                    },
                    {
                        key: "published_at",
                        label: qsTr("发行日期")
                    },
                    {
                        key: "tempo",
                        label: qsTr("节奏")
                    },
                    {
                        key: "duration",
                        label: qsTr("时长")
                    },
                    {
                        key: "language",
                        label: qsTr("语种")
                    },
                    {
                        key: "album_published_at",
                        label: qsTr("专辑发行"),
                        optional: true
                    }
                ]
                delegate: ColumnLayout {
                    id: field
                    required property var modelData
                    readonly property string value: section.fieldValue(modelData.key)
                    objectName: "songInformation-" + modelData.key
                    visible: !modelData.optional || value.length > 0 && value !== section.text(section.information.published_at)
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    Layout.alignment: Qt.AlignTop
                    spacing: section.compact ? 6 : 8
                    EchoTextLine {
                        Layout.fillWidth: true
                        text: field.modelData.label
                        color: section.colors.secondaryText
                        font: Theme.font(12, Theme.regularWeight)
                        lineHeight: 18
                    }
                    Loader {
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        sourceComponent: field.modelData.key === "artist" && section.detailContext.artistLinks.some(artist => String(artist.name || "").trim().length > 0) ? artistValue : field.modelData.key === "album" && section.detailContext.albumNavigationId.length > 0 ? albumValue : plainValue
                        Component {
                            id: plainValue
                            EchoTextLine {
                                objectName: "songInformationValue-" + field.modelData.key
                                text: field.value || (section.detailContext.loading ? qsTr("加载中…") : "—")
                                color: field.value ? section.colors.text : section.colors.secondaryText
                                font: Theme.font(13, Theme.mediumWeight)
                                lineHeight: 19.5
                                wrap: true
                                fillTextWidth: true
                            }
                        }
                    }
                }
            }
        }
    }
    Component {
        id: artistValue
        EchoArtistLinks {
            objectName: "songInformationArtists"
            artists: section.detailContext.artistLinks
            font: Theme.font(13, Theme.mediumWeight)
            lineHeight: 19.5
            color: section.colors.accentText
            requireCompleteName: true
            onArtistRequested: id => {
                if (section.router.navigate("artist-detail", {}, id))
                    section.navigated();
            }
        }
    }
    Component {
        id: albumValue
        EchoSoftButton {
            id: album
            objectName: "songInformationAlbum"
            text: section.text(section.detailContext.detail.album) || qsTr("单曲")
            plain: true
            leftPadding: 0
            rightPadding: 0
            implicitHeight: Math.max(19.5, contentItem.implicitHeight)
            font: Theme.font(13, Theme.mediumWeight)
            foreground: section.colors.accentText
            contentItem: EchoButtonContent {
                text: album.text
                wrap: true
                horizontalAlignment: Qt.AlignLeft
                font: album.font
                lineHeight: 19.5
                color: album.foreground
            }
            onClicked: {
                if (section.router.navigate("album-detail", {}, section.detailContext.albumNavigationId))
                    section.navigated();
            }
        }
    }
}
