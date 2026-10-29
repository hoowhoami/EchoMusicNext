pragma ComponentBehavior: Bound
import QtQml

// Model identities own their delegates. In-model row updates retain normal
// reuse; a replacement must not inherit the previous model's pooled rows.
QtObject {
    id: guard
    objectName: "viewPoolGuard"
    required property var targetView
    property bool ready: false
    property bool resetting: false
    property bool hasSeenModel: false
    Component.onCompleted: {
        hasSeenModel = targetView.model !== null && targetView.model !== undefined;
        ready = true;
    }
    readonly property Component overrideFactory: Component {
        Binding {
            when: false
        }
    }
    function resetProjection(): void {
        if (!guard.ready || guard.resetting || !guard.targetView.reuseItems)
            return;
        guard.resetting = true;
        // Capture live bindings after setModel and its caller finish. Capturing
        // inside modelChanged can save a binding the caller is still replacing.
        const reuse = guard.overrideFactory.createObject(guard, {
            target: guard.targetView,
            property: "reuseItems",
            value: false
        });
        reuse.when = true;
        // Model reattachment must retain a page's current selection and scroll
        // position, including expressions installed by select/lyric owners.
        const positions = ["currentIndex", "contentX", "contentY"].map(name => {
            const binding = guard.overrideFactory.createObject(guard, {
                target: guard.targetView,
                property: name,
                value: guard.targetView[name]
            });
            binding.when = true;
            return binding;
        });
        // setModel can already have reused old rows before notifying observers.
        // Reattach the current model with reuse disabled, then restore its live
        // binding rather than converting it to a saved constant.
        const model = guard.overrideFactory.createObject(guard, {
            target: guard.targetView,
            property: "model",
            value: null
        });
        model.when = true;
        model.when = false;
        guard.targetView.forceLayout();
        for (const binding of positions) {
            binding.when = false;
            binding.destroy();
        }
        reuse.when = false;
        model.destroy();
        reuse.destroy();
        guard.resetting = false;
    }
    readonly property Connections modelChanges: Connections {
        target: guard.targetView
        function onModelChanged(): void {
            if (!guard.ready || guard.resetting)
                return;
            // The first model has no previous pool. Reattaching it would race
            // with popup opening and its initial keyboard selection.
            if (!guard.hasSeenModel) {
                guard.hasSeenModel = guard.targetView.model !== null && guard.targetView.model !== undefined;
                return;
            }
            Qt.callLater(guard.resetProjection);
        }
    }
}
