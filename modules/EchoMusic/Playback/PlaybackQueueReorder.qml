pragma ComponentBehavior: Bound
import QtQuick
import EchoMusic.Playback
import EchoMusic.Controls

EchoListReorder {
    id: control
    objectName: "playbackQueueReorder"
    required property PlaybackQueueModel queue
    property bool viewActive: false
    property bool playing: false
    sourceModel: queue
    rowHeight: 56
    handleLeft: 72
    handleRight: 50
    snapshotForRow: row => ({
                title: row.title,
                artistText: row.artistText,
                coverUrl: row.coverUrl
            })
    onMoveRequested: (from, to) => queue.move(from, to)
    dragContent: Component {
        PlaybackQueueRow {
            objectName: "queueDragGhost"
            index: control.fromIndex
            title: control.snapshot ? control.snapshot.title : ""
            artistText: control.snapshot ? control.snapshot.artistText : ""
            coverUrl: control.snapshot ? control.snapshot.coverUrl : ""
            isCurrent: control.queue && control.queue.currentIndex === control.fromIndex
            viewActive: control.viewActive
            playing: control.playing
            resourceActive: control.usable
            actionsEnabled: false
            enabled: false
            background: Item {}
        }
    }
}
