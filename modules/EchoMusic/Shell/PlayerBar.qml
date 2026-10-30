pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import EchoMusic.Theme
import EchoMusic.Controls
import EchoMusic.Shell
import EchoMusic.Playback

Item {
    id: bar
    AppearanceHost {
        anchors.fill: parent
        entry: Theme.store.extensions.theme
        placement: "playerbar"
        z: 0
    }
    EchoUiPart {
        id: publicSurface
        target: bar
        role: "shell.playerbar"
        key: "shell.playerbar"
        properties: ["background", "borderColor", "borderWidth", "radius", "opacity"]
    }
    opacity: publicSurface.value("opacity", 1)
    Loader {
        anchors.fill: parent
        z: -0.5
        active: bar.visible && bar.Window.window !== null && bar.Window.window.visible && bar.Window.window.visibility !== Window.Minimized && Object.keys(publicSurface.overrides).some(name => name !== "opacity")
        sourceComponent: Rectangle {
            color: publicSurface.value("background", "transparent")
            radius: publicSurface.value("radius", 0)
            border.color: publicSurface.value("borderColor", "transparent")
            border.width: publicSurface.value("borderWidth", 0)
        }
    }
    objectName: "playerbar"
    required property ShellController controller
    required property RouteController router
    required property real uiScale
    signal playbackPageRequested
    readonly property alias coverItem: playerCover
    property Item lastActionAnchor: bar
    property bool lyricStyle: false
    // Decorations above the bar occupy page space even though they render
    // outside its fixed control geometry. The composition root forwards this.
    readonly property real aboveControlsHeight: lyricStyle ? aboveControls.height : 0
    property bool manageCapacity: true
    property bool retiring: false
    property ThemeColors colors: Theme.colors
    readonly property PlaybackService playback: controller.playback as PlaybackService
    readonly property ShellActionModel actions: controller.playerbarActions
    // PlayerBar: flex 1 / 1.5 / 1 with capped sides. LyricPlayerControls:
    // minmax(0, 1fr) / intrinsic controls / minmax(0, 1fr), uncapped.
    readonly property real sideWidth: lyricStyle ? Math.max(0, (width - 56 - centerActions.implicitWidth) / 2) : Math.min(320, Math.max(120, (width - 60) / 3.5))
    implicitHeight: 88
    function updateCapacity() {
        if (retiring || !manageCapacity || !visible || width <= 0)
            return;
        actions.setCapacity(ShellActionModel.Left, Math.max(0, Math.floor((metadata.width - (lyricStyle ? 36 : 44)) / 28)));
        actions.setCapacity(ShellActionModel.Center, lyricStyle ? 2147483647 : Math.max(0, Math.floor(center.width / 36)));
        // This region already excludes the center. Reserve only the More button,
        // right inset and actual 4px strip gaps.
        actions.setCapacity(ShellActionModel.Right, Math.max(0, Math.floor((trailing.width - rightRow.anchors.rightMargin - more.implicitWidth) / (36 + rightActions.spacing))));
    }
    Component.onCompleted: {
        if (lyricStyle)
            controller.setLyricPageActive(visible);
        updateCapacity();
    }
    Component.onDestruction: {
        retiring = true;
        capacityUpdate.stop();
        if (lyricStyle)
            controller.setLyricPageActive(false);
    }
    onVisibleChanged: {
        if (retiring)
            return;
        if (lyricStyle)
            controller.setLyricPageActive(visible);
        // The lyric center is max-content, independent of the narrower shell
        // bar's previous overflowOwner.item projection. Only the visible bar owns capacity.
        if (visible && lyricStyle && manageCapacity)
            actions.setCapacity(ShellActionModel.Center, 2147483647);
        capacityUpdate.restart();
    }
    onManageCapacityChanged: capacityUpdate.restart()
    PluginSurfaceHost {
        anchors.fill: parent
        model: bar.controller.pluginSurfaces
        target: "playerbar.background"
        visible: !bar.lyricStyle
        clip: true
    }
    PluginSurfaceHost {
        id: aboveControls
        objectName: "playerbarAboveControls"
        anchors.bottom: parent.top
        width: parent.width
        height: implicitHeight
        model: bar.controller.pluginSurfaces
        target: "lyrics.aboveControls"
        visible: bar.lyricStyle
    }
    PlaybackProgress {
        visible: bar.lyricStyle
        anchors.top: parent.top
        width: parent.width
        height: 16
        edgeToEdge: true
        colors: bar.colors
        playback: bar.playback
    }
    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: bar.lyricStyle ? 16 : 18
        anchors.rightMargin: bar.lyricStyle ? 16 : 18
        anchors.topMargin: bar.lyricStyle ? 16 : 10
        anchors.bottomMargin: bar.lyricStyle ? 0 : 10
        spacing: 12
        RowLayout {
            objectName: "playerbarLeftArea"
            Layout.alignment: Qt.AlignVCenter
            Layout.fillHeight: false
            // Both side regions reserve equal space; Qt must not expand the
            // metadata layout and take width from the centered playback area.
            Layout.fillWidth: false
            Layout.preferredWidth: bar.sideWidth
            Layout.minimumWidth: bar.lyricStyle ? 0 : 120
            Layout.maximumWidth: bar.lyricStyle ? Infinity : 320
            spacing: 12
            EchoCover {
                id: playerCover
                coverScope: "playerbar"
                visible: !bar.lyricStyle
                Layout.preferredWidth: 56
                Layout.preferredHeight: 56
                source: bar.playback ? bar.playback.cover : ""
                // The same current-track artwork supplies the playback-page
                // flight. Decode for its maximum 380 DIP destination once.
                minimumDecodeSize: 380
                colors: bar.colors
                placeholderColor: bar.colors.mutedSurface
                placeholderGlyphSize: 24
                resourceActive: bar.visible
                Accessible.role: Accessible.Button
                Accessible.name: qsTr("打开播放页")
                Accessible.onPressAction: bar.playbackPageRequested()
                TapHandler {
                    onTapped: bar.playbackPageRequested()
                }
            }
            Item {
                id: metadata
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                Layout.preferredHeight: bar.lyricStyle ? 60 : 64
                onWidthChanged: capacityUpdate.restart()
                PlayerSongText {
                    objectName: "playerbarSongText"
                    y: 4
                    width: parent.width
                    height: bar.lyricStyle ? 20 : 24
                    playback: bar.playback
                    colors: bar.colors
                    lyricStyle: bar.lyricStyle
                    onArtistRequested: id => bar.router.navigate("artist-detail", {}, id)
                    onAlbumRequested: id => bar.router.navigate("album-detail", {}, id)
                    onCopyRequested: bar.controller.copyCurrentSongInfo()
                }
                PlayerActionStrip {
                    objectName: "playerbarLeftActions"
                    y: bar.lyricStyle ? 28 : 34
                    width: implicitWidth
                    height: 28
                    compact: true
                    // Compact slots are 28 DIP with 20px glyphs (4px inset each
                    // side); the right strip's 36 DIP slots inset 8px. Matching
                    // the glyph-to-glyph gap of the right column (8 + 4 + 8)
                    // needs spacing 12 here: 4 + 12 + 4.
                    spacing: 12
                    colors: bar.colors
                    playbackLoading: bar.playback && bar.playback.loading
                    lyricStyle: bar.lyricStyle
                    actions: bar.actions
                    zone: ShellActionModel.Left
                    onActionAboutToActivate: (id, anchor) => bar.lastActionAnchor = anchor
                }
            }
        }
        Item {
            id: center
            objectName: "playerbarCenterArea"
            Layout.fillWidth: !bar.lyricStyle
            Layout.preferredWidth: bar.lyricStyle ? centerActions.implicitWidth : -1
            Layout.minimumWidth: bar.lyricStyle ? centerActions.implicitWidth : 150
            Layout.alignment: Qt.AlignVCenter
            Layout.preferredHeight: centerRows.implicitHeight
            onWidthChanged: capacityUpdate.restart()
            // The centered action strip can use adjacent reserved space;
            // its intrinsic width must not affect the capacity measurement.
            // Center the whole controls/progress group independently of the
            // metadata rows. The positioner owns the two children's vertical geometry.
            Column {
                id: centerRows
                width: parent.width
                spacing: 6
                PlayerActionStrip {
                    id: centerActions
                    objectName: "playerbarCenterActions"
                    x: (center.width - width) / 2
                    width: implicitWidth
                    height: 40
                    colors: bar.colors
                    playbackLoading: bar.playback && bar.playback.loading
                    lyricStyle: bar.lyricStyle
                    actions: bar.actions
                    zone: ShellActionModel.Center
                    onActionAboutToActivate: (id, anchor) => bar.lastActionAnchor = anchor
                }
                PlaybackProgress {
                    objectName: "playerbarProgressRow"
                    visible: !bar.lyricStyle
                    x: (center.width - width) / 2
                    width: Math.max(0, Math.min(center.width, 480) - 8)
                    height: implicitHeight
                    colors: bar.colors
                    playback: bar.playback
                }
            }
        }
        Item {
            id: trailing
            objectName: "playerbarRightArea"
            Layout.alignment: Qt.AlignVCenter
            Layout.fillWidth: false
            Layout.preferredWidth: bar.sideWidth
            Layout.minimumWidth: bar.lyricStyle ? 0 : 120
            Layout.maximumWidth: bar.lyricStyle ? Infinity : 320
            Layout.preferredHeight: 40
            onWidthChanged: capacityUpdate.restart()
            RowLayout {
                id: rightRow
                anchors.right: parent.right
                anchors.rightMargin: 6
                anchors.verticalCenter: parent.verticalCenter
                spacing: 4
                PlayerActionStrip {
                    id: rightActions
                    objectName: "playerbarRightActions"
                    Layout.preferredWidth: implicitWidth
                    Layout.preferredHeight: 40
                    spacing: 4
                    colors: bar.colors
                    playbackLoading: bar.playback && bar.playback.loading
                    lyricStyle: bar.lyricStyle
                    actions: bar.actions
                    zone: ShellActionModel.Right
                    onActionAboutToActivate: (id, anchor) => bar.lastActionAnchor = anchor
                }
                EchoIconButton {
                    id: more
                    hoverBackground: false
                    objectName: "playerbarMoreAction"
                    colors: bar.colors
                    glyph: "dots"
                    text: qsTr("更多")
                    implicitWidth: 36
                    implicitHeight: 36
                    padding: 8
                    foreground: hovered || visualFocus ? bar.colors.accentText : bar.colors.iconMain
                    scale: down ? 0.9 : hovered ? 1.1 : 1
                    Behavior on scale {
                        NumberAnimation {
                            duration: Theme.normalDuration
                        }
                    }
                    onClicked: {
                        bar.lastActionAnchor = more;
                        overflowOwner.open();
                    }
                }
            }
        }
    }
    Timer {
        id: capacityUpdate
        interval: 0
        onTriggered: bar.updateCapacity()
    }
    Connections {
        target: bar.actions
        function onLayoutChanged() {
            // A contribution can leave as the overlay's bar is detached. The
            // surviving visible owner restores capacity after Qt lays it out.
            if (!bar.retiring && bar.manageCapacity && bar.visible)
                capacityUpdate.restart();
        }
    }
    EchoPopupLoader {
        id: overflowOwner
        objectName: "playerbarMorePopupOwner"
        hostItem: bar
        sourceComponent: Component {
            PlayerBarMorePopup {
                id: overflow
                anchorItem: more
                actions: bar.actions
                lyricStyle: bar.lyricStyle
                uiScale: bar.uiScale
            }
        }
    }
    Connections {
        target: bar.router
        function onDismissOverlays() {
            overflowOwner.close();
        }
    }
}
