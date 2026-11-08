pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Controls
import EchoMusic.Theme

EchoDialog {
    id: dialog
    objectName: "pluginUiInspector"
    echoUiSurface: "inspector"
    heading: qsTr("界面扩展检查器")
    description: qsTr("查看公开区域、布局约束和插件贡献，调整当前区域的优先级或恢复宿主内容。")
    preferredWidth: 1000
    maximumHeight: 760
    readonly property EchoUiInspectorProbe probe: EchoUiInspectorProbe {
        color: Theme.accent
        selectedId: dialog.selectedId
        onPicked: identity => {
            dialog.refresh();
            dialog.selectedId = identity;
        }
    }
    opacity: probe.active ? 0 : 1
    modal: !probe.active
    property var nodes: []
    property string selectedId: ""
    property var selected: null
    function refreshSelected(): void {
        const current = selectedId.length ? EchoUiPartRegistry.describe(selectedId) : null;
        selected = current && current.id ? current : null;
    }
    onSelectedIdChanged: refreshSelected()
    Timer {
        id: selectedRefresh
        interval: 0
        onTriggered: dialog.refreshSelected()
    }
    function refresh(): void {
        const flat = EchoUiPartRegistry.snapshot();
        const known = new Set(flat.map(node => node.id));
        const children = new Map();
        for (const node of flat) {
            const parent = known.has(node.parentId) ? node.parentId : "";
            if (!children.has(parent))
                children.set(parent, []);
            children.get(parent).push(node);
        }
        const next = [];
        const visited = new Set();
        function append(parent: string, depth: int): void {
            for (const node of children.get(parent) || []) {
                if (visited.has(node.id))
                    continue;
                visited.add(node.id);
                next.push(Object.assign({}, node, {
                    depth: Math.min(16, depth)
                }));
                append(node.id, depth + 1);
            }
        }
        append("", 0);
        if (JSON.stringify(next) !== JSON.stringify(nodes))
            nodes = next;
        refreshSelected();
    }
    function prefer(owner: string, id: string): void {
        if (!selected)
            return;
        const order = EchoUiPartRegistry.preference(selected.address).filter(value => value !== owner + ":" + id && value !== "!" + owner + ":" + id && value !== "@host");
        order.push(owner + ":" + id);
        EchoUiPartRegistry.setPreference(selected.address, order);
        refresh();
    }
    function suppress(owner: string, id: string): void {
        if (!selected)
            return;
        const order = EchoUiPartRegistry.preference(selected.address).filter(value => value !== owner + ":" + id && value !== "!" + owner + ":" + id);
        order.push("!" + owner + ":" + id);
        EchoUiPartRegistry.setPreference(selected.address, order);
        refresh();
    }
    onOpened: {
        EchoUiPartRegistry.acquireStructure(dialog, true);
        refresh();
    }
    onClosed: {
        probe.active = false;
        probe.selectedId = "";
        EchoUiPartRegistry.releaseStructure(dialog);
    }
    Connections {
        target: EchoUiPartRegistry
        enabled: dialog.visible
        function onPartChanged(part) {
            if (part.identity === dialog.selectedId)
                selectedRefresh.restart();
        }
        function onModelDiagnosticsChanged() {
            selectedRefresh.restart();
        }
        function onTreeChanged() {
            dialog.refresh();
        }
    }
    body: Component {
        ColumnLayout {
            readonly property string echoUiSurface: "inspector"
            spacing: 12
            RowLayout {
                Layout.fillWidth: true
                EchoTextField {
                    id: filter
                    Layout.fillWidth: true
                    placeholderText: qsTr("查找区域、组件或插件")
                }
                EchoIconButton {
                    glyph: "focus-centered"
                    text: qsTr("点击拾取界面区域，Esc 取消")
                    onClicked: dialog.probe.active = true
                }
                EchoSwitch {
                    checked: EchoUiPartRegistry.enabled
                    onToggled: EchoUiPartRegistry.enabled = checked
                    Accessible.name: qsTr("启用界面扩展")
                }
                EchoTextLine {
                    text: qsTr("启用界面扩展")
                    color: Theme.floatingText
                    font: Theme.font(12)
                }
                EchoSoftButton {
                    text: qsTr("清除排序偏好")
                    onClicked: EchoUiPartRegistry.resetPreferences()
                }
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 16
                ListView {
                    id: list
                    objectName: "uiInspectorNodes"
                    Layout.preferredWidth: 340
                    Layout.preferredHeight: 400
                    clip: true
                    reuseItems: true
                    model: dialog.nodes.filter(node => (node.role + " " + node.key + " " + node.owner).toLowerCase().includes(filter.text.toLowerCase()))
                    ScrollBar.vertical: EchoPopupScrollBar {
                        surface: dialog
                        view: list
                        resourceActive: dialog.visible
                    }
                    delegate: ItemDelegate {
                        required property var modelData
                        Accessible.role: Accessible.Button
                        Accessible.name: modelData.role + " " + modelData.key
                        Accessible.onPressAction: clicked()
                        width: list.width
                        height: 56
                        leftPadding: 8 + modelData.depth * 10
                        highlighted: dialog.selectedId === modelData.id
                        onClicked: dialog.selectedId = modelData.id
                        contentItem: ColumnLayout {
                            spacing: 2
                            EchoTextLine {
                                Layout.fillWidth: true
                                text: modelData.role
                                color: Theme.floatingText
                                font: Theme.font(12, Theme.semiboldWeight)
                            }
                            EchoTextLine {
                                Layout.fillWidth: true
                                text: modelData.key || modelData.id
                                color: Theme.floatingSecondaryText
                                font: Theme.font(11)
                            }
                        }
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignTop
                    EchoTextLine {
                        text: dialog.selected ? dialog.selected.role : qsTr("选择一个公开区域")
                        color: Theme.floatingText
                        font: Theme.font(14, Theme.semiboldWeight)
                    }
                    EchoTextLine {
                        visible: dialog.selected !== null
                        text: dialog.selected && dialog.selected.persistent ? qsTr("区域偏好按当前账号保存") : qsTr("匿名或重名区域，仅本次实例生效")
                        color: Theme.floatingSecondaryText
                        font: Theme.font(11)
                    }
                    Flickable {
                        id: contractViewport
                        Layout.fillWidth: true
                        Layout.preferredHeight: 180
                        contentWidth: width
                        contentHeight: contractText.implicitHeight
                        clip: true
                        boundsBehavior: Flickable.StopAtBounds
                        ScrollBar.vertical: EchoPopupScrollBar {
                            surface: dialog
                            view: contractViewport
                            resourceActive: dialog.visible
                        }
                        TextArea {
                            id: contractText
                            objectName: "uiInspectorContract"
                            width: parent.width
                            readOnly: true
                            selectByMouse: true
                            wrapMode: TextEdit.Wrap
                            color: Theme.floatingSecondaryText
                            font: Theme.font(11)
                            background: null
                            text: dialog.selected ? JSON.stringify({
                                selector: dialog.selected.uniqueSelector,
                                persistent: dialog.selected.persistent,
                                scope: dialog.selected.scope,
                                resourceKey: dialog.selected.resourceKey,
                                parentId: dialog.selected.parentId,
                                window: dialog.selected.window,
                                layoutKind: dialog.selected.layoutKind,
                                layoutEngine: dialog.selected.layoutEngine,
                                insertionPositions: dialog.selected.insertionPositions,
                                composable: dialog.selected.composable,
                                contractVersion: dialog.selected.contractVersion,
                                automatic: dialog.selected.automatic,
                                state: dialog.selected.state,
                                capabilities: dialog.selected.capabilities,
                                model: dialog.selected.model,
                                modelContext: dialog.selected.modelContext,
                                visualSources: dialog.selected.visualSources,
                                properties: dialog.selected.properties,
                                actions: dialog.selected.actions,
                                overrides: dialog.selected.overrides,
                                contributions: dialog.selected.contributions
                            }, null, 2) : ""
                        }
                    }
                    RowLayout {
                        visible: dialog.selected !== null
                        EchoSoftButton {
                            text: qsTr("恢复宿主内容")
                            onClicked: EchoUiPartRegistry.setPreference(dialog.selected.address, ["@host"])
                        }
                        EchoSoftButton {
                            text: qsTr("恢复插件组合")
                            onClicked: EchoUiPartRegistry.setPreference(dialog.selected.address, [])
                        }
                    }
                    ListView {
                        id: contributions
                        Layout.fillWidth: true
                        Layout.preferredHeight: 160
                        clip: true
                        reuseItems: true
                        model: dialog.selected ? dialog.selected.contributions : []
                        delegate: RowLayout {
                            required property var modelData
                            width: contributions.width
                            height: 44
                            EchoTextLine {
                                Layout.fillWidth: true
                                text: modelData.owner + " / " + modelData.id + " · " + modelData.operation + (modelData.status ? " · " + modelData.status : "")
                                color: Theme.floatingText
                                font: Theme.font(11)
                            }
                            EchoIconButton {
                                glyph: "arrow-up"
                                text: qsTr("优先使用")
                                onClicked: dialog.prefer(modelData.owner, modelData.id)
                            }
                            EchoIconButton {
                                glyph: "x"
                                text: qsTr("此处停用")
                                onClicked: dialog.suppress(modelData.owner, modelData.id)
                            }
                        }
                    }
                }
            }
        }
    }
}
