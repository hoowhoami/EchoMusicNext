pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Theme
import EchoMusic.Controls
import EchoMusic.Plugins

Popup {
    id: overlay
    objectName: "pluginLocalInstallOverlay"
    required property PluginManager manager
    required property Item coveredItem
    property real uiScale: 1
    property bool resourceActive: true
    property bool dragging: false
    // Qt's Overlay stays outside the registered ordinary backdrop scene. A
    // page-child blur would capture itself and create rendering feedback.
    parent: coveredItem.Overlay.overlay
    scale: uiScale
    transformOrigin: Popup.TopLeft
    x: geometry.rect.x
    y: geometry.rect.y
    width: geometry.rect.width / uiScale
    height: geometry.rect.height / uiScale
    padding: 0
    enabled: false
    focus: false
    modal: false
    closePolicy: Popup.NoAutoClose
    EchoPopupAnchor {
        id: geometry
        item: overlay.coveredItem
        coordinateItem: overlay.parent
    }
    EchoPopupLifecycle {
        surface: overlay
    }
    background: EchoFloatingSurface {
        color: Theme.withAlpha(Theme.main, 0.82)
        radius: 0
        frostedEnabled: true
        blurSigma: 8
        backdropFillOpacity: 1
        shadowEnabled: false
        border.width: 0
    }
    contentItem: Item {

        Item {
            id: installPanel
            anchors.centerIn: parent
            width: Math.min(352, parent.width - 64)
            height: Math.max(176, installContents.implicitHeight + 48)
            Rectangle {
                id: panelFill
                anchors.fill: parent
                radius: Theme.popoverRadius
                color: Theme.withAlpha(Theme.elevated, 0.96)
                EchoSurfaceShadow {
                    anchors.fill: parent
                    surface: panelFill
                    z: -1
                    radius: panelFill.radius
                    blur: 57.6
                    offset: Qt.vector2d(0, 18)
                    color: Theme.withAlpha(Theme.text, 0.14)
                }
            }
            // The pixel-snapped frame keeps the dashed outline stable
            // while the window resizes; a Shape path re-tessellates
            // every frame and visibly jittered here.
            EchoDashedFrame {
                anchors.fill: parent
                strokeColor: Theme.mix(Theme.subtleBorder, Theme.accent, 0.54)
                radius: Theme.popoverRadius
                dashLength: 4
            }
            ColumnLayout {
                id: installContents
                anchors.centerIn: parent
                width: parent.width - 48
                spacing: 12
                Rectangle {
                    Layout.alignment: Qt.AlignHCenter
                    implicitWidth: 52
                    implicitHeight: 52
                    radius: Theme.controlRadius
                    color: Theme.withAlpha(Theme.accent, 0.12)
                    EchoIcon {
                        objectName: "pluginLocalInstallIcon"
                        anchors.centerIn: parent
                        name: overlay.manager.busy ? "refresh" : "arrow-bar-down"
                        width: 28
                        height: 28
                        color: Theme.accentText
                        RotationAnimator on rotation {
                            running: overlay.visible && overlay.manager.busy && Theme.store.motionEnabled
                            from: 0
                            to: 360
                            duration: 1000
                            loops: Animation.Infinite
                        }
                    }
                }
                Label {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    text: overlay.manager.busy ? qsTr("正在安装 %1 个插件").arg(overlay.manager.localInstallCount) : qsTr("松开安装插件")
                    color: Theme.text
                    font: Theme.font(15.2, Theme.extraBoldWeight)
                }
                Label {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    text: overlay.manager.busy ? qsTr("安装完成后会自动刷新插件列表") : qsTr("支持 .echomod 插件包和开发目录")
                    color: Theme.secondaryText
                    font: Theme.font(12, Theme.semiboldWeight)
                }
            }
        }
    }
}
