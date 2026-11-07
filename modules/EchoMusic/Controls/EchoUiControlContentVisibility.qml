pragma ComponentBehavior: Bound
import QtQuick
import QtQml.Models

QtObject {
    id: adapter
    required property var factory
    property Instantiator bindings: Instantiator {
        model: adapter.factory ? adapter.factory.renderSlots : []
        delegate: Binding {
            required property var modelData
            target: modelData.item
            property: "visible"
            when: adapter.factory && adapter.factory.covered && modelData.item && modelData.item.parent === modelData.origin
            value: false
            restoreMode: Binding.RestoreBindingOrValue
        }
    }
}
