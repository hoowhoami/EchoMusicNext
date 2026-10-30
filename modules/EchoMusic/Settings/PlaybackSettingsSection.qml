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
    readonly property PlaybackPreferences preferences: playback.preferences
    readonly property TrackTransitionController transition: playback.transition
    readonly property PlaybackAudioControls audio: playback.audioControls
    readonly property list<string> qualityKeys: ["standard", "higher", "flac", "hi_res", "viper_tape", "viper_clear", "viper_atmos"]
    readonly property list<string> transitionKeys: ["automix-pro", "automix-basic", "fade", "gapless", "none"]
    readonly property list<string> descriptions: [qsTr("结合节拍与段落选择衔接位置，自动调整混音"), qsTr("保持歌曲原速，自动选择衔接位置与时长"), qsTr("按设定时长交叠播放，前曲渐弱、后曲渐强"), qsTr("跳过首尾静音，直接衔接下一首"), qsTr("保留完整首尾，播放结束后切换下一首")]
    heading: qsTr("播放体验")
    glyph: "player-play"
    SettingsRow {
        heading: qsTr("播放时载入队列")
        description: qsTr("单击或双击播放歌曲时，选择要载入的歌曲范围")
        EchoSelect {
            colors: Theme.floating
            implicitWidth: 180
            model: [qsTr("当前歌曲所在列表"), qsTr("仅当前歌曲")]
            currentIndex: section.preferences.queueMode === "single" ? 1 : 0
            onActivated: section.preferences.queueMode = currentIndex === 1 ? "single" : "context"
        }
    }
    SettingsDivider {}
    SettingsRow {
        heading: qsTr("启动时自动播放")
        description: qsTr("启动后自动播放恢复的会话")
        EchoSwitch {
            checked: section.preferences.autoPlayOnLaunch
            onToggled: section.preferences.autoPlayOnLaunch = checked
        }
    }
    SettingsDivider {}
    SettingsRow {
        heading: qsTr("默认音质")
        description: qsTr("歌曲默认使用此音质，当前歌曲单独选择的音质保持不变")
        EchoSelect {
            objectName: "settingsDefaultQuality"
            colors: Theme.floating
            implicitWidth: 180
            model: section.preferences.viperQualityEnabled ? [qsTr("标准品质"), qsTr("HQ 高品质"), qsTr("SQ 无损品质"), qsTr("Hi-Res 品质"), qsTr("蝰蛇母带"), qsTr("蝰蛇超清"), qsTr("蝰蛇全景声")] : [qsTr("标准品质"), qsTr("HQ 高品质"), qsTr("SQ 无损品质"), qsTr("Hi-Res 品质")]
            currentIndex: section.qualityKeys.indexOf(section.preferences.defaultQuality)
            onActivated: section.preferences.defaultQuality = section.qualityKeys[currentIndex]
        }
    }
    SettingsDivider {}
    SettingsRow {
        heading: qsTr("开启蝰蛇音质")
        description: qsTr("显示蝰蛇母带、超清和全景声，仍需歌曲支持及对应权益")
        EchoSwitch {
            checked: section.preferences.viperQualityEnabled
            onToggled: section.preferences.viperQualityEnabled = checked
        }
    }
    SettingsDivider {}
    SettingsRow {
        heading: qsTr("智能兼容模式")
        description: qsTr("首选音质不可用时自动尝试备选")
        EchoSwitch {
            objectName: "settingsCompatibilityMode"
            checked: section.preferences.compatibilityMode
            onToggled: section.preferences.compatibilityMode = checked
        }
    }
    SettingsDivider {}
    SettingsRow {
        heading: qsTr("歌曲过渡")
        description: section.descriptions[section.transitionKeys.indexOf(section.transition.mode)]
        EchoSelect {
            colors: Theme.floating
            objectName: "trackTransitionMode"
            implicitWidth: 180
            model: [qsTr("节奏融合"), qsTr("自然衔接"), qsTr("淡入淡出"), qsTr("无缝播放"), qsTr("关闭")]
            currentIndex: section.transitionKeys.indexOf(section.transition.mode)
            onActivated: section.transition.mode = section.transitionKeys[currentIndex]
        }
    }
    SettingsDivider {
        visible: section.transition.mode === "fade"
    }
    SettingsRow {
        visible: section.transition.mode === "fade"
        heading: qsTr("过渡时长")
        description: qsTr("调整两首歌交叠淡入淡出的时间")
        EchoSpinBox {
            implicitWidth: 180
            suffix: qsTr("秒")
            from: 0
            to: 15
            value: section.transition.fadeSeconds
            onValueModified: section.transition.fadeSeconds = value
        }
    }
    SettingsDivider {
        visible: section.transition.mode === "automix-pro"
    }
    SettingsRow {
        visible: section.transition.mode === "automix-pro"
        heading: qsTr("对齐节拍")
        description: qsTr("将当前歌曲尾声微调到下一首的速度，关闭时保持原速")
        EchoSwitch {
            checked: section.transition.matchTempo
            onToggled: section.transition.matchTempo = checked
        }
    }
    SettingsDivider {}
    SettingsRow {
        heading: qsTr("开始播放时淡入")
        description: qsTr("播放新歌曲时，音量从静音渐起")
        EchoSwitch {
            checked: section.preferences.volumeFade
            onToggled: section.preferences.volumeFade = checked
        }
    }
    SettingsDivider {
        visible: section.preferences.volumeFade
    }
    SettingsRow {
        visible: section.preferences.volumeFade
        heading: qsTr("淡入时长")
        description: qsTr("调整开始播放时的音量渐起时长")
        EchoSlider {
            implicitWidth: 192
            showValue: true
            valueSuffix: "ms"
            from: 500
            to: 3000
            stepSize: 100
            value: section.preferences.volumeFadeMs
            onMoved: section.preferences.volumeFadeMs = value
            Accessible.name: qsTr("淡入时长")
        }
    }
    SettingsDivider {}
    SettingsRow {
        heading: qsTr("音量均衡")
        description: qsTr("自动平衡不同歌曲的播放响度")
        EchoSwitch {
            checked: section.audio.volumeNormalization
            onToggled: section.audio.volumeNormalization = checked
        }
    }
    SettingsDivider {
        visible: section.audio.volumeNormalization
    }
    SettingsRow {
        visible: section.audio.volumeNormalization
        heading: qsTr("参考响度")
        description: qsTr("设定歌曲的目标响度；仅对带有响度信息的音源生效")
        statusText: section.audio.normalizationStatus
        EchoSlider {
            objectName: "normalizationReferenceLufs"
            implicitWidth: 192
            showValue: true
            valueSuffix: " LUFS"
            from: -20
            to: -8
            stepSize: 1
            value: section.audio.referenceLufs
            onMoved: section.audio.referenceLufs = value
            Accessible.name: qsTr("参考响度")
        }
    }
    SettingsDivider {}
    SettingsRow {
        heading: qsTr("音频播放失败时尝试 MV 音频")
        description: qsTr("普通音频获取或解码失败后，尝试曲库 MV 音轨并从头播放，可能与原曲版本不同")
        EchoSwitch {
            objectName: "settingsMvAudioFallback"
            checked: section.preferences.mvAudioFallbackEnabled
            onToggled: section.preferences.mvAudioFallbackEnabled = checked
        }
    }
    SettingsDivider {}
    SettingsRow {
        heading: qsTr("自动跳过错误")
        description: qsTr("播放失败时停留在当前歌曲，并按设定延迟自动尝试下一首")
        EchoSwitch {
            objectName: "settingsAutoNext"
            checked: section.preferences.autoNext
            onToggled: section.preferences.autoNext = checked
        }
    }
    SettingsDivider {
        visible: section.preferences.autoNext
    }
    SettingsRow {
        visible: section.preferences.autoNext
        heading: qsTr("失败后切换延迟")
        description: qsTr("给用户留出确认失败状态的时间，再自动切换")
        EchoSpinBox {
            objectName: "settingsAutoNextDelay"
            colors: Theme.floating
            implicitWidth: 180
            from: 0
            to: 600
            stepSize: 1
            suffix: qsTr("秒")
            value: section.preferences.autoNextDelaySeconds
            Accessible.name: qsTr("失败后切换延迟")
            onValueModified: section.preferences.autoNextDelaySeconds = value
        }
    }
    SettingsDivider {
        visible: section.preferences.autoNext
    }
    SettingsRow {
        visible: section.preferences.autoNext
        heading: qsTr("最大自动切换次数")
        description: qsTr("连续失败时最多自动尝试的次数，避免无限跳歌")
        EchoSpinBox {
            objectName: "settingsAutoNextMaxAttempts"
            colors: Theme.floating
            implicitWidth: 180
            from: 1
            to: 999
            suffix: qsTr("次")
            value: section.preferences.autoNextMaxAttempts
            Accessible.name: qsTr("最大自动切换次数")
            onValueModified: section.preferences.autoNextMaxAttempts = value
        }
    }
    SettingsDivider {}
    SettingsRow {
        heading: qsTr("防止系统休眠")
        description: qsTr("播放音乐时阻止系统进入睡眠")
        statusText: section.playback.preventSleepError
        EchoSwitch {
            objectName: "settingsPreventSleep"
            checked: section.preferences.preventSleep
            onToggled: section.preferences.preventSleep = checked
        }
    }
}
