#include "MenuBarLyricController.h"
#include "PlaybackService.h"
#include "PlaybackLibraryActions.h"
#include "LyricModel.h"
#include "LyricTextController.h"
#include "ThemeStore.h"
#include <QGuiApplication>
#include <utility>
namespace
{
QVariantMap menuBarLineAt(const EchoMusic::LyricModel &model, qint64 position, bool keepPrevious,
                          int *selectedRow = nullptr)
{
    int row = model.scrollable() ? model.indexAt(position) : model.nextVisible(-1);
    auto line = model.lineAt(row);
    const bool expired = model.scrollable() &&
                         position >= model.startAt(row) + model.durationAt(row) &&
                         model.durationAt(row) > 0;
    if (keepPrevious && (line.value("filtered").toBool() || expired)) {
        row = model.visibleIndex(row);
        line = model.lineAt(row);
    } else if (line.value("filtered").toBool() || expired) {
        return {};
    }
    if (selectedRow)
        *selectedRow = row;
    return line;
}
} // namespace
namespace EchoMusic
{
MenuBarLyricController::MenuBarLyricController(SettingsStore &settings, QObject *parent)
    : QObject(parent), m_settings(settings, this)
{
    connect(&m_settings, &MenuBarLyricPreferences::changed, this, &MenuBarLyricController::sync);
}
MenuBarLyricController::~MenuBarLyricController()
{
    shutdown();
}
void MenuBarLyricController::attachPlayback(PlaybackService &playback)
{
    Q_ASSERT(!m_playback);
    m_playback = &playback;
    connect(&playback, &PlaybackService::changed, this, &MenuBarLyricController::sync);
    connect(&playback, &PlaybackService::clockChanged, this, &MenuBarLyricController::sync);
    connect(playback.lyrics(), &LyricModel::changed, this, &MenuBarLyricController::sync);
    connect(playback.lyricText(), &LyricTextController::changed, this,
            &MenuBarLyricController::sync);
    connect(&playback, &QObject::destroyed, this, [this] {
        m_playback = nullptr;
        m_library = nullptr;
        sync();
    });
    sync();
}
void MenuBarLyricController::attachTheme(ThemeStore &theme)
{
    Q_ASSERT(!m_theme);
    m_theme = &theme;
    connect(&theme, &ThemeStore::changed, this, &MenuBarLyricController::sync);
    connect(&theme, &ThemeStore::fontChanged, this, &MenuBarLyricController::sync);
    connect(&theme, &ThemeStore::lyricFontsChanged, this, &MenuBarLyricController::sync);
    sync();
}
void MenuBarLyricController::setPresentationReady()
{
    m_ready = true;
    sync();
}
void MenuBarLyricController::shutdown()
{
    m_closing = true;
    m_native.reset();
    m_presented = false;
}
void MenuBarLyricController::setIcon(const QImage &icon)
{
    if (m_icon.cacheKey() == icon.cacheKey())
        return;
    m_icon = icon;
    sync();
    emit presentationChanged();
}
void MenuBarLyricController::setShellVisible(bool visible)
{
    if (m_shellVisible == visible)
        return;
    m_shellVisible = visible;
    sync();
}

MenuBarLyricContent MenuBarLyricController::contentAt(const LyricModel &model, qint64 position,
                                                      int mode, bool keepPrevious,
                                                      int secondaryContent, bool romanizationAsRuby)
{
    int row = -1;
    const auto line = menuBarLineAt(model, position, keepPrevious, &row);
    const auto original = line.value("lineText").toString();
    const auto translation = line.value("translation").toString();
    const auto romanization = line.value("romanization").toString();
    MenuBarLyricContent result;
    result.primary = original;
    result.offsetMs = model.offsetMs();
    result.wordTimed = model.scrollable() && line.value("wordTimed").toBool();
    if (result.wordTimed)
        result.words = line.value("words").toList();
    if (mode >= 2 && secondaryContent != 0 && romanizationAsRuby && result.wordTimed)
        result.ruby = model.rubyAt(row);
    QStringList parts;
    if (secondaryContent != 0 && result.ruby.isEmpty() && !romanization.trimmed().isEmpty())
        parts.append(romanization);
    if (result.ruby.isEmpty() && secondaryContent != 1 && !translation.trimmed().isEmpty())
        parts.append(translation);
    const auto secondary = parts.join(QStringLiteral(" / "));
    if (mode == 1 && !secondary.isEmpty()) {
        result.primary = secondary;
        result.primaryIsSecondary = true;
        result.wordTimed = false;
        result.words.clear();
    } else if (mode == 2 && !secondary.isEmpty()) {
        result.primary += QStringLiteral(" · ") + secondary;
        result.secondary = secondary;
    } else if (mode == 3) {
        result.secondary = secondary;
    }
    return result;
}
void MenuBarLyricController::sync()
{
    const bool lyricsEnabled = m_settings.enabled() && m_playback;
    if (m_closing || !available() || !m_ready || (m_icon.isNull() && !lyricsEnabled)) {
        m_native.reset();
        if (std::exchange(m_presented, false))
            emit presentationChanged();
        return;
    }
    if (m_playback && m_library != m_playback->libraryActions()) {
        if (m_library)
            disconnect(m_library, nullptr, this, nullptr);
        m_library = m_playback->libraryActions();
        if (m_library)
            connect(m_library, &PlaybackLibraryActions::changed, this,
                    &MenuBarLyricController::sync);
    }
    m_native.create([this](auto action) { invoke(action); },
                    [this] { return m_playback ? m_playback->clockPosition() : 0.; });
    MenuBarLyricState state;
    state.icon = m_icon;
    state.hasTrack = m_playback && m_playback->hasTrack();
    state.playing = m_playback && m_playback->playing();
    state.advancing = m_playback && m_playback->clock().value("isAdvancing").toBool();
    state.title = state.hasTrack ? m_playback->title() : QStringLiteral("EchoMusicNext");
    state.coverColor = m_playback ? m_playback->coverColor() : QColor();
    state.playMode = m_playback ? m_playback->playMode() : 0;
    state.favorite = m_library && m_library->favorite();
    state.favoriteEnabled = m_library && state.hasTrack && !m_library->favoriteLoading();
    const auto at = m_playback ? qint64(m_playback->clockPosition()) : 0;
    if (lyricsEnabled && state.hasTrack)
        state.content = contentAt(*m_playback->lyrics(), at, m_settings.displayMode(),
                                  m_settings.idleBehavior() == 1, m_settings.secondaryContent(),
                                  m_playback->lyricText()->romanizationAsRuby());
    const bool empty = state.content.primary.isEmpty();
    state.lyricsVisible = lyricsEnabled && !(empty && m_settings.idleBehavior() == 2) &&
                          !(!state.playing && m_settings.pauseBehavior() == 2);
    state.visible = m_shellVisible && (state.lyricsVisible || !state.icon.isNull());
    state.dimmed = !state.playing && m_settings.pauseBehavior() == 1;
    if (empty)
        state.content.primary = state.title;
    auto style = m_settings.style();
    if (m_theme) {
        style.font = m_theme->menuBarLyricBaseFont();
        style.motionEnabled = m_theme->motionEnabled();
    }
    m_native.update(state, style);
    // Confirm creation once; clock updates must not allocate diagnostic maps.
    if (!m_presented && m_native.diagnostics().value("created").toBool()) {
        m_presented = true;
        emit presentationChanged();
    }
}
void MenuBarLyricController::invoke(MenuBarLyric::Action action)
{
    if (m_closing)
        return;
    using Action = MenuBarLyric::Action;
    // Native tracking may still be on the stack. Defer surface retirement and
    // window/settings creation until AppKit has completed that event dispatch.
    if (action == Action::Disable) {
        QMetaObject::invokeMethod(
            this, [this] { m_settings.setEnabled(false); }, Qt::QueuedConnection);
    } else if (action == Action::Settings || action == Action::OpenMain ||
               action == Action::TrayActivate || action == Action::TrayMenu) {
        QMetaObject::invokeMethod(
            this,
            [this, action] {
                if (m_closing)
                    return;
                if (action == Action::Settings)
                    emit settingsRequested();
                else if (action == Action::TrayActivate)
                    emit trayActivateRequested();
                else if (action == Action::TrayMenu)
                    emit trayMenuRequested();
                else
                    emit openMainRequested();
            },
            Qt::QueuedConnection);
    } else if (m_playback && m_playback->hasTrack()) {
        switch (action) {
        case Action::PlayMode:
            m_playback->cycleMode();
            break;
        case Action::Previous:
            m_playback->previous();
            break;
        case Action::Toggle:
            m_playback->toggle();
            break;
        case Action::Next:
            m_playback->next();
            break;
        case Action::Favorite:
            if (m_library && !m_library->favoriteLoading())
                m_library->toggleFavorite();
            break;
        default:
            break;
        }
    }
}
} // namespace EchoMusic
