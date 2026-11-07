pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts

ColumnLayout {
    id: layout
    property string partRole: "layout.column"
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
