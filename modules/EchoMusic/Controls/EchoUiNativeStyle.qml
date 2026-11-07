pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQml.Models
import EchoMusic.Controls

// Only materialized for actual property overrides. Binding owns restoration
// of both native values and QML bindings; no host property is imperatively reset.
QtObject {
    id: adapter
    required property EchoUiPartRegistration part
    property Instantiator bindings: Instantiator {
        model: adapter.part ? adapter.part.nativeStyleBindings : []
        delegate: Binding {
            required property var modelData
            target: adapter.part && adapter.part.target ? modelData.attached ? adapter.part.target.Layout : adapter.part.target : null
            property: modelData.property
            value: {
                if (!adapter.part)
                    return undefined;
                const values = adapter.part.overrides;
                const value = values[modelData.key];
                if (!modelData.attached && value < 0 && modelData.key === "preferredWidth")
                    return adapter.part.target ? adapter.part.target.implicitWidth : 0;
                if (!modelData.attached && value < 0 && modelData.key === "preferredHeight")
                    return adapter.part.target ? adapter.part.target.implicitHeight : 0;
                return modelData.member && value !== undefined ? value[modelData.member] : value;
            }
            restoreMode: Binding.RestoreBindingOrValue
        }
    }
}
