pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Theme
import EchoMusic.Controls

ColumnLayout {
    id: gallery
    required property list<string> colors
    signal customize
    spacing: 18
    RowLayout {
        Layout.fillHeight: false
        Layout.fillWidth: true
        spacing: 12
        Label {
            text: qsTr("选择颜色")
            font: Theme.font(14, Theme.semiboldWeight)
            color: Theme.text
        }
        Item {
            Layout.fillWidth: true
        }
        EchoActionButton {
            id: customize
            objectName: "customSkinAction"
            text: qsTr("自选颜色")
            padding: 0
            font: Theme.font(11, Theme.regularWeight)
            onClicked: gallery.customize()
            contentItem: RowLayout {
                spacing: 7
                Rectangle {
                    implicitWidth: 15
                    implicitHeight: 15
                    radius: 7.5
                    color: Theme.store.seedColor
                    EchoDashedFrame {
                        anchors.fill: parent
                        solid: true
                        strokeColor: Theme.subtleBorder
                        radius: 7.5
                    }
                }
                Text {
                    text: customize.text
                    font: customize.font
                    color: Theme.text
                }
            }
            background: Item {}
        }
    }
    GridLayout {
        id: grid
        Layout.fillWidth: true
        Layout.maximumWidth: 960
        columns: Math.max(1, Math.floor((width + 12) / ((gallery.width <= 700 ? 44 : 72) + 12)))
        rowSpacing: 12
        columnSpacing: 12
        // Fixed 24-swatch gallery. Catalog/content lists use virtual views.
        Repeater {
            model: gallery.colors
            delegate: EchoActionButton {
                id: swatch
                required property string modelData
                required property int index
                objectName: "skinSwatch-" + index
                readonly property bool selected: Theme.store.themeKey === "solid" && Theme.store.seedColor.toString() === modelData
                Layout.fillWidth: true
                Layout.preferredWidth: Math.max(0, (grid.width - (grid.columns - 1) * 12) / grid.columns)
                implicitHeight: 32
                Accessible.name: qsTr("纯色 ") + modelData
                Accessible.role: Accessible.CheckBox
                Accessible.checked: selected
                onClicked: Theme.store.selectSolid(modelData)
                background: Rectangle {
                    radius: Theme.controlRadius
                    color: swatch.modelData
                    EchoDashedFrame {
                        anchors.fill: parent
                        anchors.margins: 4
                        radius: 2
                        visible: swatch.visualFocus
                        solid: true
                        strokeColor: Theme.accent
                        thickness: 2
                    }
                }
                contentItem: Item {
                    EchoIcon {
                        anchors.centerIn: parent
                        width: 16
                        height: 16
                        name: "check"
                        visible: swatch.selected
                        color: EchoColorValue.contrast(swatch.modelData, "white") >= 3 ? "white" : "#1d1d1f"
                    }
                }
            }
        }
    }
}
