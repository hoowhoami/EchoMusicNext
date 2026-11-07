pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import EchoMusic.Controls

Item {
    id: cell
    required property QtObject factory
    required property var layoutSpec
    required property string contributionKey
    required property string contributionOwner
    property bool positionerLayout: false
    Binding {
        target: cell
        property: "width"
        when: cell.positionerLayout && cell.layoutSpec.preferredWidth >= 0
        value: cell.layoutSpec.preferredWidth ?? cell.implicitWidth
        restoreMode: Binding.RestoreBindingOrValue
    }
    Binding {
        target: cell
        property: "height"
        when: cell.positionerLayout && cell.layoutSpec.preferredHeight >= 0
        value: cell.layoutSpec.preferredHeight ?? cell.implicitHeight
        restoreMode: Binding.RestoreBindingOrValue
    }
    implicitWidth: body.implicitWidth
    implicitHeight: body.implicitHeight
    Layout.preferredWidth: layoutSpec.preferredWidth ?? -1
    Layout.preferredHeight: layoutSpec.preferredHeight ?? -1
    Layout.minimumWidth: layoutSpec.minimumWidth ?? 0
    Layout.minimumHeight: layoutSpec.minimumHeight ?? 0
    Layout.maximumWidth: layoutSpec.maximumWidth ?? Infinity
    Layout.maximumHeight: layoutSpec.maximumHeight ?? Infinity
    Layout.fillWidth: layoutSpec.fillWidth ?? false
    Layout.fillHeight: layoutSpec.fillHeight ?? false
    Layout.alignment: layoutSpec.alignment ?? 0
    Layout.margins: layoutSpec.margins ?? 0
    Layout.leftMargin: layoutSpec.leftMargin ?? layoutSpec.margins ?? 0
    Layout.rightMargin: layoutSpec.rightMargin ?? layoutSpec.margins ?? 0
    Layout.topMargin: layoutSpec.topMargin ?? layoutSpec.margins ?? 0
    Layout.bottomMargin: layoutSpec.bottomMargin ?? layoutSpec.margins ?? 0
    Layout.row: layoutSpec.row ?? -1
    Layout.column: layoutSpec.column ?? -1
    Layout.rowSpan: layoutSpec.rowSpan ?? 1
    Layout.columnSpan: layoutSpec.columnSpan ?? 1
    visible: layoutSpec.visible ?? true
    EchoUiPart {
        target: cell
        role: "plugin.content"
        key: cell.contributionKey
        owner: cell.contributionOwner
        properties: []
    }
    PresentationScope {
        id: presentation
        anchors.fill: parent
    }
    EchoUiContentHost {
        id: body
        anchors.fill: parent
        factory: cell.factory
        active: presentation.presented || cell.height <= 0 && cell.visible && cell.Window.window !== null && cell.Window.window.visible && cell.Window.window.visibility !== Window.Minimized
    }
}
