#pragma once
#include "MenuBarLyricPreferences.h"
#include <QPointer>
namespace EchoMusic
{
class PlaybackService;
class PlaybackLibraryActions;
class ThemeStore;
class LyricModel;
/// Application-owned menu bar presentation; never depends on a QML settings page.
class MenuBarLyricController final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by the window controller")
    Q_PROPERTY(EchoMusic::MenuBarLyricPreferences *settings READ settings CONSTANT)
    Q_PROPERTY(bool available READ available CONSTANT)
public:
    explicit MenuBarLyricController(SettingsStore &settings, QObject *parent = nullptr);
    ~MenuBarLyricController() override;
    MenuBarLyricPreferences *settings()
    {
        return &m_settings;
    }
    bool available() const
    {
        return MenuBarLyric::available();
    }
    void attachPlayback(PlaybackService &playback);
    void attachTheme(ThemeStore &theme);
    void setPresentationReady();
    void setIcon(const QImage &icon);
    void setShellVisible(bool visible);
    bool ownsTray() const
    {
        return m_presented && !m_icon.isNull();
    }
    void shutdown();
    /// Projects only the needed source row; static lyrics use the first visible row.
    /// Display modes never manufacture timing for translation or romanization.
    static MenuBarLyricContent contentAt(const LyricModel &model, qint64 position, int displayMode,
                                         bool keepPrevious, int secondaryContent = 0,
                                         bool romanizationAsRuby = false);
    QVariantMap diagnostics() const
    {
        return m_native.diagnostics();
    }
signals:
    void openMainRequested();
    void settingsRequested();
    void trayActivateRequested();
    void trayMenuRequested();
    void presentationChanged();

private:
    void sync();
    void invoke(MenuBarLyric::Action action);
    MenuBarLyricPreferences m_settings;
    MenuBarLyric m_native;
    QPointer<PlaybackService> m_playback;
    QPointer<PlaybackLibraryActions> m_library;
    QPointer<ThemeStore> m_theme;
    QImage m_icon;
    bool m_presented = false, m_shellVisible = true;
    bool m_ready = false, m_closing = false;
};
} // namespace EchoMusic
