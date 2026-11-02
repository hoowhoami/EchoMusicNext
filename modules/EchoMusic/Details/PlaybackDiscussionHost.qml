pragma ComponentBehavior: Bound
import QtQuick
import EchoMusic.Controls
import EchoMusic.Shell
import EchoMusic.Playback

Item {
    id: host
    required property PlaybackDiscussion service
    required property ShellSettings settings
    required property PlaybackService playback
    property bool resourceActive: true
    property real uiScale: 1
    property Item actionAnchor: null
    property bool resumeComposer: false
    function openComments(): void {
        service.openComments();
    }
    function openBarrage(): void {
        if (resourceActive && service.barrageAvailable)
            barrageOwner.open();
    }
    function updatePresentation(): void {
        service.setPresented(resourceActive);
        service.barrage.setEnabled(settings.lyricBarrageEnabled);
        if (!resourceActive) {
            commentsOwner.close();
            barrageOwner.close();
        }
    }
    onResourceActiveChanged: updatePresentation()
    Component.onCompleted: updatePresentation()
    Component.onDestruction: service.setPresented(false)
    Loader {
        anchors.fill: parent
        active: host.resourceActive && host.settings.lyricBarrageEnabled && host.service.barrageAvailable
        sourceComponent: BarrageLayer {
            controller: host.service.barrage
            config: host.settings.lyricBarrageConfig
            playing: host.resourceActive && host.playback.playing
        }
    }
    Connections {
        target: host.settings
        function onChanged(): void {
            host.service.barrage.setEnabled(host.settings.lyricBarrageEnabled);
        }
    }
    Connections {
        target: host.service
        function onCommentsRequested(): void {
            commentsOwner.open();
        }
    }
    Connections {
        target: host.service.barrage
        function onVerificationStarted(): void {
            if (barrageOwner.visible) {
                host.resumeComposer = true;
                barrageOwner.close();
            }
        }
        function onSubmitted(): void {
            host.resumeComposer = false;
        }
        function onScopeReset(): void {
            host.resumeComposer = false;
            barrageOwner.close();
        }
        function onSubmissionFailed(): void {
            const resume = host.resumeComposer;
            host.resumeComposer = false;
            if (resume)
                Qt.callLater(function () {
                    if (host.resourceActive)
                        barrageOwner.open();
                });
        }
    }
    EchoPopupLoader {
        id: commentsOwner
        objectName: "playbackCommentDrawerOwner"
        hostItem: host
        sourceComponent: PlaybackCommentDrawer {
            resourceActive: host.resourceActive
            service: host.service
            uiScale: host.uiScale
        }
    }
    EchoPopupLoader {
        id: barrageOwner
        objectName: "lyricBarragePanelOwner"
        hostItem: host
        sourceComponent: BarragePanel {
            objectName: "lyricBarragePanel"
            lyric: true
            controller: host.service.barrage
            settings: host.settings
            anchorItem: host.actionAnchor
            uiScale: host.uiScale
        }
    }
}
