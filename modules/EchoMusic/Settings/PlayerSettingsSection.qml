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
    readonly property PlayerEnginePreferences preferences: playback.enginePreferences
    heading: qsTr("播放引擎")
    glyph: "cpu"
    SettingsRow {
        heading: qsTr("普通预读时长")
        description: qsTr("本地播放使用的解复用前读时长")
        EchoSpinBox {
            objectName: "settingsEngineDemuxerReadaheadSecs"
            colors: Theme.floating
            implicitWidth: 180
            from: 0
            to: 3600000
            stepSize: 1
            decimals: 12
            suffix: qsTr("秒")
            value: section.preferences.demuxerReadaheadSecs
            Accessible.name: qsTr("普通预读时长")
            onValueModified: section.preferences.demuxerReadaheadSecs = value
        }
    }
    SettingsDivider {}
    SettingsRow {
        heading: qsTr("网络缓存时长上限")
        description: qsTr("与前向缓存上限共同限制网络预读；默认 3600000 秒表示基本不按时长限制")
        EchoSpinBox {
            objectName: "settingsEngineCacheSecs"
            colors: Theme.floating
            implicitWidth: 180
            from: 0
            to: 3600000
            stepSize: 1
            decimals: 12
            suffix: qsTr("秒")
            value: section.preferences.cacheSecs
            Accessible.name: qsTr("网络缓存时长上限")
            onValueModified: section.preferences.cacheSecs = value
        }
    }
    SettingsDivider {}
    SettingsRow {
        heading: qsTr("网络缓存模式")
        description: qsTr("自动时仅网络音频启用更大的 packet 缓存")
        EchoSelect {
            objectName: "settingsEngineCacheMode"
            colors: Theme.floating
            implicitWidth: 180
            model: [qsTr("自动"), qsTr("开启"), qsTr("关闭")]
            readonly property list<string> keys: ["auto", "yes", "no"]
            currentIndex: keys.indexOf(section.preferences.cacheMode)
            Accessible.name: qsTr("网络缓存模式")
            onActivated: section.preferences.cacheMode = keys[currentIndex]
        }
    }
    SettingsDivider {}
    SettingsRow {
        heading: qsTr("磁盘缓存")
        description: qsTr("缓存模式启用时将音频数据写入临时文件，结束读取后自动清理")
        EchoSwitch {
            objectName: "settingsEngineCacheOnDisk"
            checked: section.preferences.cacheOnDisk
            Accessible.name: qsTr("磁盘缓存")
            onToggled: section.preferences.cacheOnDisk = checked
        }
    }
    SettingsDivider {}
    SettingsRow {
        heading: qsTr("缓存耗尽时暂停")
        description: qsTr("网络缓存不足时等待，不影响音频输出缓冲")
        EchoSwitch {
            objectName: "settingsEngineCachePause"
            checked: section.preferences.cachePause
            Accessible.name: qsTr("缓存耗尽时暂停")
            onToggled: section.preferences.cachePause = checked
        }
    }
    SettingsDivider {}
    SettingsRow {
        heading: qsTr("缓存恢复等待")
        description: qsTr("进入缓冲后，恢复播放前等待的可播放缓存时长")
        EchoSpinBox {
            objectName: "settingsEngineCachePauseWaitSecs"
            colors: Theme.floating
            implicitWidth: 180
            from: 0
            to: 3600000
            stepSize: 0.1
            decimals: 12
            suffix: qsTr("秒")
            value: section.preferences.cachePauseWaitSecs
            Accessible.name: qsTr("缓存恢复等待")
            onValueModified: section.preferences.cachePauseWaitSecs = value
        }
    }
    SettingsDivider {}
    SettingsRow {
        heading: qsTr("前向缓存上限")
        description: section.preferences.cacheOnDisk ? qsTr("磁盘缓存启用时限制前向包索引的内存占用，不限制临时文件大小") : qsTr("解复用 packet 队列的前向缓存大小上限")
        EchoSpinBox {
            objectName: "settingsEngineDemuxerMaxMb"
            colors: Theme.floating
            implicitWidth: 180
            from: 0
            to: 4096
            stepSize: 8
            decimals: 12
            suffix: qsTr("MB")
            value: section.preferences.demuxerMaxMb
            Accessible.name: qsTr("前向缓存上限")
            onValueModified: section.preferences.demuxerMaxMb = value
        }
    }
    SettingsDivider {}
    SettingsRow {
        heading: qsTr("回退缓存上限")
        description: section.preferences.cacheOnDisk ? qsTr("磁盘缓存启用时限制已读包索引的内存占用，不限制临时文件大小") : qsTr("保留已读 packet 的回退缓存字节上限")
        EchoSpinBox {
            objectName: "settingsEngineDemuxerMaxBackMb"
            colors: Theme.floating
            implicitWidth: 180
            from: 0
            to: 4096
            stepSize: 4
            decimals: 12
            suffix: qsTr("MB")
            value: section.preferences.demuxerMaxBackMb
            Accessible.name: qsTr("回退缓存上限")
            onValueModified: section.preferences.demuxerMaxBackMb = value
        }
    }
    SettingsDivider {}
    SettingsRow {
        heading: qsTr("音频设备缓冲")
        description: qsTr("输出设备侧的小缓冲，蓝牙设备可适当调高以减少断续，但会增加响应延迟")
        EchoSpinBox {
            objectName: "settingsEngineAudioBufferSecs"
            colors: Theme.floating
            implicitWidth: 180
            from: 0
            to: 10
            stepSize: 0.05
            decimals: 12
            suffix: qsTr("秒")
            value: section.preferences.audioBufferSecs
            Accessible.name: qsTr("音频设备缓冲")
            onValueModified: section.preferences.audioBufferSecs = value
        }
    }
    SettingsDivider {}
    SettingsRow {
        heading: qsTr("音频采样率")
        description: qsTr("自动时优先对齐输出设备，固定后由引擎重采样到目标采样率")
        EchoSelect {
            objectName: "settingsEngineAudioSamplerate"
            colors: Theme.floating
            implicitWidth: 180
            model: [qsTr("自动"), qsTr("44.1 kHz"), qsTr("48 kHz"), qsTr("96 kHz"), qsTr("192 kHz")]
            readonly property list<string> keys: ["auto", "44100", "48000", "96000", "192000"]
            currentIndex: keys.indexOf(section.preferences.audioSamplerate)
            Accessible.name: qsTr("音频采样率")
            onActivated: section.preferences.audioSamplerate = keys[currentIndex]
        }
    }
    SettingsDivider {}
    SettingsRow {
        heading: qsTr("音频声道")
        description: qsTr("安全自动会在音效需要立体声时升到双声道")
        EchoSelect {
            objectName: "settingsEngineAudioChannels"
            colors: Theme.floating
            implicitWidth: 180
            model: [qsTr("安全自动"), qsTr("自动"), qsTr("立体声"), qsTr("单声道")]
            readonly property list<string> keys: ["auto-safe", "auto", "stereo", "mono"]
            currentIndex: keys.indexOf(section.preferences.audioChannels)
            Accessible.name: qsTr("音频声道")
            onActivated: section.preferences.audioChannels = keys[currentIndex]
        }
    }
    SettingsDivider {}
    SettingsRow {
        heading: qsTr("输出采样格式")
        description: qsTr("仅独占输出下生效，普通输出使用系统混音格式")
        EchoSelect {
            objectName: "settingsEngineAudioFormat"
            colors: Theme.floating
            implicitWidth: 180
            model: [qsTr("自动"), qsTr("32-bit Float"), qsTr("16-bit Integer"), qsTr("32-bit Integer")]
            readonly property list<string> keys: ["auto", "float", "s16", "s32"]
            currentIndex: keys.indexOf(section.preferences.audioFormat)
            Accessible.name: qsTr("输出采样格式")
            onActivated: section.preferences.audioFormat = keys[currentIndex]
        }
    }
    SettingsDivider {}
    SettingsRow {
        heading: qsTr("无缝播放")
        description: qsTr("自动：采样率或声道变化时重开输出；开启：始终复用输出；关闭：每首歌重开输出")
        EchoSelect {
            objectName: "settingsEngineGaplessAudio"
            colors: Theme.floating
            implicitWidth: 180
            model: [qsTr("自动"), qsTr("开启"), qsTr("关闭")]
            readonly property list<string> keys: ["weak", "yes", "no"]
            currentIndex: keys.indexOf(section.preferences.gaplessAudio)
            Accessible.name: qsTr("无缝播放")
            onActivated: section.preferences.gaplessAudio = keys[currentIndex]
        }
    }
    SettingsDivider {}
    SettingsRow {
        heading: qsTr("播放恢复超时")
        description: qsTr("长时间暂停后恢复播放可能卡住，超时后自动重新加载音频源。设为 0 禁用")
        EchoSpinBox {
            objectName: "settingsEnginePlayResumeTimeout"
            colors: Theme.floating
            implicitWidth: 180
            from: 0
            to: 30
            stepSize: 1
            decimals: 12
            suffix: qsTr("秒")
            value: section.preferences.playResumeTimeout
            Accessible.name: qsTr("播放恢复超时")
            onValueModified: section.preferences.playResumeTimeout = value
        }
    }
    SettingsDivider {}
    SettingsRow {
        heading: qsTr("播放卡死自动恢复")
        description: qsTr("播放中进度超过该秒数无推进则判定为卡死，自动重取地址并从断点续播。设为 0 禁用")
        EchoSpinBox {
            objectName: "settingsEnginePlaybackStallTimeout"
            colors: Theme.floating
            implicitWidth: 180
            from: 0
            to: 60
            stepSize: 1
            decimals: 12
            suffix: qsTr("秒")
            value: section.preferences.playbackStallTimeout
            Accessible.name: qsTr("播放卡死自动恢复")
            onValueModified: section.preferences.playbackStallTimeout = value
        }
    }
    SettingsDivider {
        visible: section.preferences.playbackStallTimeout > 0
    }
    SettingsRow {
        visible: section.preferences.playbackStallTimeout > 0
        heading: qsTr("最大自动恢复次数")
        description: qsTr("同一首歌连续卡死时最多自动恢复的次数，超过则提示并按设置自动切下一首")
        EchoSpinBox {
            objectName: "settingsEnginePlaybackStallMaxAttempts"
            colors: Theme.floating
            implicitWidth: 180
            from: 1
            to: 10
            stepSize: 1
            decimals: 0
            suffix: qsTr("次")
            value: section.preferences.playbackStallMaxAttempts
            Accessible.name: qsTr("最大自动恢复次数")
            onValueModified: section.preferences.playbackStallMaxAttempts = value
        }
    }
}
