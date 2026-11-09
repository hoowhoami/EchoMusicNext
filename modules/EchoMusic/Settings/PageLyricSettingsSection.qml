pragma ComponentBehavior: Bound
import QtQuick
import EchoMusic.Controls
import EchoMusic.Theme
import EchoMusic.Playback

SettingsSection {
    id: section
    property real uiScale: 1
    required property PlaybackService playback
    heading: qsTr("播放页")
    glyph: "play"
    readonly property color lyricAccent: Theme.store.lyricAccent(section.playback.coverColor)
    FontFamilySettingsRow {
        heading: qsTr("播放页歌词字体")
        description: qsTr("播放页歌词使用的字体，跟随全局或单独指定")
        selectorName: "settingsLyricFont"
        family: Theme.store.lyricFont
        onFamilySelected: selectedFamily => Theme.store.lyricFont = selectedFamily
    }
    SettingsDivider {}
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
        heading: qsTr("副行颜色")
        description: qsTr("译文、音译等副行使用的颜色，可单独设置或跟随封面")
        LyricSecondaryColorControl {
            uiScale: section.uiScale
            preferences: section.playback.lyricAppearance
            resolvedColor: LyricColors.secondary(section.playback.lyricAppearance, section.lyricAccent, Theme.withAlpha(LyricColors.globalPlayed(section.playback.lyricAppearance, section.lyricAccent), .3))
            coverColor: section.lyricAccent
        }
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
