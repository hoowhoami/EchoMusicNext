#include "ShortcutActions.h"
#include "ShortcutService.h"
#include "ShellController.h"
#include "ShellSettings.h"
#include "WindowController.h"
namespace EchoMusic
{
ShortcutActions::ShortcutActions(ShortcutService &shortcuts, PlaybackService &playback,
                                 ShellSettings &settings, WindowController &window,
                                 ShellController &shell, QObject *parent)
    : QObject(parent)
{
    connect(&shortcuts, &ShortcutService::triggered, this,
            [&playback, &settings, &window, &shell](ShortcutService::Command command) {
                using Shortcut = ShortcutService;
                switch (command) {
                case Shortcut::TogglePlayback:
                    playback.toggle();
                    break;
                case Shortcut::PreviousTrack:
                    playback.previous();
                    break;
                case Shortcut::NextTrack:
                    playback.next();
                    break;
                case Shortcut::SeekBackward:
                    playback.seek(qMax<qint64>(0, playback.positionMs() - 5000));
                    break;
                case Shortcut::SeekForward:
                    playback.seek(qMin(playback.durationMs(), playback.positionMs() + 5000));
                    break;
                // PlaybackService and the native output contract use percent, not 0..1.
                case Shortcut::VolumeUp:
                    playback.setVolume(playback.volume() + 5);
                    break;
                case Shortcut::VolumeDown:
                    playback.setVolume(playback.volume() - 5);
                    break;
                case Shortcut::ToggleMute:
                    playback.toggleMute();
                    break;
                case Shortcut::ToggleFavorite:
                    shell.invokePlayerCommand(ShellController::Favorite);
                    break;
                case Shortcut::TogglePlayMode:
                    playback.cycleMode();
                    break;
                case Shortcut::ToggleWindow:
                    window.toggleVisibility();
                    break;
                case Shortcut::ToggleSidebar:
                    settings.toggleSidebar();
                    break;
                case Shortcut::ToggleMainLyric:
                    window.requestPlaybackPage();
                    break;
                case Shortcut::ToggleMiniPlayer:
                    window.floating()->toggleMini();
                    break;
                case Shortcut::ToggleDesktopLyric:
                    window.floating()->toggleDesktop();
                    break;
                }
            });
}
} // namespace EchoMusic
