pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import EchoMusic.Shell
import EchoMusic.Playback

ColumnLayout {
    id: section
    required property ShellSettings settings
    required property WindowController windowService
    required property PlaybackService playback
    spacing: 28
    CommonLyricSettingsSection {
        Layout.fillWidth: true
        playback: section.playback
    }
    PageLyricSettingsSection {
        Layout.fillWidth: true
        playback: section.playback
        uiScale: section.settings.uiScale
    }
    DesktopLyricSettingsSection {
        Layout.fillWidth: true
        windowService: section.windowService
        playback: section.playback
        uiScale: section.settings.uiScale
    }
    TaskbarLyricSettingsSection {
        playback: section.playback
        uiScale: section.settings.uiScale
        Layout.fillWidth: true
        windowService: section.windowService
    }
    Loader {
        Layout.fillWidth: true
        active: section.windowService.menuBarLyrics.available
        visible: active
        sourceComponent: MenuBarLyricSettingsSection {
            windowService: section.windowService
            playback: section.playback
            uiScale: section.settings.uiScale
        }
    }
    MiniLyricSettingsSection {
        Layout.fillWidth: true
        windowService: section.windowService
        playback: section.playback
        uiScale: section.settings.uiScale
    }
}
