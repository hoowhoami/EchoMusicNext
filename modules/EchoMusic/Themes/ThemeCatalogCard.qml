pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Theme
import EchoMusic.Controls

EchoActionButton {
    id: card
    required property string themeName
    required property string description
    required property bool selected
    property url previewSource
    property bool extension: false
    property var variants: ({})
    signal previewRequested
    signal settingsRequested
    signal chosen
    Accessible.name: themeName
    Accessible.role: Accessible.CheckBox
    Accessible.checked: selected
    padding: 10
    hoverEnabled: true
    onClicked: chosen()
    background: Rectangle {
        radius: Theme.cardRadius
        color: Theme.mutedSurface
        EchoDashedFrame {
            anchors.fill: parent
            solid: true
            strokeColor: card.visualFocus ? Theme.accent : "transparent"
            radius: Theme.cardRadius
            thickness: card.visualFocus ? 2 : 1
        }
    }
    contentItem: ColumnLayout {
        spacing: 0
        ThemeThumbnail {
            radius: Theme.mediaRadius
            Layout.fillWidth: true
            solid: false
            resourceActive: card.previewSource.toString().length === 0
            seed: "#0071e3"
            paletteOverride: (card.variants[Theme.dark ? "dark" : "light"] || {}).palette || ({})
            Loader {
                anchors.fill: parent
                active: card.visible && card.previewSource.toString().length > 0
                sourceComponent: EchoCover {
                    source: card.previewSource
                    cornerRadius: Theme.mediaRadius
                }
            }
            EchoSelectionBadge {
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 8
                visible: card.selected
            }
        }
        RowLayout {
            objectName: "themeCardTitleRow"
            Layout.fillWidth: true
            Layout.preferredHeight: EchoControlSize.heightFor(EchoControlSize.Compact)
            Layout.topMargin: 10
            Layout.bottomMargin: 4
            spacing: 4
            EchoTextLine {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                text: card.themeName
                font: Theme.font(14, Theme.semiboldWeight)
                color: Theme.text
                lineHeight: EchoControlSize.heightFor(EchoControlSize.Compact)
                fillTextWidth: true
                stableElisionBaseline: true
            }
            EchoIconButton {
                objectName: "themeCardPreviewAction"
                visible: card.extension
                implicitWidth: 32
                implicitHeight: 32
                glyph: "eye"
                text: qsTr("预览主题")
                onClicked: card.previewRequested()
            }
            EchoIconButton {
                objectName: "themeCardSettingsAction"
                visible: card.extension
                implicitWidth: 32
                implicitHeight: 32
                glyph: "settings"
                text: qsTr("主题设置")
                onClicked: card.settingsRequested()
            }
        }
        Label {
            Layout.fillWidth: true
            Layout.preferredHeight: 38.4
            text: card.description
            color: Theme.secondaryText
            font: Theme.font(12, Theme.regularWeight)
            lineHeight: 1.6
            wrapMode: Text.Wrap
            maximumLineCount: 2
            elide: Text.ElideRight
        }
    }
}
