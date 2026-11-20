pragma ComponentBehavior: Bound
import QtQuick
import EchoMusic.Shell
import EchoMusic.Playback

// Non-window owner: each enabled surface owns its own native window and
// remains independent of the replaceable main window and its QML context.
Item {
    id: owner
    required property WindowController windowService
    required property ShellSettings shellSettings
    required property ShellController shellController
    required property PlaybackService playbackService
    property StartupController startup: null
    readonly property bool ready: startup === null || startup.entered
    Binding {
        target: owner.windowService.floating
        property: "miniScale"
        value: owner.shellSettings.uiScale
    }
    Loader {
        active: owner.ready && owner.windowService.floating.desktopEnabled
        sourceComponent: DesktopLyricWindow {
            service: owner.windowService.floating
            playback: owner.playbackService
        }
    }
    Loader {
        active: owner.ready && owner.windowService.floating.unlockAvailable
        sourceComponent: DesktopLyricUnlockWindow {
            service: owner.windowService.floating
        }
    }
    Loader {
        active: owner.ready && owner.windowService.floating.miniActive
        sourceComponent: MiniPlayerWindow {
            pluginSurfaces: owner.shellController.pluginSurfaces
            service: owner.windowService.floating
            playback: owner.playbackService
        }
    }
    Loader {
        active: owner.ready && owner.windowService.taskbar.enabled
        sourceComponent: TaskbarLyricWindow {
            controller: owner.windowService.taskbar
            playback: owner.playbackService
        }
    }
}
