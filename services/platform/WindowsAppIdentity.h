#pragma once
#include <QString>
namespace EchoMusic
{
/// The Windows shell identity of the running application.
///
/// Windows 11's media panel resolves a media session to a product name and icon
/// through the AppUserModelID: it looks for a Start Menu shortcut that carries
/// the same id. An unpackaged process without such a shortcut falls back to
/// "Unknown app", no matter how complete its executable version resource is
/// (the version resource labels Task Manager and the file properties, not the
/// shell's media session). The installer leaves that shortcut, but a build run
/// straight from its output directory does not, so the identity is published on
/// every launch instead of only on install.
///
/// The same id also decides taskbar button grouping: a process that publishes
/// it is grouped with shortcuts that carry it.
class WindowsAppIdentity final
{
public:
    /// The stable AppUserModelID shared by the process and its Start Menu
    /// shortcut. Kept equal to ECHO_APPLICATION_ID on purpose.
    static QString appUserModelId();
    /// Publishes the AppUserModelID for the process. Must run before the first
    /// window is created, so the shell attributes windows, the tray icon and
    /// media sessions consistently.
    static void publish();
    /// Creates or repairs the Start Menu shortcut that resolves the id to the
    /// product name and icon. Returns false when it cannot be written.
    static bool ensureStartMenuShortcut();
    /// Writes one shortcut with the AppUserModelID stamped on it. Exposed so a
    /// test can pin the property the shell reads.
    static bool writeShortcut(const QString &shortcutPath, const QString &targetPath,
                              const QString &appUserModelId);
};
} // namespace EchoMusic
