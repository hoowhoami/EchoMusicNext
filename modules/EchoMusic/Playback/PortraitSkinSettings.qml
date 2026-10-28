pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import EchoMusic.Theme
import EchoMusic.Controls
import EchoMusic.Playback

ColumnLayout {
    id: form
    required property LyricAppearance appearance
    readonly property LyricPortraitAppearance settings: appearance.portrait
    property color coverColor: "#0071e3"
    property real uiScale: 1
    spacing: 18
    LyricSkinSection {
        heading: qsTr("写真背景")
        LyricSkinRangeRow {
            heading: qsTr("背景透明度")
            valueLabel: value + "%"
            value: form.settings.backdropOpacity
            from: 10
            to: 100
            stepSize: 5
            onMoved: value => form.settings.backdropOpacity = value
        }
        LyricSkinToggleRow {
            heading: qsTr("自动轮播")
            hint: qsTr("有多张写真时自动切换")
            checked: form.settings.carouselEnabled
            onToggled: value => form.settings.carouselEnabled = value
        }
        LyricSkinRangeRow {
            visible: form.settings.carouselEnabled
            heading: qsTr("轮播间隔")
            valueLabel: value + qsTr(" 秒")
            value: form.settings.carouselInterval
            from: 5
            to: 60
            stepSize: 5
            onMoved: value => form.settings.carouselInterval = value
        }
        LyricSkinToggleRow {
            heading: qsTr("无写真时展示封面")
            hint: qsTr("使用歌曲封面填充背景")
            checked: form.settings.fallbackCover
            onToggled: value => form.settings.fallbackCover = value
        }
    }
    LyricSkinSection {
        heading: qsTr("自动收起")
        LyricSkinToggleRow {
            heading: qsTr("歌词自动收起")
            hint: qsTr("无操作后保留当前句与翻译、音译")
            checked: form.settings.autoCollapse
            onToggled: value => form.settings.autoCollapse = value
        }
        LyricSkinRangeRow {
            visible: form.settings.autoCollapse
            heading: qsTr("收起延迟")
            valueLabel: value + qsTr(" 秒")
            value: form.settings.collapseDelay
            from: 5
            to: 60
            stepSize: 1
            onMoved: value => form.settings.collapseDelay = value
        }
        LyricSkinToggleRow {
            visible: form.settings.autoCollapse
            heading: qsTr("收起时隐藏控制栏")
            hint: qsTr("移动鼠标恢复操作")
            checked: form.settings.hideControls
            onToggled: value => form.settings.hideControls = value
        }
    }
    LyricSkinSection {
        heading: qsTr("歌词样式")
        LyricSkinRangeRow {
            heading: qsTr("字号")
            valueLabel: Math.round(value * 100) + "%"
            value: form.settings.fontScale
            from: .7
            to: 1.4
            stepSize: .1
            onMoved: value => form.settings.fontScale = value
        }
        LyricSkinRangeRow {
            heading: qsTr("字重")
            valueLabel: "W" + value
            value: form.settings.fontWeight
            from: 100
            to: 900
            stepSize: 100
            onMoved: value => form.settings.fontWeight = value
        }
        LyricSkinColorRow {
            heading: qsTr("已播字色")
            swatchColor: LyricColors.portraitPlayed(form.appearance, form.coverColor)
            actionObjectName: "portraitPlayedColorAction"
            resetObjectName: "resetPortraitPlayedColor"
            onResetColor: {
                form.settings.useCoverColor = false;
                form.settings.textColor = LyricColors.defaultCoverPlayedColor.toString();
            }
            onOpenPicker: playedPicker.open()
        }
        LyricSkinColorRow {
            heading: qsTr("未播字色")
            swatchColor: LyricColors.portraitUnplayed(form.appearance, form.coverColor)
            actionObjectName: "portraitUnplayedColorAction"
            resetObjectName: "resetPortraitUnplayedColor"
            onResetColor: {
                form.settings.unplayedUseCoverColor = false;
                form.settings.unplayedTextColor = LyricColors.defaultPortraitUnplayedColor.toString();
            }
            onOpenPicker: unplayedPicker.open()
        }
    }
    LyricSkinSection {
        heading: qsTr("歌词动画")
        LyricSkinToggleRow {
            heading: qsTr("弹簧动画")
            checked: form.settings.enableSpring
            onToggled: value => form.settings.enableSpring = value
        }
        LyricSkinToggleRow {
            heading: qsTr("模糊律动")
            checked: form.settings.enableBlur
            onToggled: value => form.settings.enableBlur = value
        }
        LyricSkinToggleRow {
            heading: qsTr("缩放律动")
            checked: form.settings.enableScale
            onToggled: value => form.settings.enableScale = value
        }
    }
    EchoSoftButton {
        objectName: "resetPortraitSkin"
        Layout.alignment: Qt.AlignRight
        text: qsTr("恢复全部默认")
        colors: Theme.floating
        enabled: form.settings.customized
        onClicked: form.settings.reset()
    }
    EchoPopupLoader {
        id: playedPicker
        hostItem: form
        sourceComponent: EchoColorPickerDialog {
            objectName: "portraitPlayedColorPicker"
            title: qsTr("选择已播字色")
            uiScale: form.uiScale
            value: LyricColors.opaqueHex(LyricColors.portraitPlayed(form.appearance, form.coverColor))
            dynamicLabel: qsTr("封面主题色")
            dynamicColor: form.coverColor
            dynamicSelected: form.settings.useCoverColor
            onDynamicConfirmed: {
                form.settings.textColor = "";
                form.settings.useCoverColor = true;
                close();
            }
            onConfirmed: color => {
                form.settings.useCoverColor = false;
                form.settings.textColor = color;
                close();
            }
        }
    }
    EchoPopupLoader {
        id: unplayedPicker
        hostItem: form
        sourceComponent: EchoColorPickerDialog {
            objectName: "portraitUnplayedColorPicker"
            title: qsTr("选择未播字色")
            uiScale: form.uiScale
            value: LyricColors.opaqueHex(LyricColors.portraitUnplayed(form.appearance, form.coverColor))
            dynamicLabel: qsTr("封面主题色")
            dynamicColor: form.coverColor
            dynamicSelected: form.settings.unplayedUseCoverColor
            onDynamicConfirmed: {
                form.settings.unplayedTextColor = "";
                form.settings.unplayedUseCoverColor = true;
                close();
            }
            onConfirmed: color => {
                form.settings.unplayedUseCoverColor = false;
                form.settings.unplayedTextColor = color;
                close();
            }
        }
    }
    function closePopups(): void {
        playedPicker.close();
        unplayedPicker.close();
    }
}
