pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import EchoMusic.Theme
import EchoMusic.Controls
import EchoMusic.Playback

ColumnLayout {
    id: settings
    required property LyricAppearance appearance
    property color coverColor: "#0071e3"
    readonly property color resolvedPlayedColor: LyricColors.played(appearance, coverColor)
    readonly property color resolvedUnplayedColor: LyricColors.unplayed(appearance, coverColor)
    property real uiScale: 1
    spacing: 18
    // The bounded settings form uses native stacking and shared controls.
    LyricSkinSection {
        heading: qsTr("歌词显示")
        LyricSkinRangeRow {
            objectName: "lyricAlignSetting"
            heading: qsTr("歌词位置")
            hint: qsTr("当前歌词在页面高度上的停留位置")
            valueLabel: value === .5 ? qsTr("垂直居中") : Math.round(value * 100) + "%"
            from: 0
            to: 1
            stepSize: .05
            value: settings.appearance.alignPosition
            onMoved: value => settings.appearance.alignPosition = value
        }
        LyricSkinToggleRow {
            heading: qsTr("隐藏已播放")
            hint: qsTr("只显示当前与后续歌词")
            checked: settings.appearance.hidePassedLines
            onToggled: value => settings.appearance.hidePassedLines = value
        }
        LyricSkinColorRow {
            heading: qsTr("已播字色")
            hint: qsTr("歌曲已播放部分与逐字高亮")
            actionObjectName: "lyricSkinColorAction"
            resetObjectName: "resetLyricSkinPlayedColor"
            swatchColor: settings.resolvedPlayedColor
            onResetColor: {
                settings.appearance.useCoverColor = false;
                settings.appearance.textColor = LyricColors.defaultCoverPlayedColor.toString();
            }
            onOpenPicker: playedColorPicker.open()
        }
        LyricSkinColorRow {
            heading: qsTr("未播字色")
            hint: qsTr("未播放歌词的颜色，默认为半透明白色")
            actionObjectName: "lyricSkinUnplayedColorAction"
            resetObjectName: "resetLyricSkinUnplayedColor"
            swatchColor: settings.resolvedUnplayedColor
            onResetColor: {
                settings.appearance.unplayedUseCoverColor = false;
                settings.appearance.unplayedTextColor = LyricColors.defaultCoverUnplayedColor.toString();
            }
            onOpenPicker: unplayedColorPicker.open()
        }
    }
    LyricSkinSection {
        heading: qsTr("歌词样式")
        LyricSkinRangeRow {
            heading: qsTr("字号")
            valueLabel: Math.round(value * 100) + "%"
            from: .7
            to: 1.4
            stepSize: .1
            value: settings.appearance.fontScale
            onMoved: value => settings.appearance.fontScale = value
        }
        LyricSkinRangeRow {
            heading: qsTr("字重")
            valueLabel: "W" + value
            from: 100
            to: 900
            stepSize: 100
            value: settings.appearance.fontWeight
            onMoved: value => settings.appearance.fontWeight = value
        }
    }
    LyricSkinSection {
        heading: qsTr("歌词动画")
        LyricSkinToggleRow {
            heading: qsTr("高刷新率动画")
            hint: qsTr("跟随屏幕刷新率，可能增加性能开销；关闭时上限 60fps")
            checked: settings.appearance.highRefreshRate
            onToggled: value => settings.appearance.highRefreshRate = value
        }
        LyricSkinToggleRow {
            heading: qsTr("弹簧动画")
            hint: qsTr("使用物理弹簧驱动歌词位移")
            checked: settings.appearance.enableSpring
            onToggled: value => settings.appearance.enableSpring = value
        }
        LyricSkinToggleRow {
            heading: qsTr("模糊律动")
            hint: qsTr("歌词切换时柔和过渡")
            checked: settings.appearance.enableBlur
            onToggled: value => settings.appearance.enableBlur = value
        }
        LyricSkinToggleRow {
            heading: qsTr("缩放律动")
            hint: qsTr("突出当前播放的歌词行")
            checked: settings.appearance.enableScale
            onToggled: value => settings.appearance.enableScale = value
        }
        LyricSkinRangeRow {
            heading: qsTr("渐变宽度")
            hint: qsTr("逐字高亮的过渡范围，以字号为单位")
            valueLabel: value.toFixed(2)
            from: .05
            to: 1
            stepSize: .05
            value: settings.appearance.wordFadeWidth
            onMoved: value => settings.appearance.wordFadeWidth = value
        }
    }
    LyricSkinSection {
        heading: qsTr("封面显示")
        LyricSkinToggleRow {
            heading: qsTr("动态专辑封面")
            hint: qsTr("封面随播放律动呼吸")
            checked: settings.appearance.dynamicAlbumCover
            onToggled: value => settings.appearance.dynamicAlbumCover = value
        }
    }
    RowLayout {
        spacing: 0
        Layout.fillWidth: true
        Layout.fillHeight: false
        Item {
            Layout.fillWidth: true
        }
        EchoSoftButton {
            objectName: "resetLyricSkin"
            text: qsTr("恢复全部默认")
            colors: Theme.floating
            enabled: settings.appearance.customized
            onClicked: settings.appearance.resetSkin()
        }
    }
    EchoPopupLoader {
        id: playedColorPicker
        objectName: "lyricSkinColorPickerOwner"
        hostItem: settings
        sourceComponent: Component {
            EchoColorPickerDialog {
                id: colorPicker
                objectName: "lyricSkinColorPicker"
                title: qsTr("选择已播字色")
                uiScale: settings.uiScale
                value: LyricColors.opaqueHex(settings.resolvedPlayedColor)
                dynamicLabel: qsTr("封面主题色")
                dynamicColor: settings.coverColor
                dynamicSelected: settings.appearance.useCoverColor
                presets: ["#ffffff", "#31cfa1", "#0071e3", "#ffcc00", "#ff6b6b", "#ec4899", "#8b5cf6", "#3b82f6", "#14b8a6", "#f97316", "#a3e635", "#94a3b8"]
                onDynamicConfirmed: {
                    settings.appearance.textColor = "";
                    settings.appearance.useCoverColor = true;
                    close();
                }
                onConfirmed: color => {
                    settings.appearance.useCoverColor = false;
                    settings.appearance.textColor = color;
                    close();
                }
            }
        }
    }
    EchoPopupLoader {
        id: unplayedColorPicker
        objectName: "lyricSkinUnplayedColorPickerOwner"
        hostItem: settings
        sourceComponent: Component {
            EchoColorPickerDialog {
                id: colorPicker
                objectName: "lyricSkinUnplayedColorPicker"
                title: qsTr("选择未播字色")
                uiScale: settings.uiScale
                value: LyricColors.opaqueHex(settings.resolvedUnplayedColor)
                dynamicLabel: qsTr("封面主题色")
                dynamicColor: settings.coverColor
                dynamicSelected: settings.appearance.unplayedUseCoverColor
                presets: ["#ffffff", "#b9b9b9", "#31cfa1", "#0071e3", "#ffcc00", "#ff6b6b", "#ec4899", "#8b5cf6", "#3b82f6", "#14b8a6", "#f97316", "#94a3b8"]
                onDynamicConfirmed: {
                    settings.appearance.unplayedTextColor = "";
                    settings.appearance.unplayedUseCoverColor = true;
                    close();
                }
                onConfirmed: color => {
                    settings.appearance.unplayedUseCoverColor = false;
                    settings.appearance.unplayedTextColor = color;
                    close();
                }
            }
        }
    }
    function closePopups(): void {
        playedColorPicker.close();
        unplayedColorPicker.close();
    }
}
