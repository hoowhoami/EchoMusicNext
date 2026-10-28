#pragma once
#include "PlaybackService.h"
#include "TrackTransitionController.h"
#include <QtQml/qqmlregistration.h>
namespace EchoMusic
{
struct ListenTogetherControllerRegistration
{
    Q_GADGET
    QML_FOREIGN(ListenTogetherController)
    QML_NAMED_ELEMENT(ListenTogetherController)
    QML_UNCREATABLE("Playback-owned room session")
};
struct ListenTogetherListModelRegistration
{
    Q_GADGET
    QML_FOREIGN(ListenTogetherListModel)
    QML_NAMED_ELEMENT(ListenTogetherListModel)
    QML_UNCREATABLE("Room-owned model")
};
struct PersonalFmControllerRegistration
{
    Q_GADGET
    QML_FOREIGN(PersonalFmController)
    QML_NAMED_ELEMENT(PersonalFmController)
    QML_UNCREATABLE("Playback-owned FM provider")
};
struct PlaybackClimaxModelRegistration
{
    Q_GADGET
    QML_FOREIGN(PlaybackClimaxModel)
    QML_NAMED_ELEMENT(PlaybackClimaxModel)
    QML_UNCREATABLE("Playback-owned climax markers")
};
struct CastServiceRegistration
{
    Q_GADGET
    QML_FOREIGN(CastService)
    QML_NAMED_ELEMENT(CastService)
    QML_UNCREATABLE("Playback-owned output coordinator")
};
struct DspProviderRegistryRegistration
{
    Q_GADGET
    QML_FOREIGN(DspProviderRegistry)
    QML_NAMED_ELEMENT(DspProviderRegistry)
    QML_UNCREATABLE("Playback-owned DSP catalog")
};
struct PlayerEnginePreferencesRegistration
{
    Q_GADGET
    QML_FOREIGN(PlayerEnginePreferences)
    QML_NAMED_ELEMENT(PlayerEnginePreferences)
    QML_UNCREATABLE("Playback-owned engine preferences")
};
struct PlaybackPreferencesRegistration
{
    Q_GADGET
    QML_FOREIGN(PlaybackPreferences)
    QML_NAMED_ELEMENT(PlaybackPreferences)
    QML_UNCREATABLE("Playback-owned preferences")
};
struct TrackTransitionControllerRegistration
{
    Q_GADGET
    QML_FOREIGN(TrackTransitionController)
    QML_NAMED_ELEMENT(TrackTransitionController)
    QML_UNCREATABLE("Playback-owned transitions")
};
struct LyricSourceControllerRegistration
{
    Q_GADGET
    QML_FOREIGN(LyricSourceController)
    QML_NAMED_ELEMENT(LyricSourceController)
    QML_UNCREATABLE("Playback-owned lyric sources")
};
struct PlaybackLibraryActionsRegistration
{
    Q_GADGET
    QML_FOREIGN(PlaybackLibraryActions)
    QML_NAMED_ELEMENT(PlaybackLibraryActions)
    QML_UNCREATABLE("Playback-owned library commands")
};
struct PlaybackServiceRegistration
{
    Q_GADGET
    QML_FOREIGN(PlaybackService)
    QML_NAMED_ELEMENT(PlaybackService)
    QML_UNCREATABLE("Application-owned playback")
};
struct PlaybackQueueRegistration
{
    Q_GADGET
    QML_FOREIGN(PlaybackQueueModel)
    QML_NAMED_ELEMENT(PlaybackQueueModel)
    QML_UNCREATABLE("Playback-owned queue")
};
struct DynamicCoverControllerRegistration
{
    Q_GADGET
    QML_FOREIGN(DynamicCoverController)
    QML_NAMED_ELEMENT(DynamicCoverController)
    QML_UNCREATABLE("Playback-owned optional artwork")
};
struct LyricAppearanceRegistration
{
    Q_GADGET
    QML_FOREIGN(LyricAppearance)
    QML_NAMED_ELEMENT(LyricAppearance)
    QML_UNCREATABLE("Playback-owned lyric appearance")
};
struct LyricPortraitAppearanceRegistration
{
    Q_GADGET
    QML_FOREIGN(LyricPortraitAppearance)
    QML_NAMED_ELEMENT(LyricPortraitAppearance)
    QML_UNCREATABLE("Playback-owned portrait preferences")
};
struct PortraitControllerRegistration
{
    Q_GADGET
    QML_FOREIGN(PortraitController)
    QML_NAMED_ELEMENT(PortraitController)
    QML_UNCREATABLE("Playback-owned portrait gallery")
};
struct LyricModelRegistration
{
    Q_GADGET
    QML_FOREIGN(LyricModel)
    QML_NAMED_ELEMENT(LyricModel)
    QML_UNCREATABLE("Playback-owned lyric timeline")
};
struct PlaybackQueueCatalogRegistration
{
    Q_GADGET
    QML_FOREIGN(PlaybackQueueCatalog)
    QML_NAMED_ELEMENT(PlaybackQueueCatalog)
    QML_UNCREATABLE("Playback-owned catalog")
};
struct AudioEffectCatalogRegistration
{
    Q_GADGET
    QML_FOREIGN(AudioEffectCatalog)
    QML_NAMED_ELEMENT(AudioEffectCatalog)
    QML_UNCREATABLE("Playback-owned effect catalog")
};
struct LyricTextControllerRegistration
{
    Q_GADGET
    QML_FOREIGN(LyricTextController)
    QML_NAMED_ELEMENT(LyricTextController)
    QML_UNCREATABLE("Playback-owned lyric presentation")
};
struct SleepTimerRegistration
{
    Q_GADGET
    QML_FOREIGN(SleepTimer)
    QML_NAMED_ELEMENT(SleepTimer)
    QML_UNCREATABLE("Playback-owned deadline")
};
struct PlaybackAudioControlsRegistration
{
    Q_GADGET
    QML_FOREIGN(PlaybackAudioControls)
    QML_NAMED_ELEMENT(PlaybackAudioControls)
    QML_UNCREATABLE("Playback-owned audio controls")
};
} // namespace EchoMusic
