import QtQuick
import EchoMusic.Shell
import EchoMusic.Controls
import EchoMusic.Theme

RoutePage {
    id: page
    objectName: "probe-" + context.instanceId
    property alias visualScroll: view.contentY
    property int ticks: 0
    property bool extentAvailable: context.query.hold !== "yes"
    onExtentAvailableChanged: if (extentAvailable && EchoPageState.presentation)
        EchoPageState.presentation.release(page)
    property bool sectionEnabled: true
    readonly property bool sectionRunning: sectionEntry.running
    readonly property bool listRunning: insertion.running
    readonly property bool scrollingAllowed: viewport.navigationActive
    readonly property real sectionOpacity: view.opacity
    function replaySection(): void {
        sectionEnabled = false;
        sectionEnabled = true;
    }
    function insertVisible(): void {
        rows.insert(Math.min(rows.count, Math.max(0, Math.floor(view.contentY / 60) + 1)), {
            "name": "new"
        });
    }
    Component.onCompleted: {
        // Explicit test readiness also holds first creation, including when
        // the tiny component finishes synchronously inside Loader activation.
        if (!extentAvailable && EchoPageState.presentation)
            EchoPageState.presentation.waitFor(page);
        for (let i = 0; i < 100; ++i)
            rows.append({
                "name": String(i)
            });
    }
    ListModel {
        id: rows
    }
    EchoPageScrollContainer {
        id: viewport
        anchors.fill: parent
        pageActive: page.pageActive
        extentReady: page.extentAvailable
        scrollView: view
        ListView {
            id: view
            anchors.fill: parent
            model: rows
            delegate: Rectangle {
                required property int index
                width: view.width
                height: 60
                color: index % 2 ? "#2878bf" : "#163f65"
            }
            EchoListInsertMotion {
                id: insertion
                view: view
                active: page.pageActive
            }
            EchoEnter {
                id: sectionEntry
                target: view
                active: page.pageActive && page.sectionEnabled
            }
        }
    }
    Timer {
        interval: 5
        repeat: true
        running: page.pageActive
        onTriggered: page.ticks++
    }
}
