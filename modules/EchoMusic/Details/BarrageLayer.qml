pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import EchoMusic.Controls
import EchoMusic.Theme

Item {
    id: barrageLayer
    objectName: "barrageLayer"
    required property BarrageController controller
    required property var config
    property bool playing: false
    clip: true
    Accessible.ignored: true
    readonly property int flightCount: {
        let count = 0;
        for (let i = 0; i < lanes.count; ++i) {
            const lane = lanes.itemAt(i) as Flight;
            if (lane && lane.occupied)
                ++count;
        }
        return count;
    }
    readonly property string geometryKey: [config.fontSize, config.speed, config.area].join(":")
    onGeometryKeyChanged: clearFlights()
    function clearFlights(): void {
        for (let i = 0; i < lanes.count; ++i) {
            const lane = lanes.itemAt(i) as Flight;
            if (lane)
                lane.clear();
        }
    }
    function launch(ownOnly: bool): void {
        if (!playing)
            return;
        for (let i = 0; i < lanes.count; ++i) {
            const flying = lanes.itemAt(i) as Flight;
            if (flying && flying.occupied && flying.entry.own)
                barrageLayer.controller.refreshFlying(flying.entry);
        }
        for (let i = 0; i < lanes.count; ++i) {
            const lane = lanes.itemAt(i) as Flight;
            if (lane && !lane.occupied) {
                lane.launch(ownOnly);
                return;
            }
        }
    }
    Connections {
        target: barrageLayer.controller
        function onResetFlights(): void {
            barrageLayer.clearFlights();
        }
        function onOwnReady(): void {
            barrageLayer.launch(true);
        }
    }
    Timer {
        interval: barrageLayer.config.density === 1 ? 4500 : barrageLayer.config.density === 3 ? 1400 : 2800
        repeat: true
        running: barrageLayer.playing && !barrageLayer.controller.empty
        onTriggered: barrageLayer.launch(false)
    }
    component Flight: Rectangle {
        id: flight
        objectName: "barrageFlight"
        required property int index
        property var entry: ({})
        property bool occupied: false
        property int generation: 0
        property real launchWidth: 0
        property real textWidth: 0
        property real travelMs: 1
        y: index * area.height / 4 + 2
        width: text.implicitWidth + 16
        height: barrageLayer.config.fontSize * 1.4 + 6
        radius: Theme.detailRadius
        color: entry.own ? "#33000000" : "transparent"
        EchoDashedFrame {
            anchors.fill: parent
            solid: true
            strokeColor: "#e6ffffff"
            radius: Theme.detailRadius
            thickness: entry.own ? 1 : 0
            visible: entry.own
        }
        visible: occupied
        function clear(): void {
            ++generation;
            occupied = false;
            motion.stop();
            entry = {};
        }
        function launch(ownOnly: bool): void {
            const next = barrageLayer.controller.next(ownOnly);
            if (!next.text)
                return;
            entry = next;
            occupied = true;
            const token = ++generation;
            Qt.callLater(function () {
                if (!flight.occupied || token !== flight.generation)
                    return;
                flight.launchWidth = area.width;
                flight.textWidth = flight.width;
                flight.travelMs = (flight.launchWidth + flight.textWidth) / 100 / barrageLayer.config.speed * 1000;
                barrageLayer.controller.markFlying(flight.entry, Math.ceil(flight.travelMs));
                motion.restart();
            });
        }
        Label {
            id: text
            objectName: "barrageText"
            anchors.centerIn: parent
            textFormat: Text.PlainText
            text: String(flight.entry.text || "").replace(/[ \t\r\n\f]+/g, " ")
            font: Theme.font(barrageLayer.config.fontSize, Theme.regularWeight)
            color: "white"
            style: Text.Outline
            styleColor: "#99000000"
        }
        ParallelAnimation {
            id: motion
            paused: running && !barrageLayer.playing
            NumberAnimation {
                target: flight
                property: "x"
                from: Theme.store.motionEnabled ? flight.launchWidth : 12
                to: Theme.store.motionEnabled ? -flight.textWidth : 12
                duration: flight.travelMs
                easing.type: Easing.Linear
            }
            NumberAnimation {
                target: flight
                property: "opacity"
                from: 1
                to: Theme.store.motionEnabled ? 1 : 0
                duration: flight.travelMs
                easing.type: Easing.Linear
            }
            onFinished: {
                flight.occupied = false;
                barrageLayer.launch(true);
            }
        }
    }
    Item {
        id: area
        y: 24
        width: parent.width
        height: Math.max((barrageLayer.config.fontSize * 1.4 + 8) * 4, barrageLayer.height * barrageLayer.config.area / 100 - 24)
        opacity: barrageLayer.config.opacity / 100
        Repeater {
            id: lanes
            model: 4
            delegate: Flight {}
        }
    }
    Label {
        anchors.right: parent.right
        anchors.rightMargin: 20
        y: 100
        visible: barrageLayer.flightCount === 0 && (barrageLayer.controller.loading || barrageLayer.controller.errorMessage.length > 0 || barrageLayer.controller.empty)
        text: barrageLayer.controller.loading ? qsTr("弹幕加载中…") : barrageLayer.controller.errorMessage || qsTr("暂无弹幕")
        color: "#a6ffffff"
        style: Text.Outline
        styleColor: "#99000000"
        font: Theme.font(12, Theme.regularWeight)
    }
}
