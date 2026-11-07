pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic

// Instantiated only while a public menu has model contributions. Menu owns the
// navigation/trigger path; no second visual menu model is introduced.
QtObject {
    id: projection
    required property var adapter
    required property var menu
    property var owned: []
    property var checkedStates: ({})
    property var checkedBases: ({})
    function clear(): void {
        const previous = owned;
        owned = [];
        for (const object of previous) {
            if (menu) {
                if (object.isSeparator === true)
                    menu.removeItem(object);
                else
                    menu.removeAction(object);
            }
            object.destroy();
        }
    }
    function apply(entries: var): void {
        clear();
        const next = [];
        const retained = {};
        const bases = {};
        for (const entry of entries) {
            if (entry.separator === true) {
                const item = separator.createObject(menu.contentItem, {
                    uiScale: menu.uiScale
                });
                menu.addItem(item);
                next.push(item);
                continue;
            }
            const identity = entry.token + ':' + entry.key;
            const checked = checkedStates[identity];
            bases[identity] = entry.checked === true;
            retained[identity] = checked === undefined || checkedBases[identity] !== bases[identity] ? bases[identity] : checked;
            const action = actionComponent.createObject(projection, {
                descriptor: entry,
                checked: retained[identity]
            });
            menu.addAction(action);
            next.push(action);
        }
        checkedStates = retained;
        checkedBases = bases;
        owned = next;
        menu.refreshMeasuredSize();
    }
    readonly property Component actionComponent: Component {
        Action {
            required property var descriptor
            property bool danger: descriptor.danger === true
            text: descriptor.label
            enabled: descriptor.enabled !== false
            checkable: descriptor.checkable === true
            onTriggered: {
                projection.checkedStates[descriptor.token + ':' + descriptor.key] = checked;
                projection.adapter.invokeEntry(descriptor.token, descriptor.key, checked);
            }
        }
    }
    readonly property Component separator: Component {
        EchoMenuSeparator {
            readonly property bool isSeparator: true
        }
    }
    Component.onDestruction: clear()
}
