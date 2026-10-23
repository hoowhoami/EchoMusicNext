pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Controls
import EchoMusic.Theme
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
        Layout.fillWidth: true
        windowService: section.windowService
    }
    MiniLyricSettingsSection {
        Layout.fillWidth: true
        windowService: section.windowService
        playback: section.playback
        uiScale: section.settings.uiScale
    }
}
