pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Theme
import EchoMusic.Controls

ItemDelegate {
    id: row
    EchoMotionScope {
        id: motionScope
        target: row
        active: row.resourceActive && (!row.ListView.view || row.y + row.height > row.ListView.view.contentY && row.y < row.ListView.view.contentY + row.ListView.view.height)
        animations: [removal, rowFill]
    }
    // Keep the departing delegate only for its finite native remove phase.
    // No business operation waits for the visual, including history deletion.
    Connections {
        target: row.ListView
        function onRemove(): void {
            if (motionScope.allowed) {
                row.ListView.delayRemove = true;
                removal.start();
            }
        }
        function onReused(): void {
            removal.stop();
            row.opacity = 1;
            row.scale = 1;
        }
    }
    ParallelAnimation {
        id: removal
        onStopped: row.ListView.delayRemove = false
        NumberAnimation {
            target: row
            property: "opacity"
            to: 0
            duration: Theme.songRemoveDuration
            easing.type: Easing.BezierSpline
            easing.bezierCurve: Theme.easeSongRemove
        }
        NumberAnimation {
            target: row
            property: "scale"
            to: 0.85
            duration: Theme.songRemoveDuration
            easing.type: Easing.BezierSpline
            easing.bezierCurve: Theme.easeSongRemove
        }
    }
    EchoUiPart {
        id: publicRow
        target: row
        role: "song.row"
        key: row.resourceId
        active: row.resourceActive
        properties: ["foreground", "secondaryForeground", "background", "radius", "borderColor", "borderWidth", "font", "fontSize", "fontWeight", "letterSpacing"]
        actions: ["play", "details", "favorite", "album"]
        onActionRequested: (action, arguments) => {
            if (action === "play" && row.playbackAvailable && !row.pendingPlay)
                row.playRequested(row.resourceId);
            else if (action === "details")
                row.detailsRequested(row.resourceId);
            else if (action === "favorite" && row.actionsAvailable)
                row.favoriteRequested(row.resourceId);
            else if (action === "album" && row.albumId.length)
                row.albumRequested(row.albumId);
        }
    }
    objectName: "songDelegate"
    required property int index
    required property SongListGeometry geometry
    required property string title
    required property string artistText
    required property string albumText
    required property string coverUrl
    required property string durationText
    required property string extraText
    required property string resourceId
    required property var artistLinks
    required property string albumId
    signal artistRequested(string id)
    signal albumRequested(string id)
    property bool lyricColumn: false
    property bool showAlbum: true
    property bool resourceActive: true
    property bool playbackAvailable: false
    property bool actionsAvailable: false
    property bool favorite: false
    property bool active: false
    property bool playing: false
    property bool playbackLoading: false
    readonly property bool pendingPlay: active && playbackLoading
    property bool catalogPlayable: true
    // The context target keeps a distinct background until its menu closes.
    // This fill takes precedence over the current-song selection background.
    property bool contextTarget: false
    property bool original: false
    property bool showOriginal: false
    property var privilegeTags: []
    property string qualityLabel: ""
    signal favoriteRequested(string id)
    signal detailsRequested(string id)
    signal commentsRequested(string id)
    signal mvRequested(string id)
    // Context menus support pointer, Menu and Shift+F10 invocation.
    // Inline icons keep the MV, comments and favorite order.
    signal moreRequested(string id, real sceneX, real sceneY)
    signal playRequested(string id)
    Keys.onPressed: event => {
        if (event.key === Qt.Key_Menu || (event.key === Qt.Key_F10 && (event.modifiers & Qt.ShiftModifier))) {
            const point = row.mapToItem(null, row.width, row.height);
            row.moreRequested(row.resourceId, point.x, point.y);
            event.accepted = true;
        }
    }
    component RowAction: EchoIconButton {
        visualContentActive: row.resourceActive && row.visible && (opacity > 0 || activeFocus)
        foreground: hoverHighlighted || visualFocus ? Theme.accentText : Theme.secondaryText
        scaleFeedback: true
    }
    height: 60
    padding: 0
    hoverEnabled: true
    highlighted: active
    Accessible.selected: active
    Accessible.name: (index + 1) + " " + title + " " + artistText
    background: Rectangle {
        radius: publicRow.value("radius", Theme.controlRadius)
        color: publicRow.value("background", row.contextTarget ? Theme.colors.hover : row.active ? Theme.selected : row.hovered || row.visualFocus ? Theme.rowHover : "transparent")
        border.color: publicRow.value("borderColor", "transparent")
        border.width: publicRow.value("borderWidth", 0)
        Behavior on color {
            enabled: motionScope.allowed
            ColorAnimation {
                id: rowFill
                duration: Theme.fastDuration
                easing.type: Easing.BezierSpline
                easing.bezierCurve: Theme.easeStandard
            }
        }
    }
    contentItem: RowLayout {
        EchoUiColumnGroup.controller: row.geometry
        EchoUiColumnGroup.keys: row.geometry.uiColumnKeys
        EchoUiColumnGroup.rowIndex: row.index
        EchoUiColumnGroup.active: row.resourceActive
        EchoUiColumnGroup.row: ({
                resourceId: row.resourceId,
                title: row.title,
                artistText: row.artistText,
                albumText: row.albumText,
                albumId: row.albumId,
                durationText: row.durationText,
                coverUrl: row.coverUrl
            })
        spacing: 0
        Item {
            objectName: "songIndexColumn"
            Layout.preferredWidth: row.geometry.indexWidth
            Layout.minimumWidth: row.geometry.indexWidth
            Layout.maximumWidth: row.geometry.indexWidth
            Layout.fillHeight: true
            Text {
                objectName: "songIndexText"
                // Center ordinary ordinals on the reference's 16px play slot;
                // long ordinals grow rightwards without crossing the row edge.
                x: Math.max(0, 16 - implicitWidth / 2)
                anchors.verticalCenter: parent.verticalCenter
                text: row.index + 1
                textFormat: Text.PlainText
                color: publicRow.value("secondaryForeground", Theme.secondaryText)
                font: publicRow.styledFont(row.geometry.numberFont)
                visible: !row.hovered && !row.visualFocus && !row.active
            }
            EchoPlaybackButton {
                objectName: "songIndexPlay"
                visualContentActive: row.resourceActive && visible
                scaleFeedback: true
                hoverBackground: false
                x: 1
                anchors.verticalCenter: parent.verticalCenter
                width: 30
                height: 30
                padding: 8
                playing: row.active && row.playing
                loading: row.pendingPlay
                foreground: row.active || hoverHighlighted || visualFocus ? Theme.accentText : Theme.iconMain
                visible: row.hovered || row.visualFocus || row.active || row.pendingPlay
                enabled: row.playbackAvailable && (row.active || row.catalogPlayable) && !loading
                onClicked: row.playRequested(row.resourceId)
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            spacing: 12
            EchoCover {
                coverScope: "song"
                objectName: "songCover"
                Layout.preferredWidth: 46
                Layout.preferredHeight: 46
                source: row.coverUrl
                resourceActive: row.resourceActive
            }
            ColumnLayout {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                spacing: 2
                EchoTextTagRow {
                    objectName: "songTitleRow"
                    textItem.objectName: "songTitleText"
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    text: row.title
                    font: publicRow.styledFont(Theme.spacedFont(13, Theme.semiboldWeight, -0.2))
                    color: publicRow.value("foreground", row.active ? Theme.accentText : Theme.text)
                    spacing: 6
                    Repeater {
                        model: row.privilegeTags
                        delegate: EchoTag {
                            required property var modelData
                            text: modelData.label
                            tone: EchoTag.Custom
                            customTone: modelData.color
                        }
                    }
                    Loader {
                        active: row.qualityLabel.length > 0
                        visible: active
                        sourceComponent: EchoTag {
                            text: row.qualityLabel
                            tone: EchoTag.Custom
                            customTone: "#06B6D4"
                        }
                    }
                    Loader {
                        active: row.showOriginal && row.original
                        visible: active
                        sourceComponent: EchoTag {
                            objectName: "songOriginalTag"
                            text: qsTr("原唱")
                            tone: EchoTag.Custom
                            customTone: "#F59E0B"
                        }
                    }
                }
                EchoArtistLinks {
                    objectName: "songArtistRow"
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    textObjectName: "songArtistText"
                    artists: row.artistLinks
                    onArtistRequested: id => row.artistRequested(id)
                    color: publicRow.value("secondaryForeground", row.active ? Theme.accentText : Theme.secondaryText)
                    font: publicRow.styledFont(Theme.font(12, Theme.mediumWeight))
                }
            }
            RowLayout {
                id: rowActions
                readonly property bool focusWithin: mvAction.activeFocus || detailsAction.activeFocus || favoriteAction.activeFocus
                Layout.rightMargin: 10
                spacing: 6
                RowAction {
                    id: mvAction
                    objectName: "songMvAction"
                    glyph: "brand-youtube"
                    text: qsTr("播放 MV")
                    tooltipText: text
                    Layout.preferredWidth: 28
                    Layout.preferredHeight: 28
                    padding: 6
                    hoverBackground: false
                    visible: Number(row.resourceId) > 0
                    opacity: row.hovered || row.visualFocus || rowActions.focusWithin ? 1 : 0
                    onClicked: row.mvRequested(row.resourceId)
                }
                RowAction {
                    id: detailsAction
                    objectName: "songDetailsAction"
                    glyph: "message-circle"
                    text: qsTr("评论")
                    tooltipText: text
                    Layout.preferredWidth: 28
                    Layout.preferredHeight: 28
                    padding: 6
                    hoverBackground: false
                    enabled: row.resourceId.length > 0
                    opacity: row.hovered || row.visualFocus || rowActions.focusWithin ? 1 : 0
                    onClicked: row.commentsRequested(row.resourceId)
                }
                RowAction {
                    id: favoriteAction
                    objectName: "songFavoriteAction"
                    glyph: row.favorite ? "heart-filled" : "heart"
                    text: qsTr("收藏")
                    Layout.preferredWidth: 28
                    Layout.preferredHeight: 28
                    padding: 6
                    enabled: row.actionsAvailable
                    // The favorite heart stays red in both states, with a transparent hover surface.
                    foreground: hovered ? "#dc2626" : "#ef4444"
                    hoverBackground: false
                    onClicked: row.favoriteRequested(row.resourceId)
                }
            }
        }
        AbstractButton {
            id: albumLink
            objectName: "songAlbumLink"
            visible: row.showAlbum
            Layout.preferredWidth: row.lyricColumn ? row.geometry.lyricWidth : row.geometry.albumWidth
            Layout.preferredHeight: albumLabel.implicitHeight
            text: row.lyricColumn ? row.extraText : row.albumText.trim().length ? row.albumText : "-"
            padding: 0
            rightPadding: 12
            enabled: !row.lyricColumn && row.albumId.length > 0 && row.albumText.trim().length > 0
            focusPolicy: enabled ? Qt.StrongFocus : Qt.NoFocus
            hoverEnabled: true
            background: null
            contentItem: Text {
                id: albumLabel
                objectName: "songAlbumText"
                text: albumLink.text
                textFormat: Text.PlainText
                color: publicRow.value("secondaryForeground", row.active || albumLink.hovered || albumLink.visualFocus ? Theme.accentText : Theme.secondaryText)
                font: publicRow.styledFont(Theme.font(13, Theme.regularWeight))
                elide: Text.ElideRight
            }
            HoverHandler {
                cursorShape: Qt.PointingHandCursor
                enabled: albumLink.enabled
            }
            onClicked: row.albumRequested(row.albumId)
        }
        Text {
            objectName: "songDurationText"
            Layout.preferredWidth: row.geometry.durationWidth
            leftPadding: 8
            text: row.durationText
            color: publicRow.value("secondaryForeground", row.active ? Theme.accentText : Theme.secondaryText)
            font: publicRow.styledFont(Theme.font(12, Theme.regularWeight))
        }
    }
}
