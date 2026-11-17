pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Controls
import EchoMusic.Theme

Rectangle {
    id: tabs
    readonly property string _echoUiModelKind: "tabs"
    readonly property Item _echoUiNativeFocusItem: buttons.count ? buttons.itemAt(shownIndex) : null
    property QtObject _echoUiModelAdapter: null
    readonly property var extensionEntries: _echoUiModelAdapter ? _echoUiModelAdapter.entries : []
    readonly property var displayedEntries: _echoUiModelAdapter ? _echoUiModelAdapter.displayEntries : []
    readonly property var shownLabels: _echoUiModelAdapter ? displayedEntries.map(entry => entry.label) : labels
    function nativeIndex(displayIndex: int): int {
        return _echoUiModelAdapter ? (displayedEntries[displayIndex]?.nativeIndex ?? -1) : displayIndex;
    }

    readonly property int shownIndex: !_echoUiModelAdapter ? currentIndex : _echoUiModelAdapter.selectedKey.length ? displayedEntries.findIndex(entry => entry.identity === _echoUiModelAdapter.selectedKey) : displayedEntries.findIndex(entry => entry.nativeIndex === currentIndex)
    onCurrentIndexChanged: {
        if (_echoUiModelAdapter)
            _echoUiModelAdapter.clearSelection();
    }
    function selectTab(index: int): void {
        const native = nativeIndex(index);
        if (native >= 0) {
            if (_echoUiModelAdapter)
                _echoUiModelAdapter.clearSelection();
            picked(native);
        } else if (_echoUiModelAdapter) {
            const entry = displayedEntries[index];
            _echoUiModelAdapter.selectEntry(extensionEntries.findIndex(value => value.identity === entry.identity));
        }
    }
    function echoUiSelectIndex(index: int): bool {
        if (!available(index))
            return false;
        selectTab(index);
        return true;
    }
    required property list<string> labels
    property list<string> glyphs: []
    property list<string> counts: []
    property int countTone: EchoBadge.Muted
    // Optional knowledge flags preserve unknown counts until a category is read.
    property list<bool> countKnown: []
    property list<bool> markedTabs: []
    property string markerDescription: qsTr("当前项位于%1")
    property int currentIndex: 0
    property bool compact: false
    // Keep a 7px icon-to-label gap.
    property real contentSpacing: 7
    // Tab labels use natural letter spacing.
    property real labelLetterSpacing: 0
    property ThemeColors colors: Theme.colors
    property list<bool> availableTabs: []
    property bool radioGroup: false
    // Media surfaces can scope the shared tab colors to white.
    property color selectionColor: colors.selected
    property color selectionTextColor: colors.accentText
    property color hoverColor: colors.mutedSurface
    property string accessibleName: qsTr("选项卡")
    signal picked(int index)
    function available(index: int): bool {
        if (index < 0 || index >= shownLabels.length)
            return false;
        const native = nativeIndex(index);
        return native < 0 ? displayedEntries[index]?.enabled === true : availableTabs.length === 0 || availableTabs[native];
    }
    function boundary(last: bool): int {
        for (let i = 0; i < shownLabels.length; ++i) {
            const candidate = last ? shownLabels.length - 1 - i : i;
            if (available(candidate))
                return candidate;
        }
        return -1;
    }
    implicitHeight: compact ? 32 : 44
    radius: Theme.itemRadius
    color: tabs.colors.mutedSurface
    Accessible.role: radioGroup ? Accessible.Grouping : Accessible.PageTabList
    Accessible.name: accessibleName
    // Use 4px padding, or 3px for compact tabs.
    readonly property real inset: compact ? 3 : 4
    readonly property real tabWidth: (width - inset * 2 - Math.max(0, shownLabels.length - 1) * 4) / Math.max(1, shownLabels.length)
    Rectangle {
        objectName: "tabSelection"
        x: tabs.inset + tabs.shownIndex * (tabs.tabWidth + 4)
        y: tabs.inset
        width: tabs.tabWidth
        height: tabs.height - tabs.inset * 2
        radius: Theme.itemRadius
        color: tabs.selectionColor
        visible: tabs.shownIndex >= 0 && tabs.shownIndex < tabs.shownLabels.length
        Behavior on x {
            NumberAnimation {
                duration: Theme.normalDuration
                easing.type: Easing.BezierSpline
                easing.bezierCurve: Theme.easeStandard
            }
        }
    }
    Repeater {
        id: buttons
        model: tabs.shownLabels
        delegate: EchoActionButton {
            id: tab
            required property int index
            required property string modelData
            readonly property string shownGlyph: tabs.nativeIndex(index) >= 0 ? (tabs.glyphs[tabs.nativeIndex(index)] || "") : (tabs.displayedEntries[index]?.glyph || "")
            objectName: "tab-" + index
            enabled: tabs.available(index)
            opacity: enabled ? 1 : 0.35
            x: tabs.inset + index * (tabs.tabWidth + 4)
            y: tabs.inset
            width: tabs.tabWidth
            height: tabs.height - tabs.inset * 2
            text: modelData
            padding: 0
            hoverEnabled: true
            // Qt forbids removing tab-focus eligibility from the focused item.
            activeFocusOnTab: activeFocus || index === (tabs.shownIndex >= 0 ? tabs.shownIndex : tabs.boundary(false))
            Accessible.role: tabs.radioGroup ? Accessible.RadioButton : Accessible.PageTab
            Accessible.name: text
            Accessible.selected: !tabs.radioGroup && index === tabs.shownIndex
            Accessible.checkable: tabs.radioGroup
            Accessible.checked: tabs.radioGroup && index === tabs.shownIndex
            contentItem: Item {
                implicitWidth: tabContent.implicitWidth
                implicitHeight: tabContent.implicitHeight
                RowLayout {
                    id: tabContent
                    anchors.centerIn: parent
                    width: Math.min(implicitWidth, parent.width)
                    spacing: tabs.contentSpacing
                    EchoIcon {
                        visible: tab.shownGlyph.length > 0
                        name: tab.shownGlyph
                        Layout.preferredWidth: 14
                        Layout.preferredHeight: 14
                        color: tab.index === tabs.shownIndex ? tabs.selectionTextColor : tabs.colors.secondaryText
                    }
                    EchoTextTagRow {
                        objectName: "tabLabel-" + tab.index
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        text: tab.text
                        font: Theme.spacedFont(tabs.compact ? 12 : 13, Theme.semiboldWeight, tabs.labelLetterSpacing)
                        color: tab.index === tabs.shownIndex ? tabs.selectionTextColor : tab.hovered ? tabs.colors.text : tabs.colors.secondaryText
                        spacing: tabs.contentSpacing
                        wrapTags: false
                        EchoStatusDot {
                            objectName: "tabCurrentDot"
                            visible: tabs.nativeIndex(tab.index) >= 0 && tabs.markedTabs[tabs.nativeIndex(tab.index)] === true
                            color: tabs.colors.accent
                            accessibleName: tabs.markerDescription.arg(tab.text)
                        }
                        EchoBadge {
                            visible: tabs.nativeIndex(tab.index) >= 0 && tabs.counts.length > tabs.nativeIndex(tab.index) && (tabs.countKnown.length <= tabs.nativeIndex(tab.index) || tabs.countKnown[tabs.nativeIndex(tab.index)])
                            count: visible ? tabs.counts[tabs.nativeIndex(tab.index)] : undefined
                            tone: tab.index === tabs.shownIndex ? EchoBadge.Accent : tabs.countTone
                            colors: tabs.colors
                        }
                    }
                }
            }
            background: Rectangle {
                radius: Theme.itemRadius
                color: tab.hovered && tab.index !== tabs.shownIndex ? tabs.hoverColor : "transparent"
                EchoDashedFrame {
                    anchors.fill: parent
                    solid: true
                    strokeColor: Theme.accent
                    radius: Theme.itemRadius
                    thickness: tab.visualFocus ? 2 : 0
                    visible: tab.visualFocus
                }
            }
            function select(next: int) {
                if (next < 0 || !tabs.available(next))
                    return;
                tabs.selectTab(next);
                buttons.itemAt(next).forceActiveFocus();
            }
            function move(direction: int) {
                for (let i = 1; i <= tabs.shownLabels.length; ++i) {
                    const next = (index + i * direction + tabs.shownLabels.length) % tabs.shownLabels.length;
                    if (tabs.available(next)) {
                        select(next);
                        return;
                    }
                }
            }
            onClicked: tabs.selectTab(index)
            Keys.onRightPressed: move(1)
            Keys.onLeftPressed: move(-1)
            Keys.onPressed: event => {
                if (tabs.radioGroup && (event.key === Qt.Key_Up || event.key === Qt.Key_Down)) {
                    move(event.key === Qt.Key_Up ? -1 : 1);
                    event.accepted = true;
                } else if (event.key === Qt.Key_Home) {
                    select(tabs.boundary(false));
                    event.accepted = true;
                } else if (event.key === Qt.Key_End) {
                    select(tabs.boundary(true));
                    event.accepted = true;
                }
            }
        }
    }
}
