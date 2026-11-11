pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Controls
import EchoMusic.Theme
import EchoMusic.Music

Rectangle {
    id: row
    required property string songId
    required property string title
    required property string artist
    required property string album
    required property string cover
    required property var score
    required property SongCatalogActions actions
    property bool resourceActive: true
    property bool pooled: false
    ListView.onPooled: pooled = true
    ListView.onReused: pooled = false
    signal addSongRequested(string songId)
    readonly property bool favorite: {
        const revision = row.actions.favoriteRevision;
        return row.actions.isFavorite(row.songId);
    }
    implicitHeight: 70
    radius: Theme.itemRadius
    color: Theme.withAlpha(Theme.text, hover.hovered ? 0.06 : 0.03)
    border.width: 1
    border.color: hover.hovered ? Theme.withAlpha(Theme.accent, 0.25) : "transparent"
    HoverHandler {
        id: hover
    }
    TapHandler {
        onDoubleTapped: row.actions.playNow(row.songId)
    }
    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 14
        anchors.rightMargin: 14
        spacing: 14
        EchoCover {
            source: row.cover
            resourceActive: row.resourceActive && !row.pooled
            Layout.preferredWidth: 48
            Layout.preferredHeight: 48
            placeholderGlyph: "microphone"
            placeholderOpacity: 0.2
            placeholderColor: Theme.colors.mutedSurface
        }
        ColumnLayout {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            spacing: 3
            Label {
                Layout.fillWidth: true
                text: row.title
                font: Theme.font(13, Theme.semiboldWeight)
                color: Theme.text
                elide: Text.ElideRight
            }
            Label {
                Layout.fillWidth: true
                text: row.artist + (row.album.length ? " · " + row.album : "")
                font: Theme.font(12, Theme.regularWeight)
                color: Theme.secondaryText
                elide: Text.ElideRight
            }
        }
        Rectangle {
            id: scoreBadge
            readonly property real value: typeof row.score === "number" ? row.score : 0
            implicitWidth: Math.max(76, scoreContent.implicitWidth + 18)
            implicitHeight: scoreContent.implicitHeight + 12
            radius: Theme.detailRadius
            color: Theme.withAlpha(Theme.accent, 0.04 + value * 0.04)
            border.width: 1
            border.color: Theme.withAlpha(Theme.accent, 0.1 + value * 0.08)
            RowLayout {
                id: scoreContent
                anchors.centerIn: parent
                spacing: 5
                Label {
                    text: qsTr("匹配度")
                    font: Theme.font(10, Theme.semiboldWeight)
                    color: Theme.secondaryText
                }
                Label {
                    text: typeof row.score === "number" ? Math.round(row.score * 100) + "%" : "—"
                    font: Theme.spacedFont(12, 750, -0.24)
                    color: Theme.accentText
                }
            }
        }
        RowLayout {
            spacing: 4
            EchoIconButton {
                id: playAction
                glyph: "play"
                implicitWidth: 36
                implicitHeight: 36
                padding: 9.5
                text: qsTr("播放")
                foreground: Theme.accentText
                background: Rectangle {
                    radius: Theme.controlRadius
                    color: Theme.withAlpha(Theme.accent, playAction.hovered ? 0.2 : 0.12)
                }
                onClicked: row.actions.playNow(row.songId)
            }
            EchoIconButton {
                id: favoriteAction
                glyph: row.favorite ? "heart-filled" : "heart"
                implicitWidth: 36
                implicitHeight: 36
                padding: 9.5
                text: row.favorite ? qsTr("取消收藏") : qsTr("收藏")
                foreground: Theme.colors.dangerText
                background: Rectangle {
                    radius: Theme.controlRadius
                    color: Theme.withAlpha(Theme.colors.danger, favoriteAction.hovered ? 0.16 : row.favorite ? 0.12 : 0.08)
                }
                onClicked: row.actions.toggleFavorite(row.songId)
            }
            EchoIconButton {
                id: addAction
                glyph: "playlist-add"
                implicitWidth: 36
                implicitHeight: 36
                padding: 10
                text: qsTr("添加到歌单")
                tooltipText: text
                background: Rectangle {
                    radius: Theme.controlRadius
                    color: Theme.withAlpha(Theme.text, addAction.hovered ? 0.12 : 0.06)
                }
                onClicked: row.addSongRequested(row.songId)
            }
            EchoIconButton {
                id: detailsAction
                glyph: "info-circle"
                implicitWidth: 36
                implicitHeight: 36
                padding: 10
                text: qsTr("歌曲详情")
                tooltipText: text
                background: Rectangle {
                    radius: Theme.controlRadius
                    color: Theme.withAlpha(Theme.text, detailsAction.hovered ? 0.12 : 0.06)
                }
                onClicked: row.actions.openDetails(row.songId)
            }
        }
    }
}
