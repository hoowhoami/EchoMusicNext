pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Shell
import EchoMusic.Theme
import EchoMusic.Controls

Popup {
    id: drawer
    EchoPopupLifecycle {
        surface: drawer
    }
    objectName: "themeAppearanceDrawer"
    property WindowController windowService: null
    required property real uiScale
    readonly property EchoColorPickerDialog colorDialog: accentPickerOwner.item
    readonly property list<string> accentPresets: ["#0071e3", "#ef4444", "#ec4899", "#8b5cf6", "#6366f1", "#0ea5e9", "#14b8a6", "#22c55e", "#f59e0b", "#f97316"]
    parent: Overlay.overlay
    scale: uiScale
    transformOrigin: Popup.TopLeft
    width: Math.min(360, parent ? parent.width / uiScale - 24 : 360)
    readonly property real restingX: parent ? parent.width - (width + 12) * uiScale : 0
    x: restingX
    y: (Math.max(46, 35 / uiScale) + 16) * uiScale
    // Current player is the fixed 88px idle bar plus its 8px outer margin.
    // Bind this inset to the actual expanded player when playback layouts land.
    height: parent ? Math.max(0, parent.height / uiScale - y / uiScale - 108) : 480
    padding: 0
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    Overlay.modal: Rectangle {
        color: "transparent"
    }
    onAboutToHide: accentPickerOwner.close()
    // Enter with a 16px offset and opacity transition.
    enter: Transition {
        NumberAnimation {
            property: "opacity"
            from: 0
            to: 1
            duration: Theme.panelDuration
            easing.type: Easing.BezierSpline
            easing.bezierCurve: Theme.easeEnter
        }
        NumberAnimation {
            property: "x"
            from: drawer.restingX + 16 * drawer.uiScale
            to: drawer.restingX
            duration: Theme.panelDuration
            easing.type: Easing.BezierSpline
            easing.bezierCurve: Theme.easeEnter
        }
    }
    exit: Transition {
        NumberAnimation {
            property: "opacity"
            from: 1
            to: 0
            duration: Theme.exitDuration
            easing.type: Easing.BezierSpline
            easing.bezierCurve: Theme.easeExit
        }
        NumberAnimation {
            property: "x"
            from: drawer.restingX
            to: drawer.restingX + 16 * drawer.uiScale
            duration: Theme.exitDuration
            easing.type: Easing.BezierSpline
            easing.bezierCurve: Theme.easeExit
        }
    }
    background: EchoFloatingSurface {
        color: Theme.elevated
        radius: Theme.popoverRadius
        border.width: 1
        border.color: Theme.floating.surfaceOutline
        dialogShadow: true
    }
    contentItem: ColumnLayout {
        spacing: 0
        EchoDrawerHeader {
            Layout.fillWidth: true
            heading: qsTr("外观调整")
            headingFont: Theme.font(17, Theme.semiboldWeight)
            closeLabel: qsTr("关闭外观调整")
            onCloseRequested: drawer.close()
        }
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 1
            color: Theme.floatingBorder
        }
        ScrollView {
            id: scroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: availableWidth
            clip: true
            ScrollBar.vertical: EchoPopupScrollBar {
                surface: drawer
                view: scroll
                resourceActive: drawer.visible
            }
            ColumnLayout {
                width: scroll.availableWidth
                spacing: 0
                ColumnLayout {
                    visible: Theme.store.extensions.theme !== null
                    Layout.fillWidth: true
                    Layout.margins: 24
                    spacing: 12
                    Label {
                        Layout.fillWidth: true
                        text: Theme.store.extensions.theme ? qsTr("%1 设置").arg(Theme.store.extensions.theme.title) : ""
                        color: Theme.floatingText
                        font: Theme.font(14, Theme.semiboldWeight)
                    }
                    AppearanceHost {
                        objectName: "themeAppearanceSettingsHost"
                        Layout.fillWidth: true
                        Layout.preferredHeight: Math.max(1, implicitHeight)
                        entry: Theme.store.extensions.theme
                        placement: "settings"
                        resourceActive: drawer.visible
                    }
                    Label {
                        Layout.fillWidth: true
                        text: Theme.store.extensions.theme ? Theme.store.extensions.theme.configuration.errorMessage : ""
                        visible: text.length > 0
                        color: Theme.floating.dangerText
                        wrapMode: Text.Wrap
                        font: Theme.font(12, Theme.regularWeight)
                    }
                    EchoSoftButton {
                        text: qsTr("恢复主题默认")
                        onClicked: if (Theme.store.extensions.theme)
                            Theme.store.extensions.theme.configuration.reset()
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: 24
                    Layout.rightMargin: 24
                    Layout.topMargin: 18
                    Layout.bottomMargin: 14
                    spacing: 10
                    RowLayout {
                        Layout.fillHeight: false
                        spacing: 5
                        Layout.fillWidth: true
                        Label {
                            text: qsTr("强调色")
                            font: Theme.font(14, Theme.semiboldWeight)
                            color: Theme.floatingText
                        }
                        Item {
                            Layout.fillWidth: true
                        }
                        Label {
                            text: qsTr("按钮与进度条")
                            font: Theme.font(11, Theme.regularWeight)
                            color: Theme.floatingSecondaryText
                        }
                    }
                    EchoSelect {
                        objectName: "accentSourceControl"
                        Layout.fillWidth: true
                        colors: Theme.floating
                        model: [qsTr("跟随主题"), qsTr("歌曲封面取色"), qsTr("自选颜色")]
                        currentIndex: Theme.store.accentSource === "cover" ? 1 : Theme.store.customAccent ? 2 : 0
                        Accessible.name: qsTr("界面强调色来源")
                        onActivated: {
                            if (currentIndex === 0)
                                Theme.store.useThemeAccent();
                            else if (currentIndex === 1)
                                Theme.store.useCoverAccent();
                            else if (currentIndex === 2)
                                Theme.store.setAccent(Theme.store.customColor);
                        }
                    }
                    RowLayout {
                        Layout.fillHeight: false
                        visible: Theme.store.customAccent
                        Layout.leftMargin: 3
                        Layout.topMargin: 3
                        Layout.bottomMargin: 3
                        spacing: 9
                        Repeater {
                            model: 7
                            delegate: Button {
                                id: preset
                                required property int index
                                readonly property color swatchColor: drawer.accentPresets[index]
                                implicitWidth: 24
                                implicitHeight: 24
                                Accessible.name: qsTr("强调色 ") + swatchColor
                                Accessible.role: Accessible.CheckBox
                                Accessible.checked: Theme.store.customColor === swatchColor
                                onClicked: Theme.store.setAccent(swatchColor)
                                background: Rectangle {
                                    radius: 12
                                    color: preset.swatchColor
                                    border.width: 1
                                    border.color: Theme.floatingBorder
                                    Rectangle {
                                        anchors.fill: parent
                                        anchors.margins: -5
                                        visible: Theme.store.customColor === preset.swatchColor || preset.visualFocus
                                        color: "transparent"
                                        radius: 17
                                        border.width: 2
                                        border.color: Theme.accent
                                    }
                                }
                            }
                        }
                        Button {
                            objectName: "customAccentAction"
                            implicitWidth: 24
                            implicitHeight: 24
                            Accessible.name: qsTr("自定义强调色")
                            onClicked: accentPickerOwner.open()
                            background: Rectangle {
                                radius: 12
                                color: Theme.store.customColor
                                border.width: 1
                                border.color: Theme.floatingBorder
                            }
                            contentItem: Item {
                                EchoIcon {
                                    anchors.centerIn: parent
                                    width: 14
                                    height: 14
                                    name: "plus"
                                    color: Theme.floatingText
                                }
                            }
                        }
                    }
                }
                Rectangle {
                    Layout.fillWidth: true
                    Layout.leftMargin: 24
                    Layout.rightMargin: 24
                    implicitHeight: 1
                    color: Theme.floatingBorder
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: 24
                    Layout.rightMargin: 24
                    Layout.topMargin: 14
                    Layout.bottomMargin: 14
                    spacing: 10
                    Label {
                        text: qsTr("背景氛围")
                        color: Theme.floatingText
                        font: Theme.font(14, Theme.semiboldWeight)
                    }
                    EchoSelect {
                        objectName: "atmosphereSourceControl"
                        Layout.fillWidth: true
                        colors: Theme.floating
                        model: [qsTr("歌曲封面渐变"), qsTr("关闭")]
                        currentIndex: Theme.store.coverAtmosphere ? 0 : 1
                        Accessible.name: qsTr("背景氛围")
                        onActivated: Theme.store.coverAtmosphere = currentIndex === 0
                    }
                    ThemeAppearanceRange {
                        objectName: "atmosphereHeightControl"
                        visible: Theme.store.coverAtmosphere
                        Layout.fillWidth: true
                        title: qsTr("渐变范围")
                        value: Theme.store.atmosphereHeight
                        minimum: 20
                        step: 5
                        onMoved: nextValue => Theme.store.atmosphereHeight = nextValue
                    }
                    ThemeAppearanceRange {
                        objectName: "atmosphereStrengthControl"
                        visible: Theme.store.coverAtmosphere
                        Layout.fillWidth: true
                        title: qsTr("渐变强度")
                        value: Theme.store.atmosphereStrength
                        minimum: 20
                        maximum: 200
                        step: 5
                        onMoved: nextValue => Theme.store.atmosphereStrength = nextValue
                    }
                }
                Rectangle {
                    Layout.fillWidth: true
                    Layout.leftMargin: 24
                    Layout.rightMargin: 24
                    implicitHeight: 1
                    color: Theme.floatingBorder
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: 24
                    Layout.rightMargin: 24
                    Layout.topMargin: 14
                    Layout.bottomMargin: 14
                    spacing: 7
                    Label {
                        text: qsTr("右侧面板遮罩")
                        font: Theme.font(14, Theme.semiboldWeight)
                        color: Theme.floatingText
                    }
                    RowLayout {
                        Layout.fillHeight: false
                        spacing: 5
                        Layout.fillWidth: true
                        Label {
                            text: qsTr("遮罩强度")
                            font: Theme.font(12, Theme.regularWeight)
                            color: Theme.floatingText
                        }
                        Item {
                            Layout.fillWidth: true
                        }
                        Label {
                            text: Theme.panelOpacity + "%"
                            font: Theme.font(12, Theme.regularWeight)
                            color: Theme.floatingSecondaryText
                        }
                    }
                    EchoSlider {
                        colors: Theme.floating
                        objectName: "panelOpacityControl"
                        Layout.fillWidth: true
                        from: 0
                        to: 100
                        stepSize: 1
                        value: Theme.panelOpacity
                        Accessible.name: qsTr("右侧面板遮罩")
                        onMoved: Theme.store.setPanelOpacity(value)
                    }
                    Label {
                        text: qsTr("右侧内容区与底部播放器")
                        font: Theme.font(11, Theme.regularWeight)
                        color: Theme.floatingSecondaryText
                    }
                }
                Rectangle {
                    Layout.fillWidth: true
                    Layout.leftMargin: 24
                    Layout.rightMargin: 24
                    implicitHeight: 1
                    color: Theme.floatingBorder
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: 24
                    Layout.rightMargin: 24
                    Layout.topMargin: 14
                    Layout.bottomMargin: 14
                    spacing: 10
                    Label {
                        text: qsTr("皮肤透明度")
                        color: Theme.floatingText
                        font: Theme.font(14, Theme.semiboldWeight)
                    }
                    ThemeAppearanceRange {
                        objectName: "skinTransparencyControl"
                        Layout.fillWidth: true
                        title: qsTr("透明度")
                        value: Theme.store.transparency
                        onMoved: nextValue => Theme.store.transparency = nextValue
                    }
                    RowLayout {
                        Layout.fillHeight: false
                        spacing: 5
                        Layout.fillWidth: true
                        Layout.topMargin: 8
                        Label {
                            Layout.fillWidth: true
                            text: qsTr("窗口毛玻璃")
                            font: Theme.font(13, Theme.regularWeight)
                            color: Theme.floatingText
                        }
                        EchoSwitch {
                            objectName: "windowFrostControl"
                            colors: Theme.floating
                            enabled: drawer.windowService && drawer.windowService.supportsWindowFrost
                            checked: Theme.store.windowFrosted
                            Accessible.name: qsTr("窗口毛玻璃")
                            onToggled: Theme.store.windowFrosted = checked
                        }
                    }
                    Label {
                        Layout.fillWidth: true
                        visible: text !== ""
                        text: !drawer.windowService || !drawer.windowService.supportsWindowFrost ? qsTr("此设备暂不支持窗口毛玻璃。") : drawer.windowService.appearanceError !== "" ? drawer.windowService.appearanceError : Theme.store.windowFrosted && Theme.store.transparency === 0 ? qsTr("提高透明度可看到窗口后方的模糊效果。") : ""
                        color: Theme.floatingSecondaryText
                        font: Theme.font(12, Theme.regularWeight)
                        wrapMode: Text.Wrap
                    }
                }
                Rectangle {
                    Layout.fillWidth: true
                    Layout.leftMargin: 24
                    Layout.rightMargin: 24
                    implicitHeight: 1
                    color: Theme.floatingBorder
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: 24
                    Layout.rightMargin: 24
                    Layout.topMargin: 14
                    Layout.bottomMargin: 14
                    spacing: 10
                    RowLayout {
                        Layout.fillHeight: false
                        spacing: 5
                        Layout.fillWidth: true
                        Label {
                            Layout.fillWidth: true
                            text: qsTr("弹出层毛玻璃")
                            color: Theme.floatingText
                            font: Theme.font(14, Theme.semiboldWeight)
                        }
                        EchoSwitch {
                            objectName: "floatingFrostControl"
                            colors: Theme.floating
                            checked: Theme.store.floatingFrosted
                            Accessible.name: qsTr("弹出层毛玻璃")
                            onToggled: Theme.store.floatingFrosted = checked
                        }
                    }
                    Label {
                        text: qsTr("菜单、弹窗与抽屉")
                        color: Theme.floatingSecondaryText
                        font: Theme.font(11, Theme.regularWeight)
                    }
                }
            }
        }
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 1
            color: Theme.floatingBorder
        }
        EchoButton {
            accent: false
            objectName: "resetAppearanceAction"
            text: qsTr("恢复外观默认")
            colors: Theme.floating
            Layout.leftMargin: 18
            Layout.topMargin: 8
            Layout.bottomMargin: 8
            onClicked: Theme.store.resetAppearanceAdjustments()
        }
    }
    EchoPopupLoader {
        id: accentPickerOwner
        objectName: "accentColorPickerOwner"
        hostItem: drawer.contentItem
        sourceComponent: Component {
            EchoColorPickerDialog {
                id: accentPicker
                objectName: "accentColorPicker"
                title: qsTr("强调色")
                uiScale: drawer.uiScale
                value: Theme.store.customColor.toString()
                presets: drawer.accentPresets
                onConfirmed: color => Theme.store.setAccent(color)
            }
        }
    }
}
