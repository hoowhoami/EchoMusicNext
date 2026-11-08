pragma ComponentBehavior: Bound
import QtQuick

// The public host retains its geometry/commands while a replacement controls
// its visual contents. Plugin items are parented to this host, never nativeBody.
Item {
    id: host
    default property alias content: nativeBody.data
    property alias partRole: hostPart.role
    property alias partKey: hostPart.key
    property string partSurface: ""
    property alias partState: hostPart.state
    property alias partActions: hostPart.actions
    readonly property alias publicPart: hostPart
    readonly property string echoUiSurface: partSurface
    signal partActionRequested(string action, var arguments)
    opacity: hostPart.value("opacity", 1)
    data: [
        EchoUiPart {
            id: hostPart
            target: host
            surface: host.partSurface
            replaceable: true
            nativeFactory: retainedFactory
            properties: ["background", "borderColor", "borderWidth", "radius", "opacity"]
            onActionRequested: (action, arguments) => host.partActionRequested(action, arguments)
        },
        Loader {
            anchors.fill: host
            // hasVisualOverrides is recomputed one event-loop turn after the
            // overrides settle, so a refresh()'s detach-then-attach no longer
            // tears this content down and rebuilds it for the same outcome.
            active: host.visible && host.Window.window !== null && host.Window.window.visible && host.Window.window.visibility !== Window.Minimized && !hostPart.replaced && hostPart.hasVisualOverrides
            sourceComponent: Rectangle {
                color: hostPart.value("background", "transparent")
                radius: hostPart.value("radius", 0)
                border.width: hostPart.value("borderWidth", 0)
                border.color: hostPart.value("borderColor", "transparent")
            }
        },
        Item {
            id: nativeBody
            anchors.fill: parent
            visible: !hostPart.replaced
        },
        EchoUiRetainedContentFactory {
            id: retainedFactory
            content: nativeBody
            fallbackVisible: !hostPart.replaced && hostPart.contentFactory === retainedFactory
        },
        EchoUiContentHost {
            anchors.fill: host
            active: hostPart.contentFactory !== retainedFactory && host.visible && !hostPart.replaced && host.Window.window !== null && host.Window.window.visible && host.Window.window.visibility !== Window.Minimized
            factory: hostPart.contentFactory
            onItemChanged: if (item)
                Qt.callLater(() => {
                    if (retainedFactory)
                        retainedFactory.conceal();
                })
        }
    ]
}
