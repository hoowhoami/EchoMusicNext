pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Theme

// Detail-page underline tabs. Pages own routes and content; this control
// renders labels, badges and native interaction. The 36px tab box is centered
// in a 56px row; the 2px underline sits at the tab-box bottom.
Item {
    id: root
    readonly property string _echoUiModelKind: "tabs"
    property QtObject _echoUiModelAdapter: null
    readonly property var extensionEntries: _echoUiModelAdapter ? _echoUiModelAdapter.entries : []
    readonly property var displayedEntries: _echoUiModelAdapter ? _echoUiModelAdapter.displayEntries : []
    readonly property var shownLabels: _echoUiModelAdapter ? displayedEntries.map(entry => entry.label) : labels
    function nativeIndex(displayIndex: int): int {
        return _echoUiModelAdapter ? (displayedEntries[displayIndex]?.nativeIndex ?? -1) : displayIndex;
    }

    readonly property int shownIndex: !_echoUiModelAdapter ? currentIndex : _echoUiModelAdapter.selectedKey.length ? displayedEntries.findIndex(entry => entry.identity === _echoUiModelAdapter.selectedKey) : displayedEntries.findIndex(entry => entry.nativeIndex === currentIndex)
    readonly property Item _echoUiNativeFocusItem: tabs.count ? tabs.itemAt(shownIndex) : null
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
    function available(index: int): bool {
        return index >= 0 && index < shownLabels.length && (nativeIndex(index) >= 0 || displayedEntries[index]?.enabled === true);
    }
    function echoUiSelectIndex(index: int): bool {
        if (!available(index))
            return false;
        selectTab(index);
        return true;
    }
    property list<string> labels: []
    // Numeric counts include zero. An empty string hides the badge.
    property list<var> counts: []
    property list<string> objectNames: []
    property int currentIndex: 0
    property string accessibleName: qsTr("详情选项卡")
    // Pages may override typography; defaults are 15px/700.
    property real labelPixelSize: 15
    property int labelWeight: Theme.boldWeight
    implicitHeight: 56
    implicitWidth: tabRow.implicitWidth
    signal picked(int index)
    Accessible.role: Accessible.PageTabList
    Accessible.name: root.accessibleName
    readonly property real tabBoxHeight: 36
    readonly property real tabBoxTop: (root.height - root.tabBoxHeight) / 2
    // Repeater.count can change before itemAt() returns the new delegate.
    // Track the actual delegate lifecycle so a route's initially selected tab
    // has an indicator without requiring a later user selection.
    property Item activeTabItem: null
    onCurrentIndexChanged: {
        if (_echoUiModelAdapter)
            _echoUiModelAdapter.clearSelection();
    }
    onShownIndexChanged: activeTabItem = tabs ? tabs.itemAt(shownIndex) : null
    function countVisible(index: int): bool {
        index = nativeIndex(index);
        if (index < 0 || index >= root.counts.length)
            return false;
        const value = root.counts[index];
        return value !== undefined && value !== null && value !== "";
    }
    EchoDivider {
        objectName: "detailTabIndicator"
        y: root.tabBoxTop + root.tabBoxHeight - 2
        thickness: 2
        rounded: true
        lineColor: Theme.accent
        x: root.activeTabItem ? root.activeTabItem.x : 0
        width: root.activeTabItem ? root.activeTabItem.width : 0
        Behavior on x {
            enabled: Theme.normalDuration > 0
            NumberAnimation {
                duration: Theme.normalDuration
                easing.type: Easing.BezierSpline
                easing.bezierCurve: Theme.easeStandard
            }
        }
        Behavior on width {
            enabled: Theme.normalDuration > 0
            NumberAnimation {
                duration: Theme.normalDuration
                easing.type: Easing.BezierSpline
                easing.bezierCurve: Theme.easeStandard
            }
        }
    }
    RowLayout {
        id: tabRow
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: implicitWidth
        spacing: 32
        Repeater {
            id: tabs
            model: root.shownLabels
            onItemAdded: (index, item) => {
                if (index === root.shownIndex)
                    root.activeTabItem = item;
            }
            onItemRemoved: (index, item) => {
                if (root.activeTabItem === item)
                    root.activeTabItem = null;
            }
            delegate: EchoActionButton {
                id: tab
                required property int index
                required property string modelData
                readonly property string extensionGlyph: root.nativeIndex(index) < 0 ? (root.displayedEntries[index]?.glyph || "") : ""
                objectName: root.nativeIndex(tab.index) >= 0 && root.objectNames.length > root.nativeIndex(tab.index) ? root.objectNames[root.nativeIndex(tab.index)] : "detailTab-" + tab.index
                enabled: root.available(tab.index)
                flat: true
                padding: 0
                hoverEnabled: true
                activeFocusOnTab: activeFocus || tab.index === root.shownIndex
                background: null
                implicitHeight: root.tabBoxHeight
                Layout.preferredHeight: root.tabBoxHeight
                Layout.fillHeight: false
                Layout.alignment: Qt.AlignVCenter
                Accessible.role: Accessible.PageTab
                Accessible.name: tab.modelData
                Accessible.selected: tab.index === root.shownIndex
                onClicked: root.selectTab(tab.index)
                function select(next: int): void {
                    const item = tabs.itemAt(next);
                    if (!item)
                        return;
                    if (!root.available(next))
                        return;
                    root.selectTab(next);
                    item.forceActiveFocus();
                }
                function move(direction: int): void {
                    for (let i = 1; i <= root.shownLabels.length; ++i) {
                        const next = (tab.index + direction * i + root.shownLabels.length) % root.shownLabels.length;
                        if (root.available(next)) {
                            select(next);
                            return;
                        }
                    }
                }
                Keys.onRightPressed: move(1)
                Keys.onLeftPressed: move(-1)
                Keys.onPressed: event => {
                    if (event.key === Qt.Key_Home) {
                        select(0);
                        event.accepted = true;
                    } else if (event.key === Qt.Key_End) {
                        select(root.shownLabels.length - 1);
                        event.accepted = true;
                    }
                }
                contentItem: Item {
                    implicitWidth: label.implicitWidth + (glyphLoader.active ? 22 : 0)
                    implicitHeight: root.tabBoxHeight
                    Loader {
                        id: glyphLoader
                        active: tab.extensionGlyph.length > 0
                        width: active ? 14 : 0
                        height: 14
                        y: label.firstLineInkCenter - height / 2
                        sourceComponent: EchoIcon {
                            name: tab.extensionGlyph
                            color: label.color
                        }
                    }
                    EchoTextTagRow {
                        id: label
                        objectName: "detailTabLabel-" + tab.index
                        anchors.left: parent.left
                        anchors.leftMargin: glyphLoader.active ? 22 : 0
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        anchors.bottomMargin: 4
                        text: tab.modelData
                        // Active and hovered labels use the main text color; the underline carries the accent.
                        font: Theme.font(root.labelPixelSize, root.labelWeight)
                        color: tab.index === root.shownIndex || tab.hovered ? Theme.text : Theme.secondaryText
                        // Keep a 4px label-to-badge gap.
                        spacing: 4
                        wrapTags: false
                        Behavior on color {
                            enabled: Theme.fastDuration > 0
                            ColorAnimation {
                                duration: Theme.fastDuration
                                easing.type: Easing.BezierSpline
                                easing.bezierCurve: Theme.easeStandard
                            }
                        }
                        EchoBadge {
                            objectName: "detailTabBadge-" + tab.index
                            visible: root.countVisible(tab.index)
                            count: visible ? root.counts[root.nativeIndex(tab.index)] : undefined
                            // Song-tab badges use the neutral tone.
                            tone: EchoBadge.Neutral
                        }
                    }
                }
            }
        }
    }
}
