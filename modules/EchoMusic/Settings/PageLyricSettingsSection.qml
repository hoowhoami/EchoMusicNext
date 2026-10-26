pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Controls
import EchoMusic.Theme
import EchoMusic.Shell
import EchoMusic.Playback

SettingsSection {
    id: section
    property real uiScale: 1
    required property PlaybackService playback
    heading: qsTr("播放页")
    glyph: "play"
    readonly property color lyricAccent: Theme.store.lyricAccent(section.playback.coverColor)
    LyricColorSettingsRow {
        uiScale: section.uiScale
        heading: qsTr("全局歌词颜色")
        description: qsTr("默认歌词颜色；皮肤中单独设置的颜色优先")
        coverColor: section.playback.coverColor
        playedColor: LyricColors.globalPlayed(section.playback.lyricAppearance, section.lyricAccent)
        unplayedColor: LyricColors.globalUnplayed(section.playback.lyricAppearance, section.lyricAccent)
        followsCover: section.playback.lyricAppearance.followCoverColor
        unplayedFollowsCover: section.playback.lyricAppearance.unplayedFollowCoverColor
        customized: section.playback.lyricAppearance.playedColor.length > 0 || section.playback.lyricAppearance.unplayedColor.length > 0 || followsCover || unplayedFollowsCover
        onColorChosen: (unplayed, value, follow) => {
            if (unplayed) {
                section.playback.lyricAppearance.unplayedFollowCoverColor = follow;
                section.playback.lyricAppearance.unplayedColor = follow ? "" : value;
            } else {
                section.playback.lyricAppearance.followCoverColor = follow;
                section.playback.lyricAppearance.playedColor = follow ? "" : value;
            }
        }
        onResetRequested: section.playback.lyricAppearance.resetColors()
    }
    SettingsDivider {}
    SettingsRow {
        heading: qsTr("封面模糊背景")
        description: qsTr("使用模糊封面作为背景，关闭后使用主题色")
        EchoSwitch {
            colors: Theme.floating
            checked: section.playback.lyricAppearance.backgroundBlur
            onToggled: section.playback.lyricAppearance.backgroundBlur = checked
        }
    }
    SettingsDivider {}
    SettingsRow {
        heading: qsTr("背景律动")
        description: qsTr("让模糊背景色块流动，会增加性能消耗")
        EchoSwitch {
            colors: Theme.floating
            enabled: section.playback.lyricAppearance.backgroundBlur
            checked: section.playback.lyricAppearance.backgroundRhythm
            onToggled: section.playback.lyricAppearance.backgroundRhythm = checked
        }
    }
}
