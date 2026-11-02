pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import EchoMusic.Theme
import EchoMusic.Controls
import EchoMusic.PluginSdk
import EchoMusic.Plugins

EchoDialog {
    id: dialog
    required property PluginRuntime runtime
    required property string installationId
    required property EchoPluginContext scope
    objectName: "pluginSettingsDialog"
    heading: qsTr("%1 设置").arg(dialog.runtime.record(dialog.installationId).name || qsTr("插件"))
    preferredWidth: 640
    maximumHeight: 760
    windowHeightMargin: 120
    flushBody: true
    // Plugin settings provide their own dialog blur and opacity.
    background: EchoFloatingSurface {
        radius: Theme.dialogRadius
        color: Theme.withAlpha(Theme.elevated, Theme.dark ? 0.98 : 0.96)
        frostedEnabled: true
        blurSigma: 18
        backdropFillOpacity: 1
        border.width: 1
        border.color: Theme.floating.surfaceOutline
        dialogShadow: true
    }
    body: Component {
        Flickable {
            id: viewport
            objectName: "pluginSettingsViewport"
            implicitHeight: contentHeight
            contentWidth: width
            contentHeight: settingsLoader.height
            boundsBehavior: Flickable.StopAtBounds
            clip: true
            ScrollBar.vertical: EchoPopupScrollBar {
                surface: dialog
                view: viewport
                resourceActive: dialog.visible
            }
            Loader {
                id: settingsLoader
                width: Math.max(0, viewport.width - 24)
                height: item ? (item as Item).implicitHeight : 80
                active: dialog.visible && dialog.scope !== null && dialog.scope.active && dialog.scope.ui.settingsComponent.toString().length > 0
                onActiveChanged: reload()
                function reload() {
                    setSource(active ? dialog.scope.ui.settingsComponent : "", active ? {
                        plugin: dialog.scope
                    } : {});
                }
                Component.onCompleted: reload()
                Connections {
                    target: dialog.scope ? dialog.scope.ui : null
                    function onSettingsChanged() {
                        settingsLoader.reload();
                    }
                }
            }
            Label {
                anchors.centerIn: parent
                visible: !settingsLoader.active
                text: qsTr("插件设置入口不可用")
                font: Theme.font(13, Theme.regularWeight)
                color: Theme.floatingSecondaryText
            }
        }
    }
}
