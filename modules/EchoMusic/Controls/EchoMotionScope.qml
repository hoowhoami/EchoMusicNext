pragma ComponentBehavior: Bound
import QtQuick
import EchoMusic.Theme

// One lifecycle gate for finite feedback animations. Complete, rather than
// pause, when the presentation is retired so pooled items keep no stale phase.
QtObject {
    id: scope
    required property Item target
    property bool active: true
    property list<Animation> animations: []
    readonly property bool allowed: active && target !== null && target.visible && target.Window.window !== null && target.Window.window.visible && target.Window.window.visibility !== Window.Minimized && Theme.store.motionEnabled
    onAllowedChanged: {
        if (!allowed)
            for (const animation of animations)
                if (animation)
                    animation.complete();
    }
}
