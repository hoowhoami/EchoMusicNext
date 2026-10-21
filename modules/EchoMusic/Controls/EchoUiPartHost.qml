pragma ComponentBehavior: Bound
import QtQuick

// The public host retains its geometry/commands while a replacement controls
// its visual contents. Plugin items are parented to this host, never nativeBody.
Item {
    id: host
    default property alias content: nativeBody.data
    property alias partRole: publicPart.role
    property alias partKey: publicPart.key
    property string partSurface: ""
    property alias partActions: publicPart.actions
    readonly property alias publicPart: publicPart
    readonly property string echoUiSurface: partSurface
    signal partActionRequested(string action, var arguments)
    opacity: publicPart.value("opacity", 1)
    data: [
        EchoUiPart {
            id: publicPart
            target: host
            surface: host.partSurface
            replaceable: true
            properties: ["background", "borderColor", "borderWidth", "radius", "opacity"]
            onActionRequested: (action, arguments) => host.partActionRequested(action, arguments)
        },
        Loader {
            anchors.fill: host
            active: host.visible && host.Window.window !== null && host.Window.window.visible && host.Window.window.visibility !== Window.Minimized && !publicPart.replaced && Object.keys(publicPart.overrides).some(name => name !== "opacity")
            sourceComponent: Rectangle {
                color: publicPart.value("background", "transparent")
                radius: publicPart.value("radius", 0)
                border.width: publicPart.value("borderWidth", 0)
                border.color: publicPart.value("borderColor", "transparent")
            }
        },
        Item {
            id: nativeBody
            anchors.fill: host
            visible: !publicPart.replaced
        }
    ]
}
