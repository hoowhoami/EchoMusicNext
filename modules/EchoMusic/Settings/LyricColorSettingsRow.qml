pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Controls
import EchoMusic.Theme
import EchoMusic.Shell
import EchoMusic.Playback

SettingsRow {
    id: row
    property real uiScale: 1
    property color coverColor: "#31cfa1"
    property color playedColor: "#ffffff"
    property color unplayedColor: LyricColors.defaultUnplayedColor
    property bool followsCover: false
    property bool unplayedFollowsCover: false
    property bool customized: true
    property bool editingUnplayed: false
    readonly property color resolvedCover: Theme.store.lyricAccent(coverColor)
    signal colorChosen(bool unplayed, string value, bool follow)
    signal resetRequested
    ColumnLayout {
        Layout.fillHeight: false
        spacing: 8
        RowLayout {
            spacing: 16
            Repeater {
                model: [qsTr("已播字色"), qsTr("未播字色")]
                delegate: RowLayout {
                    id: field
                    required property int index
                    required property string modelData
                    spacing: 8
                    EchoTextTagRow {
                        text: field.modelData
                        font: Theme.font(13, Theme.semiboldWeight)
                        color: Theme.floatingSecondaryText
                    }
                    Button {
                        objectName: field.index === 0 ? "lyricPlayedColor" : "lyricUnplayedColor"
                        implicitWidth: 32
                        implicitHeight: 32
                        Accessible.name: qsTr("选择") + field.modelData
                        background: Rectangle {
                            radius: Theme.cardRadius
                            color: (field.index === 0 ? row.followsCover : row.unplayedFollowsCover) ? row.resolvedCover : field.index === 0 ? row.playedColor : row.unplayedColor
                            border.width: 1
                            border.color: Theme.floatingBorder
                        }
                        onClicked: {
                            row.editingUnplayed = field.index === 1;
                            pickerLoader.open();
                        }
                    }
                }
            }
        }
        EchoSoftButton {
            Layout.alignment: Qt.AlignRight
            opacity: row.customized ? 1 : 0
            enabled: row.customized
            colors: Theme.floating
            text: qsTr("重置")
            onClicked: row.resetRequested()
        }
    }
    EchoPopupLoader {
        id: pickerLoader
        hostItem: row
        sourceComponent: EchoColorPickerDialog {
            id: picker
            uiScale: row.uiScale
            title: row.editingUnplayed ? qsTr("选择未播字色") : qsTr("选择已播字色")
            value: LyricColors.opaqueHex(row.editingUnplayed ? row.unplayedColor : row.playedColor)
            dynamicLabel: qsTr("跟随封面取色")
            dynamicColor: row.resolvedCover
            dynamicSelected: row.editingUnplayed ? row.unplayedFollowsCover : row.followsCover
            presets: ["#31cfa1", "#b9b9b9", "#ffffff", "#ffd166", "#ef476f", "#118ab2", "#8b5cf6", "#f97316", "#22c55e", "#60a5fa", "#f43f5e", "#0f172a"]
            onConfirmed: value => row.colorChosen(row.editingUnplayed, value, false)
            onDynamicConfirmed: row.colorChosen(row.editingUnplayed, "", true)
        }
    }
    // Loader owns the popup; Qt closes it as the settings section is destroyed.
}
