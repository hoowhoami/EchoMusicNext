pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts

GridLayout {
    id: layout
    property string partRole: "layout.grid"
    property string partKey: objectName
    property string partSurface: ""
    readonly property EchoUiPart publicPart: EchoUiPart {
        target: layout
        role: layout.partRole
        key: layout.partKey
        surface: layout.partSurface
        properties: []
    }
}
