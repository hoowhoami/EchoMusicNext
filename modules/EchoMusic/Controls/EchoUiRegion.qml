pragma ComponentBehavior: Bound
import QtQuick

Item {
    id: region
    default property alias content: regionFactory.component
    property alias partRole: registration.role
    property alias partKey: registration.key
    property alias partSurface: registration.surface
    property alias partActions: registration.actions
    property alias partState: registration.state
    readonly property alias publicPart: registration
    readonly property alias item: body.item
    property bool resourceActive: visible && Window.window !== null && Window.window.visible && Window.window.visibility !== Window.Minimized
    signal partActionRequested(string action, var arguments)
    implicitWidth: body.implicitWidth
    implicitHeight: body.implicitHeight
    data: [
        EchoUiNativeContentFactory {
            id: regionFactory
        },
        EchoUiPart {
            id: registration
            target: region
            nativeFactory: regionFactory
            replaceable: true
            properties: ["opacity"]
            onActionRequested: (action, arguments) => region.partActionRequested(action, arguments)
        },
        EchoUiContentHost {
            id: body
            anchors.fill: parent
            active: region.resourceActive
            factory: registration.contentFactory
        }
    ]
    opacity: registration.value("opacity", 1)
}
