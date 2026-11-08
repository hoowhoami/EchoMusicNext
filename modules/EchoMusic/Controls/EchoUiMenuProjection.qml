pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic

QtObject {
    id: projection
    required property var adapter
    required property var menu
    property var owned: []
    property var nativeItems: []
    property var checkedStates: ({})
    property var checkedBases: ({})
    function detach(container: var, item: var): void {
        if (!container || !item)
            return;
        for (let i = 0; i < container.count; ++i)
            if (container.itemAt(i) === item) {
                container.takeItem(i);
                return;
            }
    }
    function clear(): void {
        const retiring = owned || [];
        owned = [];
        for (let i = retiring.length - 1; i >= 0; --i) {
            const record = retiring[i];
            if (!record.container || !record.object)
                continue;
            if (record.submenu)
                record.container.removeMenu(record.object);
            else if (record.action)
                record.container.removeAction(record.object);
            else
                record.container.removeItem(record.object);
        }
        const native = nativeItems || [];
        nativeItems = [];
        if (menu) {
            for (const item of native)
                detach(menu, item);
            for (const item of native)
                if (item)
                    menu.addItem(item);
        }
    }
    function nativeKeys(): var {
        const items = nativeItems.length ? nativeItems : Array.from({
            length: menu.count
        }, (_, index) => menu.itemAt(index)).filter(item => item && item._echoUiModelOwned !== true);
        return items.map(item => item.objectName || (item.action ? item.action.objectName : "") || item.text || "");
    }
    function apply(entries: var): string {
        const plan = adapter.orderEntries(nativeKeys(), entries);
        if (!plan.valid)
            return plan.error;
        clear();
        nativeItems = Array.from({
            length: menu.count
        }, (_, index) => menu.itemAt(index));
        for (const item of nativeItems)
            detach(menu, item);
        populate(menu, plan.entries, "", "", "");
        menu.refreshMeasuredSize();
        return "";
    }
    function populate(container: var, entries: var, token: string, parentKey: string, owner: string): void {
        let group = "";
        for (const value of entries) {
            if (value.nativeIndex !== undefined) {
                container.addItem(nativeItems[value.nativeIndex]);
                continue;
            }
            const entry = Object.assign({}, value);
            entry.token = entry.token || token;
            entry.owner = entry.owner || owner;
            entry.key = parentKey.length ? parentKey + '/' + entry.key : entry.key;
            if (entry.operation === "hide" || entry.operation === "move")
                continue;
            if (entry.group && group && group !== entry.group)
                appendSeparator(container);
            group = entry.group || "";
            if (entry.separator === true) {
                appendSeparator(container);
                continue;
            }
            if (entry.children) {
                const submenu = menuComponent.createObject(container, {
                    extensionOwner: entry.owner,
                    modelContext: menu.modelContext,
                    title: entry.label,
                    enabled: entry.enabled !== false,
                    uiScale: menu.uiScale
                });
                container.addMenu(submenu);
                owned.push({
                    container,
                    object: submenu,
                    submenu: true
                });
                populate(submenu, entry.children, entry.token, entry.key, entry.owner);
                continue;
            }
            const identity = entry.token + ':' + entry.key;
            const base = entry.checked === true;
            const checked = checkedStates[identity] === undefined || checkedBases[identity] !== base ? base : checkedStates[identity];
            checkedBases[identity] = base;
            checkedStates[identity] = checked;
            const action = actionComponent.createObject(projection, {
                descriptor: entry,
                checked
            });
            container.addAction(action);
            owned.push({
                container,
                object: action,
                action: true
            });
        }
    }
    function appendSeparator(container: var): void {
        const item = separator.createObject(container.contentItem, {
            uiScale: menu.uiScale
        });
        container.addItem(item);
        owned.push({
            container,
            object: item
        });
    }
    readonly property Component actionComponent: Component {
        Action {
            required property var descriptor
            property bool danger: descriptor.danger === true
            text: descriptor.label
            enabled: descriptor.enabled !== false
            checkable: descriptor.checkable === true
            function echoUiArm(): void {
                projection.adapter.armEntry(descriptor.token, descriptor.key);
            }
            onTriggered: {
                projection.checkedStates[descriptor.token + ':' + descriptor.key] = checked;
                projection.adapter.invokeEntry(descriptor.token, descriptor.key, checked);
            }
        }
    }
    readonly property Component menuComponent: Component {
        EchoMenu {
            required property string extensionOwner
            EchoUiScope.owner: extensionOwner
            readonly property bool _echoUiInternal: true
            readonly property bool _echoUiModelOwned: true
        }
    }
    readonly property Component separator: Component {
        EchoMenuSeparator {
            readonly property bool _echoUiModelOwned: true
        }
    }
    Component.onDestruction: clear()
}
