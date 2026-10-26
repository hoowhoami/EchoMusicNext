pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Effects
import EchoMusic.Controls
import EchoMusic.Theme
import EchoMusic.Shell

Rectangle {
    id: page
    objectName: "startupPage"
    required property StartupController startup
    required property WindowController windowService
    required property ShellSettings settings
    required property Window window
    readonly property bool resourceActive: visible && window.visible && window.visibility !== Window.Minimized && !startup.closing
    clip: true
    color: Theme.main
    Rectangle {
        anchors.fill: parent
        opacity: 0.5
        gradient: Gradient {
            GradientStop {
                position: 0
                color: Theme.sidebar
            }
            GradientStop {
                position: 1
                color: Theme.main
            }
        }
    }
    // Bound the 300px decorative blur to this small surface.
    Loader {
        x: page.width - 200
        y: -100
        width: 300
        height: 300
        objectName: "startupGlowLoader"
        active: page.resourceActive
        sourceComponent: Rectangle {
            radius: 150
            color: Theme.withAlpha(Theme.accent, Theme.dark ? 0.1 : 0.05)
            layer.enabled: true
            layer.effect: MultiEffect {
                blurEnabled: true
                blurMax: 64
                blur: 1
            }
        }
    }
    ColumnLayout {
        anchors.centerIn: parent
        width: Math.min(480, Math.max(0, page.width - 80))
        spacing: 0
        Rectangle {
            id: mark
            objectName: "startupMark"
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: 120
            Layout.preferredHeight: 120
            Layout.bottomMargin: 60
            radius: 32
            color: Theme.elevated
            border.color: Theme.subtleBorder
            EchoSurfaceShadow {
                anchors.fill: parent
                surface: mark
                z: -1
                radius: mark.radius
                blur: 3
                offset: Qt.vector2d(0, 1)
                color: Qt.rgba(0, 0, 0, 0.1)
            }
            Column {
                width: parent.width
                anchors.centerIn: parent
                Text {
                    text: "Echo"
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    font: Theme.spacedFont(24, Theme.boldWeight, -1)
                    lineHeightMode: Text.FixedHeight
                    lineHeight: 30
                    color: Theme.text
                }
                Text {
                    text: "MUSIC"
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    lineHeightMode: Text.FixedHeight
                    lineHeight: 20
                    font: Theme.spacedFont(16, Theme.boldWeight, 2)
                    color: Theme.accentText
                }
            }
        }
        ColumnLayout {
            Layout.fillWidth: true
            visible: !page.startup.failed
            spacing: 24
            Row {
                objectName: "startupDots"
                Layout.alignment: Qt.AlignHCenter
                spacing: 6
                Repeater {
                    model: 3
                    delegate: Item {
                        id: dotSlot
                        required property int index
                        width: 8
                        height: 8
                        Rectangle {
                            id: dot
                            width: 8
                            height: 8
                            radius: 4
                            color: Theme.withAlpha(Theme.accent, 0.6)
                            y: 0
                            SequentialAnimation on y {
                                running: page.resourceActive && !page.startup.failed && !page.startup.closing && page.settings.motionEnabled
                                PauseAnimation {
                                    duration: dotSlot.index * 150
                                }
                                SequentialAnimation {
                                    loops: Animation.Infinite
                                    NumberAnimation {
                                        to: -8
                                        duration: 400
                                        easing.type: Easing.BezierSpline
                                        easing.bezierCurve: [0.45, 0.05, 0.55, 0.95, 1, 1]
                                    }
                                    NumberAnimation {
                                        to: 0
                                        duration: 400
                                        easing.type: Easing.BezierSpline
                                        easing.bezierCurve: [0.45, 0.05, 0.55, 0.95, 1, 1]
                                    }
                                }
                            }
                        }
                    }
                }
            }
            Text {
                objectName: "startupStatus"
                Layout.fillWidth: true
                text: page.startup.statusMessage.length ? page.startup.statusMessage : qsTr("正在初始化音乐引擎...")
                font: Theme.spacedFont(13, Theme.boldWeight, 0.5)
                lineHeightMode: Text.FixedHeight
                lineHeight: 19.5
                color: Theme.withAlpha(Theme.text, Theme.dark ? 0.4 : 0.6)
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
            }
        }
        ColumnLayout {
            objectName: "startupErrorGroup"
            Layout.fillWidth: true
            // The 384px text maximum propagates to this layout. Center the
            // whole group within the wider column as well as its own children.
            Layout.alignment: Qt.AlignHCenter
            visible: page.startup.failed
            spacing: 24
            Rectangle {
                objectName: "startupErrorIcon"
                Layout.alignment: Qt.AlignHCenter
                Layout.preferredWidth: 64
                Layout.preferredHeight: 64
                Layout.bottomMargin: 8
                radius: 32
                color: Theme.withAlpha("#ef4444", 0.1)
                EchoIcon {
                    anchors.centerIn: parent
                    width: 32
                    height: 32
                    name: "alert-triangle"
                    color: "#ef4444"
                }
            }
            ColumnLayout {
                Layout.fillWidth: true
                Layout.maximumWidth: 384
                Layout.alignment: Qt.AlignHCenter
                spacing: 8
                Text {
                    objectName: "startupErrorHeading"
                    lineHeightMode: Text.FixedHeight
                    lineHeight: 28
                    Layout.fillWidth: true
                    text: page.startup.canForceEnter ? qsTr("启动未完全就绪") : qsTr("启动失败")
                    font: Theme.font(18, Theme.boldWeight)
                    color: Theme.withAlpha("#ef4444", 0.9)
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                }
                Text {
                    objectName: "startupErrorMessage"
                    lineHeightMode: Text.FixedHeight
                    lineHeight: 24
                    Layout.fillWidth: true
                    text: page.startup.statusMessage
                    font: Theme.font(14, Theme.mediumWeight)
                    color: Theme.text
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                }
                Text {
                    Layout.fillWidth: true
                    visible: text.length > 0
                    text: page.startup.forceEnterHint
                    lineHeightMode: Text.FixedHeight
                    lineHeight: 20
                    font: Theme.font(12, Theme.regularWeight)
                    color: Theme.secondaryText
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                }
            }
            Flow {
                objectName: "startupErrorActions"
                Layout.alignment: Qt.AlignHCenter
                Layout.preferredWidth: Math.min(forceButton.visible ? forceButton.implicitWidth + retryButton.implicitWidth + exitButton.implicitWidth + 32 : retryButton.implicitWidth + exitButton.implicitWidth + 16, page.width - 80)
                Layout.preferredHeight: implicitHeight
                Layout.topMargin: 24
                spacing: 16
                EchoButton {
                    id: forceButton
                    objectName: "startupForceEnter"
                    visible: page.startup.canForceEnter
                    text: qsTr("仍然进入")
                    accent: true
                    onClicked: page.startup.forceEnter()
                }
                EchoButton {
                    id: retryButton
                    objectName: "startupRetry"
                    text: qsTr("重试启动")
                    accent: true
                    onClicked: page.startup.retry()
                }
                EchoSoftButton {
                    id: exitButton
                    objectName: "startupExit"
                    text: qsTr("退出应用")
                    onClicked: page.startup.requestQuit()
                }
            }
        }
    }
    Text {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 40
        text: qsTr("EchoMusic • 音为你而生").toUpperCase()
        lineHeightMode: Text.FixedHeight
        lineHeight: 18
        font: Theme.spacedFont(12, Theme.boldWeight, 1.5)
        color: Theme.withAlpha(Theme.text, 0.4)
    }
    Item {
        width: parent.width
        height: Math.max(46, 35 / page.settings.uiScale)
        EchoWindowDragArea {
            anchors.fill: parent
            onMoveRequested: page.window.startSystemMove()
            onDoubleClickRequested: page.windowService.toggleMaximized()
        }
        WindowActions {
            objectName: "startupWindowActions"
            anchors.right: parent.right
            anchors.rightMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            service: page.windowService
            settings: page.settings
            showMiniPlayer: false
        }
    }
}
