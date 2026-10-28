pragma ComponentBehavior: Bound
import QtQuick
import EchoMusic.Theme
import EchoMusic.Controls

LyricSkinSettingRow {
    id: toggleRow
    property bool checked
    signal toggled(bool value)
    action: Component {
        EchoSwitch {
            colors: Theme.floating
            checked: toggleRow.checked
            Accessible.name: toggleRow.heading
            onToggled: toggleRow.toggled(checked)
        }
    }
}
