pragma ComponentBehavior: Bound
import QtQuick

// Resolve in the host's existing bindings: the fallback is evaluated on every
// theme/state change even while a plugin wins. Never overwrite a host binding.
EchoUiPartRegistration {
    function value(name: string, fallback: var): var {
        const values = overrides;
        return Object.prototype.hasOwnProperty.call(values, name) ? values[name] : fallback;
    }
    function styledFont(base: font): font {
        return resolveFont(base, overrides);
    }
}
