pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import EchoMusic.Controls
import EchoMusic.Theme
import EchoMusic.Shell

SettingsSection {
    id: section
    required property ShellSettings settings
    required property WindowController windowService
    readonly property list<string> installedFamilies: Theme.store.fontFamilies
    heading: qsTr("字体设置")
    glyph: "typography"

    component FontRow: SettingsRow {
        id: row
        required property string family
        required property string selectorName
        property bool followGlobal: true
        readonly property string defaultFamily: followGlobal ? "follow" : "system-ui"
        readonly property var fontOptions: (followGlobal ? [qsTr("跟随全局"), qsTr("系统默认")] : [qsTr("系统默认")]).concat(section.installedFamilies)
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

    FontRow {
        heading: qsTr("全局字体")
        description: qsTr("应用到软件内所有区域的字体")
        selectorName: "settingsGlobalFont"
        followGlobal: false
        family: Theme.store.globalFont
        onFamilySelected: selectedFamily => Theme.store.globalFont = selectedFamily
    }
    SettingsDivider {}
    FontRow {
        heading: qsTr("播放页歌词字体")
        description: qsTr("播放页歌词使用的字体，跟随全局或单独指定")
        selectorName: "settingsLyricFont"
        family: Theme.store.lyricFont
        onFamilySelected: selectedFamily => Theme.store.lyricFont = selectedFamily
    }
    SettingsDivider {}
    FontRow {
        heading: qsTr("桌面歌词字体")
        description: qsTr("桌面歌词窗口使用的字体，跟随全局或单独指定")
        selectorName: "settingsDesktopLyricFont"
        family: Theme.store.desktopLyricFont
        onFamilySelected: selectedFamily => Theme.store.desktopLyricFont = selectedFamily
    }
    SettingsDivider {}
    FontRow {
        heading: qsTr("Mini 歌词字体")
        description: qsTr("Mini 歌词使用的字体，跟随全局或单独指定")
        selectorName: "settingsMiniLyricFont"
        family: Theme.store.miniLyricFont
        onFamilySelected: selectedFamily => Theme.store.miniLyricFont = selectedFamily
    }
    SettingsDivider {}
    FontRow {
        heading: qsTr("任务栏歌词字体")
        description: qsTr("任务栏歌词条使用的字体，跟随全局或单独指定")
        selectorName: "settingsTaskbarLyricFont"
        family: Theme.store.taskbarLyricFont
        onFamilySelected: selectedFamily => Theme.store.taskbarLyricFont = selectedFamily
    }
}
