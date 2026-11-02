pragma ComponentBehavior: Bound
import QtQuick
import EchoMusic.Controls
import EchoMusic.Theme
import EchoMusic.Shell
import EchoMusic.Playback

SettingsSection {
    id: section
    required property ShellSettings settings
    required property WindowController windowService
    required property PlaybackService playback
    readonly property PlaybackAudioControls audio: playback.audioControls
    readonly property bool selectedOutputUnavailable: audio.outputDevice.length > 0 && !audio.devices.some(item => item.name === audio.outputDevice)
    readonly property var outputNames: [qsTr("系统默认")].concat(audio.devices.map(item => item.description || item.name)).concat(selectedOutputUnavailable ? [audio.outputDevice + qsTr("（不可用）")] : [])
    readonly property var outputIds: [""].concat(audio.devices.map(item => item.name)).concat(selectedOutputUnavailable ? [audio.outputDevice] : [])
    readonly property string currentOutputLabel: audio.appliedOutputDevice.length === 0 ? qsTr("系统默认") : (audio.devices.find(item => item.name === audio.appliedOutputDevice)?.description || qsTr("未知设备"))
    readonly property var inputNames: [qsTr("系统默认")].concat(audio.inputDevices.map(item => item.name))
    readonly property var inputIds: ["default"].concat(audio.inputDevices.map(item => item.id))
    readonly property bool soundServer: /^(pipewire|pulse|pulseaudio):/.test(audio.outputDevice)
    heading: qsTr("音频设备")
    glyph: "device-speaker"
    Component.onCompleted: {
        audio.refreshDevices();
        audio.refreshInputs();
    }
    SettingsRow {
        heading: qsTr("输入设备")
        description: qsTr("用于听歌识曲等需要录音的功能")
        EchoSelect {
            colors: Theme.floating
            implicitWidth: 180
            wrapOptions: true
            model: section.inputNames
            currentIndex: Math.max(0, section.inputIds.indexOf(section.audio.inputDevice))
            onActivated: section.audio.inputDevice = section.inputIds[currentIndex]
        }
    }
    SettingsDivider {}
    SettingsRow {
        heading: qsTr("输出设备")
        description: qsTr("选择音频播放输出设备") + "\n" + qsTr("当前使用：") + section.currentOutputLabel
        EchoSelect {
            colors: Theme.floating
            objectName: "audioOutputDevice"
            implicitWidth: 180
            wrapOptions: true
            model: section.outputNames
            currentIndex: Math.max(0, section.outputIds.indexOf(section.audio.outputDevice))
            onActivated: section.audio.outputDevice = section.outputIds[currentIndex]
        }
    }
    SettingsDivider {}
    SettingsRow {
        heading: qsTr("独占音频设备")
        description: section.soundServer ? qsTr("当前设备使用系统音频服务，不支持独占") : qsTr("绕过系统混音器；开启后其他应用无法发声")
        EchoSwitch {
            checked: section.audio.exclusive
            enabled: !section.soundServer
            onToggled: section.audio.exclusive = checked
        }
    }
    SettingsDivider {}
    SettingsRow {
        heading: qsTr("设备断开时暂停")
        description: qsTr("设备不可用时暂停；关闭后尝试重连，失败则切回默认设备继续播放。")
        EchoSwitch {
            checked: section.audio.pauseOnDisconnect
            onToggled: section.audio.pauseOnDisconnect = checked
        }
    }
}
