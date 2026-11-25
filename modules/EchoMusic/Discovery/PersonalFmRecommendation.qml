pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Controls
import EchoMusic.Theme
import EchoMusic.Shell
import EchoMusic.Playback

Item {
    id: panel
    objectName: "recommendationPanel-fm"
    required property DiscoveryService service
    required property RouteController router
    property bool resourceActive: true
    property real uiScale: 1
    readonly property PersonalFmController fm: service.playback.personalFm
    implicitHeight: content.implicitHeight + 32
    function syncDemand(): void {
        service.setFmPreviewActive(panel, resourceActive);
    }
    Component.onCompleted: syncDemand()
    onResourceActiveChanged: syncDemand()
    Component.onDestruction: service.setFmPreviewActive(panel, false)
    Rectangle {
        anchors.fill: parent
        radius: Theme.cardRadius
        color: Theme.withAlpha(Theme.text, Theme.dark ? .06 : .035)
    }
    ColumnLayout {
        id: content
        x: 16
        y: 16
        width: Math.max(0, panel.width - 32)
        spacing: 16
        GridLayout {
            id: heading
            Layout.fillWidth: true
            Layout.minimumHeight: 28
            columns: 4
            columnSpacing: 12
            rowSpacing: 8
            readonly property bool narrow: content.width < 460
            Label {
                Layout.row: 0
                Layout.column: 0
                Layout.minimumWidth: 0
                text: qsTr("私人 FM")
                elide: Text.ElideRight
                color: Theme.text
                font: Theme.font(15, Theme.semiboldWeight)
            }
            EchoTabBar {
                objectName: "recommendationFmPoolSwitch"
                Layout.row: heading.narrow ? 1 : 0
                Layout.column: heading.narrow ? 0 : 1
                Layout.columnSpan: heading.narrow ? 4 : 1
                Layout.preferredWidth: Math.min(240, content.width)
                compact: true
                visible: panel.fm.signedIn
                labels: [qsTr("口味"), qsTr("风格"), qsTr("探索")]
                currentIndex: panel.fm.songPool
                enabled: !panel.fm.busy
                radioGroup: true
                accessibleName: qsTr("私人 FM 歌曲范围")
                onPicked: index => panel.fm.setSongPool(index)
            }
            Item {
                Layout.row: 0
                Layout.column: 2
                Layout.fillWidth: true
                Layout.minimumWidth: 0
            }
            EchoSoftButton {
                Layout.row: 0
                Layout.column: 3
                objectName: "recommendationFmMore"
                text: qsTr("更多")
                glyph: "chevron-right"
                glyphAfterText: true
                plain: true
                hoverBackground: false
                size: EchoControlSize.Small
                onClicked: panel.router.navigate("personal-fm")
            }
        }
        Loader {
            Layout.fillWidth: true
            Layout.preferredHeight: panel.fm.signedIn ? 256 : 96
            Layout.minimumHeight: Layout.preferredHeight
            Layout.maximumHeight: Layout.preferredHeight
            active: panel.resourceActive
            asynchronous: true
            sourceComponent: !panel.fm.signedIn ? signIn : panel.fm.errorMessage.length > 0 && !String(panel.fm.current.id || "").length ? failure : hero
        }
    }
    Component {
        id: hero
        PersonalFmHero {
            width: parent.width
            fm: panel.fm
            boundedPreview: true
            hoverPlayback: true
            resourceActive: panel.resourceActive
        }
    }
    Component {
        id: failure
        ColumnLayout {
            spacing: 10
            Item {
                Layout.fillHeight: true
            }
            Label {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                text: panel.fm.errorMessage
                elide: Text.ElideRight
                color: Theme.secondaryText
                font: Theme.font(13, Theme.regularWeight)
            }
            EchoSoftButton {
                Layout.alignment: Qt.AlignHCenter
                text: qsTr("重试")
                onClicked: panel.fm.retry()
            }
            Item {
                Layout.fillHeight: true
            }
        }
    }
    Component {
        id: signIn
        ColumnLayout {
            spacing: 10
            Label {
                Layout.alignment: Qt.AlignHCenter
                text: qsTr("登录后，开启你的私人 FM")
                color: Theme.secondaryText
                font: Theme.font(13, Theme.regularWeight)
            }
            EchoSoftButton {
                Layout.alignment: Qt.AlignHCenter
                text: qsTr("前往私人 FM")
                onClicked: panel.router.navigate("personal-fm")
            }
        }
    }
}
