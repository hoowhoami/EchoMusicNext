pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Controls
import EchoMusic.Theme
import EchoMusic.Playback

// Shared FM presentation. Audio, candidates and preferences belong to the controller.
Item {
    id: hero
    required property PersonalFmController fm
    property bool resourceActive: true
    property bool boundedPreview: false
    property bool hoverPlayback: false
    readonly property real discSpacing: 28
    readonly property var track: fm.current
    readonly property bool hasTrack: String(track.id || "").length > 0
    readonly property bool initialLoading: fm.previewLoading && !hasTrack
    readonly property bool resourcesActive: resourceActive && fm.signedIn && Window.window !== null && Window.window.visible && Window.window.visibility !== Window.Minimized
    implicitHeight: 248
    ThemeColors {
        id: mediaColors
        text: "white"
        tone: "white"
        secondaryText: "#bdffffff"
        accentText: "white"
        accent: Theme.accent
        elevated: "#0b1620"
        card: "#0b1620"
        border: "#24ffffff"
    }
    objectName: "personalFmHero"
    height: implicitHeight
    PersonalFmVinyl {
        id: currentDisc
        objectName: "personalFmCurrentDisc"
        x: hero.boundedPreview ? poster.width - 88 : 160
        y: (parent.height - height) / 2
        z: 1
        visible: hero.hasTrack || hero.initialLoading
        artwork: hero.track.cover || ""
        text: (hero.fm.currentPlaying ? qsTr("暂停 %1") : qsTr("播放 %1")).arg(hero.track.title || "")
        trackId: hero.track.id || ""
        spinning: hero.fm.currentPlaying
        resourceActive: hero.resourcesActive && visible
        loading: hero.initialLoading
        onClicked: hero.fm.play()
    }
    Item {
        id: sideDiscs
        // Continue from the actual current disc, including its poster overlap.
        x: currentDisc.x + currentDisc.width + hero.discSpacing
        width: Math.max(0, hero.width - x)
        height: parent.height
        // Only complete discs own media; all gaps use the same geometry.
        readonly property int visibleCount: Math.max(0, Math.min(3, Math.floor((width + hero.discSpacing) / (currentDisc.width + hero.discSpacing))))
        Row {
            y: (parent.height - currentDisc.height) / 2
            spacing: hero.discSpacing
            Repeater {
                // A changing numeric model recreates every disc,
                // replaying image loading/reveal on each resize.
                // Keep three cheap slots; only visible slots own media.
                model: 3
                delegate: Loader {
                    id: sideSlot
                    required property int index
                    readonly property var entry: hero.fm.sideTracks[index] || ({})
                    active: index < Math.min(sideDiscs.visibleCount, hero.initialLoading ? 2 : hero.fm.sideTracks.length)
                    visible: active
                    width: currentDisc.width
                    height: currentDisc.height
                    sourceComponent: PersonalFmVinyl {
                        id: sideDisc
                        objectName: "personalFmSideDisc-" + sideSlot.index
                        artwork: sideSlot.entry.cover || ""
                        text: qsTr("播放 %1 · %2").arg(sideSlot.entry.title || "").arg(sideSlot.entry.artist || "")
                        resourceActive: hero.resourcesActive
                        loading: hero.initialLoading
                        onClicked: hero.fm.play(String(sideSlot.entry.id || ""))
                    }
                }
            }
        }
    }
    EchoCover {
        id: poster
        objectName: "personalFmPoster"
        width: hero.boundedPreview ? Math.min(248, Math.max(0, hero.width - 88)) : 248
        height: 248
        z: 2
        cornerRadius: Theme.cardRadius
        resourceActive: hero.resourcesActive
        readonly property color base: "#0b1620"
        readonly property color first: Qt.tint(base, Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, .30))
        readonly property color second: Qt.tint(base, Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, .40))
        source: "data:image/svg+xml;utf8," + encodeURIComponent('<svg xmlns="http://www.w3.org/2000/svg" width="248" height="248"><defs><linearGradient id="g" x1="0.280814" y1="-0.102208" x2="0.719186" y2="1.102208"><stop stop-color="' + first + '"/><stop offset=".56" stop-color="' + second + '"/><stop offset="1" stop-color="#0b1620"/></linearGradient><radialGradient id="r" cx=".16" cy=".18" r=".352165"><stop stop-color="' + Theme.accent + '" stop-opacity=".28"/><stop offset="1" stop-color="' + Theme.accent + '" stop-opacity="0"/></radialGradient></defs><rect width="248" height="248" fill="url(#g)"/><rect width="248" height="248" fill="url(#r)"/></svg>')
        Rectangle {
            anchors.fill: parent
            radius: Theme.cardRadius
            color: "transparent"
            border.color: "#14ffffff"
        }
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 16
            spacing: 0
            EchoTabBar {
                objectName: "personalFmModeSwitch"
                Layout.fillWidth: true
                labels: [qsTr("红心"), qsTr("小众"), qsTr("速览"), qsTr("电台")]
                currentIndex: hero.fm.mode
                enabled: !hero.fm.busy
                radioGroup: true
                accessibleName: qsTr("私人 FM 模式")
                colors: mediaColors
                color: "#24ffffff"
                selectionColor: "#38ffffff"
                selectionTextColor: "white"
                hoverColor: "#1affffff"
                onPicked: index => hero.fm.setMode(index)
            }
            Item {
                Layout.fillHeight: true
            }
            Text {
                Layout.fillWidth: true
                Layout.topMargin: 12
                text: hero.fm.title
                color: "white"
                font: Theme.spacedFont(28, Theme.extraBoldWeight, -1.12)
                lineHeightMode: Text.FixedHeight
                lineHeight: 28
                elide: Text.ElideRight
            }
            Item {
                Layout.fillHeight: true
            }
            Text {
                Layout.fillWidth: true
                Layout.topMargin: 10
                text: hero.hasTrack ? hero.track.title + " · " + hero.track.artist : hero.fm.subtitle + " · " + hero.fm.poolLabel
                color: "#ccffffff"
                font: Theme.font(14, Theme.semiboldWeight)
                lineHeight: 1.5
                elide: Text.ElideRight
            }
            Item {
                Layout.fillHeight: true
            }
            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 18
                spacing: 10
                Row {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    clip: true
                    spacing: 6
                    Repeater {
                        model: [11, 17, 8, 19, 13, 18, 9, 14, 8, 15]
                        delegate: Rectangle {
                            required property int modelData
                            width: 3
                            height: modelData
                            y: 19 - height
                            radius: 1.5
                            color: "#66ffffff"
                        }
                    }
                }
                EchoIconButton {
                    id: dislike
                    objectName: "personalFmDislike"
                    Layout.leftMargin: 6
                    glyph: "heart-off"
                    text: qsTr("不喜欢")
                    implicitWidth: 34
                    implicitHeight: 34
                    padding: 5
                    hoverBackground: false
                    foreground: hovered ? "white" : "#dbffffff"
                    colors: mediaColors
                    enabled: !hero.fm.busy && hero.hasTrack
                    onClicked: hero.fm.dislike()
                    EchoToolTip {
                        parent: dislike
                        text: qsTr("不喜欢")
                        requestedVisible: dislike.hovered
                    }
                }
                EchoActionButton {
                    id: fmPlayButton
                    objectName: "personalFmPlay"
                    implicitWidth: 52
                    implicitHeight: 52
                    enabled: !hero.fm.busy
                    hoverEnabled: true
                    Accessible.name: hero.fm.currentPlaying ? qsTr("暂停私人 FM") : qsTr("播放私人 FM")
                    onClicked: hero.fm.play()
                    background: Rectangle {
                        radius: 26
                        color: hero.hoverPlayback && fmPlayButton.hovered ? Theme.mix(Theme.accent, Theme.text, .12) : Theme.accent
                        EchoDashedFrame {
                            anchors.fill: parent
                            solid: true
                            strokeColor: "white"
                            radius: 26
                            thickness: fmPlayButton.visualFocus ? 2 : 0
                            visible: fmPlayButton.visualFocus
                        }
                    }
                    contentItem: Item {
                        EchoIcon {
                            anchors.centerIn: parent
                            width: 18
                            height: 18
                            name: hero.fm.currentPlaying ? "pause" : "play"
                            color: Theme.accentForeground
                            visible: !hero.fm.busy
                        }
                        EchoSpinner {
                            anchors.centerIn: parent
                            width: 18
                            height: 18
                            color: "white"
                            visible: hero.fm.busy
                            resourceActive: hero.resourcesActive && visible
                        }
                    }
                }
            }
        }
    }
}
