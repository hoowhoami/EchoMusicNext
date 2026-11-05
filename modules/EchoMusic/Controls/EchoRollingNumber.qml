pragma ComponentBehavior: Bound
import QtQuick
import EchoMusic.Theme

// Formatted statistics retain their final native text metrics. Each digit is
// a decimal wheel: intermediate values and carry direction follow NumberFlow.
Item {
    id: number
    property string text: ""
    property alias font: measurement.font
    property alias color: measurement.color
    property alias lineHeight: measurement.lineHeight
    property alias lineHeightMode: measurement.lineHeightMode
    property bool resourceActive: true
    property bool hasPresented: false
    implicitWidth: measurement.implicitWidth
    implicitHeight: measurement.implicitHeight
    baselineOffset: measurement.baselineOffset
    Accessible.role: Accessible.StaticText
    Accessible.name: text
    onTextChanged: if (digits.status === Loader.Ready)
        (digits.item as NumberPresentation).update(true)
    EchoMotionScope {
        id: motion
        target: number
        // Statistics are short, bounded strings, never arbitrary documents.
        active: number.resourceActive && number.text.length <= 64
    }
    Text {
        id: measurement
        text: number.text
        textFormat: Text.PlainText
        font: Theme.font(18, Theme.semiboldWeight)
        color: Theme.text
        visible: !digits.active
        Accessible.ignored: true
    }
    Loader {
        id: digits
        active: motion.allowed
        anchors.fill: parent
        onLoaded: {
            (item as NumberPresentation).update(!number.hasPresented);
            number.hasPresented = true;
        }
        sourceComponent: NumberPresentation {}
    }
    component NumberPresentation: Item {
        id: presentation
        property var cells: []
        property string previousText: ""
        property real phase: 1
        readonly property real progress: Theme.rollingNumberProgress(phase)
        // Keys use numeric group and decimal place, not character index. A
        // carry such as 99 -> 100 must preserve the old units/tens wheels.
        function parse(value: string): var {
            const result = {
                values: [],
                cells: []
            };
            const pattern = /\d+(?:,\d{3})*/g;
            let cursor = 0;
            let match;
            while ((match = pattern.exec(value)) !== null) {
                while (cursor < match.index) {
                    result.cells.push({
                        index: cursor,
                        character: value.charAt(cursor),
                        numeric: false
                    });
                    ++cursor;
                }
                const group = result.values.length;
                const integer = match[0].replace(/,/g, "");
                result.values.push(Number(integer));
                let place = integer.length - 1;
                const end = match.index + match[0].length;
                while (cursor < end) {
                    const character = value.charAt(cursor);
                    const numeric = character !== ",";
                    result.cells.push({
                        index: cursor,
                        character: character,
                        numeric: numeric,
                        group: group,
                        key: numeric ? group + ":" + place-- : ""
                    });
                    ++cursor;
                }
            }
            while (cursor < value.length) {
                result.cells.push({
                    index: cursor,
                    character: value.charAt(cursor),
                    numeric: false
                });
                ++cursor;
            }
            return result;
        }
        function update(animate: bool): void {
            const previousProgress = progress;
            const positions = {};
            const directions = {};
            for (const cell of cells)
                if (cell.numeric) {
                    const position = cell.from + (cell.to - cell.from) * previousProgress;
                    positions[cell.key] = ((position % 10) + 10) % 10;
                    directions[cell.key] = Math.sign(cell.to - cell.from);
                }
            roll.stop();
            const previous = parse(previousText);
            const next = parse(number.text);
            let changed = false;
            for (const cell of next.cells) {
                if (!cell.numeric)
                    continue;
                const target = Number(cell.character);
                const from = animate ? (positions[cell.key] === undefined ? 0 : positions[cell.key]) : target;
                const current = ((from % 10) + 10) % 10;
                let delta = target - current;
                const trend = Math.sign(next.values[cell.group] - (previous.values[cell.group] || 0)) || directions[cell.key] || 0;
                // Match NumberFlow's default getDelta: the entire integer
                // determines direction, including 9 -> 0 and 0 -> 9 wraps.
                if (trend > 0 && delta < 0)
                    delta += 10;
                else if (trend < 0 && delta > 0)
                    delta -= 10;
                cell.from = from;
                cell.to = from + delta;
                changed = changed || Math.abs(delta) > 0.000001;
            }
            previousText = number.text;
            cells = next.cells;
            phase = animate && changed ? 0 : 1;
            if (phase === 0)
                roll.start();
        }
        Repeater {
            model: presentation.cells
            delegate: Item {
                id: digit
                required property var modelData
                objectName: "rollingDigit-" + modelData.index
                readonly property real position: modelData.numeric ? (presentation.phase >= 1 ? Number(modelData.character) : modelData.from + (modelData.to - modelData.from) * presentation.progress) : 0
                readonly property real lower: Math.floor(position)
                readonly property real fraction: position - lower
                x: prefix.advanceWidth
                width: through.advanceWidth - prefix.advanceWidth
                height: number.implicitHeight
                clip: true
                TextMetrics {
                    id: prefix
                    font: number.font
                    text: number.text.slice(0, digit.modelData.index)
                }
                TextMetrics {
                    id: through
                    font: number.font
                    text: number.text.slice(0, digit.modelData.index + 1)
                }
                // Only the two adjacent wheel cells exist, regardless of the
                // distance travelled. Nothing is rebuilt on animation frames.
                Text {
                    objectName: "rollingDigitValue"
                    text: digit.modelData.numeric ? String(((digit.lower % 10) + 10) % 10) : digit.modelData.character
                    textFormat: Text.PlainText
                    font: number.font
                    color: number.color
                    lineHeightMode: number.lineHeightMode
                    lineHeight: number.lineHeight
                    y: -digit.fraction * digit.height
                    Accessible.ignored: true
                }
                Text {
                    text: String((((digit.lower + 1) % 10) + 10) % 10)
                    textFormat: Text.PlainText
                    font: number.font
                    color: number.color
                    lineHeightMode: number.lineHeightMode
                    lineHeight: number.lineHeight
                    visible: digit.modelData.numeric && digit.fraction > 0
                    y: (1 - digit.fraction) * digit.height
                    Accessible.ignored: true
                }
            }
        }
        NumberAnimation {
            id: roll
            objectName: "digitRoll"
            target: presentation
            property: "phase"
            to: 1
            duration: Theme.rollingNumberDuration
            easing.type: Easing.Linear
        }
    }
}
