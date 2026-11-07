pragma ComponentBehavior: Bound
import QtQuick

// Shared renderers keep their native bindings and resolve only public overrides.
EchoUiPartRegistration {
    function value(name: string, fallback: var): var {
        const values = overrides;
        return Object.prototype.hasOwnProperty.call(values, name) ? values[name] : fallback;
    }
    function styledFont(base: font): font {
        return resolveFont(base, overrides);
    }
}
