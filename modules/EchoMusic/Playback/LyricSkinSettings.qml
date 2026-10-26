pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
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
    component SkinSection: ColumnLayout {
        id: section
        default property alias rows: sectionBody.data
        property string heading
        Layout.fillWidth: true
        spacing: 8
        data: [
            EchoTextTagRow {
                Layout.fillWidth: true
                Layout.leftMargin: 2
                text: section.heading
                font: Theme.font(12, Theme.semiboldWeight)
                color: Theme.floatingSecondaryText
                lineHeight: 18
            },
            Control {
                Layout.fillWidth: true
                implicitHeight: sectionBody.implicitHeight
                background: Rectangle {
                    radius: Theme.cardRadius
                    color: Theme.floatingCard
                }
                contentItem: ColumnLayout {
                    id: sectionBody
                    spacing: 0
                }
            }
        ]
    }
    component SettingRow: Control {
        id: row
        property string heading
        property string hint
        property Component action
        Layout.fillWidth: true
        implicitHeight: Math.max(52, contentItem.implicitHeight + 24)
        padding: 12
        Rectangle {
            width: parent.width
            height: 1
            color: Theme.floating.strongBorder
            visible: row.y > 0
        }
        contentItem: RowLayout {
            spacing: 16
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: false
                Layout.minimumWidth: 0
                spacing: 4
                EchoTextTagRow {
                    Layout.fillWidth: true
                    text: row.heading
                    font: Theme.font(13, Theme.mediumWeight)
                    color: Theme.floatingText
                    lineHeight: 20
                    wrap: true
                }
                EchoTextTagRow {
                    visible: row.hint.length > 0
                    Layout.fillWidth: true
                    text: row.hint
                    font: Theme.font(11, Theme.regularWeight)
                    color: Theme.floatingSecondaryText
                    lineHeight: 16.5
                    wrap: true
                }
            }
            Loader {
                Layout.fillHeight: false
                sourceComponent: row.action
            }
        }
    }
    component ToggleRow: SettingRow {
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
    component RangeRow: Control {
        id: row
        property string heading
        property string hint
        property string valueLabel
        property real value
        property real from
        property real to
        property real stepSize
        signal moved(real value)
        Layout.fillWidth: true
        implicitHeight: contentItem.implicitHeight + 24
        padding: 12
        Rectangle {
            width: parent.width
            height: 1
            color: Theme.floating.strongBorder
            visible: row.y > 0
        }
        contentItem: ColumnLayout {
            spacing: 4
            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: false
                spacing: 12
                EchoTextTagRow {
                    Layout.fillWidth: true
                    text: row.heading
                    font: Theme.font(13, Theme.mediumWeight)
                    color: Theme.floatingText
                    lineHeight: 20
                }
                Control {
                    leftPadding: 6
                    rightPadding: 6
                    topPadding: 2
                    bottomPadding: 2
                    implicitWidth: contentItem.implicitWidth + 12
                    implicitHeight: 22
                    background: Rectangle {
                        radius: Theme.detailRadius
                        color: Theme.floating.mutedSurface
                    }
                    contentItem: EchoTextTagRow {
                        text: row.valueLabel
                        font: Theme.font(11, Theme.semiboldWeight)
                        color: Theme.floatingSecondaryText
                        lineHeight: 18
                    }
                }
            }
            EchoTextTagRow {
                visible: row.hint.length > 0
                Layout.fillWidth: true
                text: row.hint
                font: Theme.font(11, Theme.regularWeight)
                color: Theme.floatingSecondaryText
                lineHeight: 16.5
                wrap: true
            }
            EchoSlider {
                Layout.fillWidth: true
                Layout.topMargin: 6
                colors: Theme.floating
                from: row.from
                to: row.to
                stepSize: row.stepSize
                value: row.value
                Accessible.name: row.heading
                onMoved: row.moved(value)
            }
        }
    }
    component ColorRow: SettingRow {
        id: colorRow
        property color swatchColor
        property string actionObjectName
        property string resetObjectName
        signal openPicker
        signal resetColor
        action: Component {
            RowLayout {
                spacing: 8
                EchoSoftButton {
                    objectName: colorRow.resetObjectName
                    text: qsTr("恢复默认")
                    colors: Theme.floating
                    onClicked: colorRow.resetColor()
                }
                EchoSoftButton {
                    objectName: colorRow.actionObjectName
                    text: qsTr("更改")
                    colors: Theme.floating
                    rightPadding: 36
                    onClicked: colorRow.openPicker()
                    Rectangle {
                        anchors.right: parent.right
                        anchors.rightMargin: 8
                        anchors.verticalCenter: parent.verticalCenter
                        width: 24
                        height: 24
                        radius: Theme.controlRadius
                        color: colorRow.swatchColor
                        border.width: 1
                        border.color: Theme.floating.strongBorder
                    }
                }
            }
        }
    }
    SkinSection {
        heading: qsTr("歌词显示")
        RangeRow {
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
        ToggleRow {
            heading: qsTr("隐藏已播放")
            hint: qsTr("只显示当前与后续歌词")
            checked: settings.appearance.hidePassedLines
            onToggled: value => settings.appearance.hidePassedLines = value
        }
        ColorRow {
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
        ColorRow {
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
    SkinSection {
        heading: qsTr("歌词样式")
        RangeRow {
            heading: qsTr("字号")
            valueLabel: Math.round(value * 100) + "%"
            from: .7
            to: 1.4
            stepSize: .1
            value: settings.appearance.fontScale
            onMoved: value => settings.appearance.fontScale = value
        }
        RangeRow {
            heading: qsTr("字重")
            valueLabel: "W" + value
            from: 100
            to: 900
            stepSize: 100
            value: settings.appearance.fontWeight
            onMoved: value => settings.appearance.fontWeight = value
        }
    }
    SkinSection {
        heading: qsTr("歌词动画")
        ToggleRow {
            heading: qsTr("高刷新率动画")
            hint: qsTr("跟随屏幕刷新率，可能增加性能开销；关闭时上限 60fps")
            checked: settings.appearance.highRefreshRate
            onToggled: value => settings.appearance.highRefreshRate = value
        }
        ToggleRow {
            heading: qsTr("弹簧动画")
            hint: qsTr("使用物理弹簧驱动歌词位移")
            checked: settings.appearance.enableSpring
            onToggled: value => settings.appearance.enableSpring = value
        }
        ToggleRow {
            heading: qsTr("模糊律动")
            hint: qsTr("歌词切换时柔和过渡")
            checked: settings.appearance.enableBlur
            onToggled: value => settings.appearance.enableBlur = value
        }
        ToggleRow {
            heading: qsTr("缩放律动")
            hint: qsTr("突出当前播放的歌词行")
            checked: settings.appearance.enableScale
            onToggled: value => settings.appearance.enableScale = value
        }
        RangeRow {
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
    SkinSection {
        heading: qsTr("封面显示")
        ToggleRow {
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
