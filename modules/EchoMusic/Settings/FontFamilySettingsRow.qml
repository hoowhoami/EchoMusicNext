pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import EchoMusic.Controls
import EchoMusic.Theme

// 字体族选择行：从原「字体设置」页迁出的共享组件，供界面与外观、
// 歌词各子分区复用。followGlobal 为 false 时是全局字体本体（无跟随项）。
SettingsRow {
    id: row
    property string family
    property string selectorName
    property bool followGlobal: true
    readonly property string defaultFamily: followGlobal ? "follow" : "system-ui"
    readonly property list<string> installedFamilies: Theme.store.fontFamilies
    readonly property var fontOptions: (followGlobal ? [qsTr("跟随全局"), qsTr("系统默认")] : [qsTr("系统默认")]).concat(row.installedFamilies)
    readonly property string selectedLabel: family === "follow" ? qsTr("跟随全局") : family === "system-ui" ? qsTr("系统默认") : family
    signal familySelected(string selectedFamily)

    EchoSoftButton {
        objectName: row.selectorName + "Reset"
        visible: row.family !== row.defaultFamily
        text: qsTr("重置")
        colors: Theme.floating
        onClicked: row.familySelected(row.defaultFamily)
    }
    EchoSelect {
        objectName: row.selectorName
        Layout.preferredWidth: 180
        colors: Theme.floating
        filterable: true
        model: row.fontOptions
        currentIndex: row.fontOptions.indexOf(row.selectedLabel)
        displayText: row.selectedLabel
        Accessible.name: row.heading
        onActivated: index => {
            row.familySelected(row.followGlobal && index === 0 ? "follow" : index === (row.followGlobal ? 1 : 0) ? "system-ui" : row.fontOptions[index]);
        }
    }
}
