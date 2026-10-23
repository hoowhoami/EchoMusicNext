#pragma once
#include <QLatin1StringView>

namespace EchoMusic::SettingsKeys
{
// Persisted spellings, bounded dynamic prefixes and field schemas have one
// authority. Domain preference owners retain typed defaults, validation, reset
// and migration. Independent test literals detect storage-format regressions.
namespace Appearance
{
inline constexpr QLatin1StringView Preferences{"appearance/preferences"};
inline constexpr QLatin1StringView ExtensionTheme{"appearance/extensionTheme"};
inline constexpr QLatin1StringView ExtensionSkin{"appearance/extensionSkin"};
} // namespace Appearance
namespace Artists
{
inline constexpr QLatin1StringView AlbumSort{"artists/albumSort"};
inline constexpr QLatin1StringView SongSort{"artists/songSort"};
} // namespace Artists
namespace Audio
{
inline constexpr QLatin1StringView DownloadedEffectsPrefix{"audio/downloadedEffects/"};
inline constexpr QLatin1StringView Equalizer{"audio/equalizer"};
inline constexpr QLatin1StringView Exclusive{"audio/exclusive"};
inline constexpr QLatin1StringView ImpulseEnabled{"audio/impulseEnabled"};
inline constexpr QLatin1StringView ImpulseMix{"audio/impulseMix"};
inline constexpr QLatin1StringView ImpulseNamesPrefix{"audio/impulseNames/"};
inline constexpr QLatin1StringView ImpulseResponse{"audio/impulseResponse"};
inline constexpr QLatin1StringView ImpulseResponses{"audio/impulseResponses"};
inline constexpr QLatin1StringView InputDevice{"audio/inputDevice"};
inline constexpr QLatin1StringView Normalization{"audio/normalization"};
inline constexpr QLatin1StringView OutputDevice{"audio/outputDevice"};
inline constexpr QLatin1StringView PauseOnDisconnect{"audio/pauseOnDisconnect"};
inline constexpr QLatin1StringView ProviderId{"audio/providerId"};
inline constexpr QLatin1StringView ProviderMode{"audio/providerMode"};
inline constexpr QLatin1StringView ProviderPreset{"audio/providerPreset"};
inline constexpr QLatin1StringView ProviderPresetsPrefix{"audio/providerPresets/"};
inline constexpr QLatin1StringView ReferenceLufs{"audio/referenceLufs"};
} // namespace Audio
namespace BarrageFields
{
inline constexpr QLatin1StringView Area{"area"};
inline constexpr QLatin1StringView Density{"density"};
inline constexpr QLatin1StringView FontSize{"fontSize"};
inline constexpr QLatin1StringView Opacity{"opacity"};
inline constexpr QLatin1StringView Speed{"speed"};
} // namespace BarrageFields
namespace Cast
{
inline constexpr QLatin1StringView Enabled{"cast/enabled"};
inline constexpr QLatin1StringView Volumes{"cast/volumes"};
inline constexpr QLatin1StringView VolumesPrefix{"cast/volumes/"};
} // namespace Cast
namespace Floating
{
inline constexpr QLatin1StringView DesktopEnabled{"floating/desktop/enabled"};
inline constexpr QLatin1StringView DesktopGeometry{"floating/desktop/geometry"};
inline constexpr QLatin1StringView DesktopLocked{"floating/desktop/locked"};
inline constexpr QLatin1StringView DesktopPrefix{"floating/desktop/"};
inline constexpr QLatin1StringView MiniAnchor{"floating/mini/anchor"};
inline constexpr QLatin1StringView MiniPrefix{"floating/mini/"};
} // namespace Floating
namespace Taskbar
{
inline constexpr QLatin1StringView Enabled{"taskbar/enabled"};
inline constexpr QLatin1StringView Prefix{"taskbar/"};
} // namespace Taskbar
namespace TaskbarFields
{
inline constexpr QLatin1StringView Position{"position"};
inline constexpr QLatin1StringView MaxWidth{"maxWidth"};
inline constexpr QLatin1StringView FontSize{"fontSize"};
inline constexpr QLatin1StringView ShowTranslation{"showTranslation"};
inline constexpr QLatin1StringView WordByWord{"wordByWord"};
inline constexpr QLatin1StringView DragOffsetX{"dragOffsetX"};
} // namespace TaskbarFields
namespace FloatingFields
{
inline constexpr QLatin1StringView Alignment{"alignment"};
inline constexpr QLatin1StringView AlwaysOnTop{"alwaysOnTop"};
inline constexpr QLatin1StringView BackgroundBlur{"backgroundBlur"};
inline constexpr QLatin1StringView Bold{"bold"};
inline constexpr QLatin1StringView FollowCoverColor{"followCoverColor"};
inline constexpr QLatin1StringView FontSize{"fontSize"};
inline constexpr QLatin1StringView FontWeight{"fontWeight"};
inline constexpr QLatin1StringView Layout{"layout"};
inline constexpr QLatin1StringView LineGap{"lineGap"};
inline constexpr QLatin1StringView PlayedColor{"playedColor"};
inline constexpr QLatin1StringView SecondaryFontSize{"secondaryFontSize"};
inline constexpr QLatin1StringView ShadowStrength{"shadowStrength"};
inline constexpr QLatin1StringView ShowNextLinePreview{"showNextLinePreview"};
inline constexpr QLatin1StringView ShowUnlockButton{"showUnlockButton"};
inline constexpr QLatin1StringView UnplayedColor{"unplayedColor"};
inline constexpr QLatin1StringView UnplayedFollowCoverColor{"unplayedFollowCoverColor"};
} // namespace FloatingFields
namespace Interface
{
inline constexpr QLatin1StringView MotionEnabled{"interface/motionEnabled"};
inline constexpr QLatin1StringView Prefix{"interface/"};
inline constexpr QLatin1StringView ShowFullscreenButton{"interface/showFullscreenButton"};
inline constexpr QLatin1StringView SidebarCollapsed{"interface/sidebarCollapsed"};
inline constexpr QLatin1StringView UiScale{"interface/uiScale"};
} // namespace Interface
namespace Legal
{
inline constexpr QLatin1StringView UserAgreementAccepted{"legal/userAgreementAccepted"};
}
namespace Logging
{
inline constexpr QLatin1StringView ApiResponseBody{"logging/apiResponseBody"};
inline constexpr QLatin1StringView DiagnosticUntil{"logging/diagnosticUntil"};
inline constexpr QLatin1StringView Level{"logging/level"};
inline constexpr QLatin1StringView RetentionDays{"logging/retentionDays"};
} // namespace Logging
namespace Lyrics
{
inline constexpr QLatin1StringView PageUnplayedFollowCoverColor{
    "lyrics/page/unplayedFollowCoverColor"};
inline constexpr QLatin1StringView AppearanceDynamicAlbumCover{
    "lyrics/appearance/dynamicAlbumCover"};
inline constexpr QLatin1StringView AppearanceAlignPosition{"lyrics/appearance/alignPosition"};
inline constexpr QLatin1StringView AppearanceBackgroundBlur{"lyrics/appearance/backgroundBlur"};
inline constexpr QLatin1StringView AppearanceBackgroundRhythm{"lyrics/appearance/backgroundRhythm"};
inline constexpr QLatin1StringView AppearanceEnableBlur{"lyrics/appearance/enableBlur"};
inline constexpr QLatin1StringView AppearanceEnableScale{"lyrics/appearance/enableScale"};
inline constexpr QLatin1StringView AppearanceEnableSpring{"lyrics/appearance/enableSpring"};
inline constexpr QLatin1StringView AppearanceFontScale{"lyrics/appearance/fontScale"};
inline constexpr QLatin1StringView AppearanceFontWeight{"lyrics/appearance/fontWeight"};
inline constexpr QLatin1StringView AppearanceHidePassedLines{"lyrics/appearance/hidePassedLines"};
inline constexpr QLatin1StringView AppearanceHighRefreshRate{"lyrics/appearance/highRefreshRate"};
inline constexpr QLatin1StringView AppearancePrefix{"lyrics/appearance/"};
inline constexpr QLatin1StringView AppearanceTextColor{"lyrics/appearance/textColor"};
inline constexpr QLatin1StringView AppearanceUnplayedTextColor{
    "lyrics/appearance/unplayedTextColor"};
inline constexpr QLatin1StringView AppearanceUnplayedUseCoverColor{
    "lyrics/appearance/unplayedUseCoverColor"};
inline constexpr QLatin1StringView AppearanceUseCoverColor{"lyrics/appearance/useCoverColor"};
inline constexpr QLatin1StringView AppearanceWordFadeWidth{"lyrics/appearance/wordFadeWidth"};
inline constexpr QLatin1StringView BarrageEnabled{"lyrics/barrageEnabled"};
inline constexpr QLatin1StringView BarragePrefix{"lyrics/barrage/"};
inline constexpr QLatin1StringView Conversion{"lyrics/conversion"};
inline constexpr QLatin1StringView FilterEnabled{"lyrics/filterEnabled"};
inline constexpr QLatin1StringView FilterPattern{"lyrics/filterPattern"};
inline constexpr QLatin1StringView GlobalOffsetMs{"lyrics/globalOffsetMs"};
inline constexpr QLatin1StringView OffsetStep{"lyrics/offsetStep"};
inline constexpr QLatin1StringView PageFollowCoverColor{"lyrics/page/followCoverColor"};
inline constexpr QLatin1StringView PagePlayedColor{"lyrics/page/playedColor"};
inline constexpr QLatin1StringView PageUnplayedColor{"lyrics/page/unplayedColor"};
inline constexpr QLatin1StringView RomanizationAsRuby{"lyrics/romanizationAsRuby"};
inline constexpr QLatin1StringView SourcesPrefix{"lyrics/sources/"};
inline constexpr QLatin1StringView TrackOffsetsPrefix{"lyrics/trackOffsets/"};
inline constexpr QLatin1StringView WantRomanization{"lyrics/wantRomanization"};
inline constexpr QLatin1StringView WantTranslation{"lyrics/wantTranslation"};
} // namespace Lyrics
namespace Maintenance
{
inline constexpr QLatin1StringView LastError{"maintenance/lastError"};
}
namespace Navigation
{
inline constexpr QLatin1StringView KeepAliveEnabled{"navigation/keepAliveEnabled"};
inline constexpr QLatin1StringView KeepAliveMax{"navigation/keepAliveMax"};
inline constexpr QLatin1StringView Prefix{"navigation/"};
} // namespace Navigation
namespace Network
{
inline constexpr QLatin1StringView DeviceFingerprint{"network/deviceFingerprint"};
inline constexpr QLatin1StringView DeviceGuid{"network/deviceGuid"};
inline constexpr QLatin1StringView DeviceMac{"network/deviceMac"};
inline constexpr QLatin1StringView DeviceUuid{"network/deviceUuid"};
inline constexpr QLatin1StringView GithubProxyUrl{"network/githubProxyUrl"};
inline constexpr QLatin1StringView LocalPolicy{"network/localPolicy"};
inline constexpr QLatin1StringView RegisteredDevice{"network/registeredDevice"};
inline constexpr QLatin1StringView RenderFingerprint{"network/renderFingerprint"};
inline constexpr QLatin1StringView ResetDeviceIdentity{"network/resetDeviceIdentity"};
} // namespace Network
namespace PersonalFm
{
inline constexpr QLatin1StringView Mode{"personalFm/mode"};
inline constexpr QLatin1StringView SongPool{"personalFm/songPool"};
} // namespace PersonalFm
namespace Playback
{
inline constexpr QLatin1StringView AutoNext{"playback/autoNext"};
inline constexpr QLatin1StringView AutoNextDelaySeconds{"playback/autoNextDelaySeconds"};
inline constexpr QLatin1StringView AutoNextMaxAttempts{"playback/autoNextMaxAttempts"};
inline constexpr QLatin1StringView AutoPlayOnLaunch{"playback/autoPlayOnLaunch"};
inline constexpr QLatin1StringView CompatibilityMode{"playback/compatibilityMode"};
inline constexpr QLatin1StringView DefaultQuality{"playback/defaultQuality"};
inline constexpr QLatin1StringView FadeSeconds{"playback/fadeSeconds"};
inline constexpr QLatin1StringView MatchTempo{"playback/matchTempo"};
inline constexpr QLatin1StringView Mode{"playback/mode"};
inline constexpr QLatin1StringView MvAudioFallbackEnabled{"playback/mvAudioFallbackEnabled"};
inline constexpr QLatin1StringView Prefix{"playback/"};
inline constexpr QLatin1StringView PreventSleep{"playback/preventSleep"};
inline constexpr QLatin1StringView QueueMode{"playback/queueMode"};
inline constexpr QLatin1StringView SongEffect{"playback/songEffect"};
inline constexpr QLatin1StringView Speed{"playback/speed"};
inline constexpr QLatin1StringView TransitionMode{"playback/transitionMode"};
inline constexpr QLatin1StringView ViperQualityEnabled{"playback/viperQualityEnabled"};
inline constexpr QLatin1StringView Volume{"playback/volume"};
inline constexpr QLatin1StringView VolumeFade{"playback/volumeFade"};
inline constexpr QLatin1StringView VolumeFadeMs{"playback/volumeFadeMs"};
} // namespace Playback
namespace PlayerEngine
{
inline constexpr QLatin1StringView PlaybackStallMaxAttempts{
    "playerEngine/playbackStallMaxAttempts"};
inline constexpr QLatin1StringView AudioBufferSecs{"playerEngine/audioBufferSecs"};
inline constexpr QLatin1StringView AudioChannels{"playerEngine/audioChannels"};
inline constexpr QLatin1StringView AudioFormat{"playerEngine/audioFormat"};
inline constexpr QLatin1StringView AudioSamplerate{"playerEngine/audioSamplerate"};
inline constexpr QLatin1StringView CacheMode{"playerEngine/cacheMode"};
inline constexpr QLatin1StringView CachePause{"playerEngine/cachePause"};
inline constexpr QLatin1StringView CachePauseWaitSecs{"playerEngine/cachePauseWaitSecs"};
inline constexpr QLatin1StringView CacheSecs{"playerEngine/cacheSecs"};
inline constexpr QLatin1StringView DemuxerMaxBackMb{"playerEngine/demuxerMaxBackMb"};
inline constexpr QLatin1StringView DemuxerMaxMb{"playerEngine/demuxerMaxMb"};
inline constexpr QLatin1StringView DemuxerReadaheadSecs{"playerEngine/demuxerReadaheadSecs"};
inline constexpr QLatin1StringView GaplessAudio{"playerEngine/gaplessAudio"};
inline constexpr QLatin1StringView PlayResumeTimeout{"playerEngine/playResumeTimeout"};
inline constexpr QLatin1StringView PlaybackStallTimeout{"playerEngine/playbackStallTimeout"};
inline constexpr QLatin1StringView Prefix{"playerEngine/"};
} // namespace PlayerEngine
namespace Plugins
{
inline constexpr QLatin1StringView EntryFormat{"plugins/%1"};
inline constexpr QLatin1StringView FailuresPrefix{"plugins/failures/"};
inline constexpr QLatin1StringView IdPrefix{"plugins/id/"};
inline constexpr QLatin1StringView IdentitiesPrefix{"plugins/identities/"};
inline constexpr QLatin1StringView Installations{"plugins/installations"};
inline constexpr QLatin1StringView Prefix{"plugins/"};
inline constexpr QLatin1StringView RollbackPrefix{"plugins/rollback/"};
inline constexpr QLatin1StringView SafeMode{"plugins/safeMode"};
inline constexpr QLatin1StringView SourcePrefix{"plugins/source/"};
inline constexpr QLatin1StringView Sources{"plugins/sources"};
} // namespace Plugins
namespace Recognition
{
inline constexpr QLatin1StringView Source{"recognition/source"};
}
namespace Search
{
inline constexpr QLatin1StringView DefaultEnabled{"search/defaultEnabled"};
}
namespace Shell
{
inline constexpr QLatin1StringView PlayerbarPrefix{"shell/playerbar/"};
inline constexpr QLatin1StringView TitlebarPrefix{"shell/titlebar/"};
} // namespace Shell
namespace ShellFields
{
inline constexpr QLatin1StringView Badges{"badges"};
inline constexpr QLatin1StringView Order{"order"};
inline constexpr QLatin1StringView Placements{"placements"};
} // namespace ShellFields
namespace Shortcuts
{
inline constexpr QLatin1StringView ConflictPrompt{"shortcuts/conflictPrompt"};
inline constexpr QLatin1StringView GlobalEnabled{"shortcuts/globalEnabled"};
inline constexpr QLatin1StringView GlobalPrefix{"shortcuts/global/"};
inline constexpr QLatin1StringView LocalPrefix{"shortcuts/local/"};
inline constexpr QLatin1StringView Prefix{"shortcuts/"};
inline constexpr QLatin1StringView SuppressDefault{"shortcuts/suppressDefault"};
} // namespace Shortcuts
namespace Sidebar
{
inline constexpr QLatin1StringView EntryOrder{"sidebar/entryOrder"};
inline constexpr QLatin1StringView HiddenEntries{"sidebar/hiddenEntries"};
inline constexpr QLatin1StringView HiddenGroups{"sidebar/hiddenGroups"};
inline constexpr QLatin1StringView Prefix{"sidebar/"};
inline constexpr QLatin1StringView Shortcuts{"sidebar/shortcuts"};
} // namespace Sidebar
namespace Typography
{
inline constexpr QLatin1StringView DesktopLyricFont{"typography/desktopLyricFont"};
inline constexpr QLatin1StringView GlobalFont{"typography/globalFont"};
inline constexpr QLatin1StringView LyricFont{"typography/lyricFont"};
inline constexpr QLatin1StringView MiniLyricFont{"typography/miniLyricFont"};
inline constexpr QLatin1StringView Prefix{"typography/"};
inline constexpr QLatin1StringView TaskbarLyricFont{"typography/taskbarLyricFont"};
} // namespace Typography
namespace Updates
{
inline constexpr QLatin1StringView AutoCheck{"updates/autoCheck"};
inline constexpr QLatin1StringView Prefix{"updates/"};
inline constexpr QLatin1StringView Prerelease{"updates/prerelease"};
inline constexpr QLatin1StringView Silent{"updates/silent"};
} // namespace Updates
namespace Video
{
inline constexpr QLatin1StringView BarrageEnabled{"video/barrageEnabled"};
inline constexpr QLatin1StringView BarragePrefix{"video/barrage/"};
} // namespace Video
namespace Window
{
inline constexpr QLatin1StringView CloseToTray{"window/closeToTray"};
inline constexpr QLatin1StringView HideDockInBackground{"window/hideDockInBackground"};
inline constexpr QLatin1StringView HideMenuBarInBackground{"window/hideMenuBarInBackground"};
inline constexpr QLatin1StringView NormalGeometry{"window/normalGeometry"};
inline constexpr QLatin1StringView RememberSize{"window/rememberSize"};
inline constexpr QLatin1StringView StartMinimized{"window/startMinimized"};
} // namespace Window
namespace Cloud
{
inline constexpr QLatin1StringView UploadBackgroundReminderMuted{
    "cloud/upload/backgroundReminderMuted"};
}
namespace Library
{
inline constexpr QLatin1StringView ImportBackgroundReminderMuted{
    "library/import/backgroundReminderMuted"};
}
} // namespace EchoMusic::SettingsKeys
